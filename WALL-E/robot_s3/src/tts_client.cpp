#include "tts_client.h"
#include "audio_output.h"
#include "log.h"

#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>   // root CA bundle, needed for HTTPS
#include <ArduinoJson.h>

static const char* TAG = "TTS";

namespace {

// The JSON body is small (it carries only the text), so it is built in
// one piece. The AUDIO comes back the other way and is streamed.
const char* kContentType = "application/json";

// The response is read in slices of this size. 1024 base64 chars decode
// to 768 PCM bytes, which is 16 ms of audio.
const size_t kReadChunk = 1024;

// States of the "find the base64 in the noise" scanner.
enum ScanState {
    SCAN_LOOKING,     // hunting for the  "data":"  marker
    SCAN_IN_DATA,     // consuming base64 characters
    SCAN_DONE,        // closing quote seen
};

int8_t b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return (int8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int8_t)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (int8_t)(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Marker we look for: "data":"  (the field holding the base64 audio).
const char* kDataMarker = "\"data\":\"";

}  // namespace

bool TtsClient::ready() const {
    return WALLE_ENABLE_TTS && strlen(WALLE_GEMINI_API_KEY) > 0;
}

size_t TtsClient::splitText(const String& text, String* out, size_t maxChunks) {
    size_t n = 0;
    if (text.length() == 0 || maxChunks == 0) return 0;

    int start = 0;
    while (start < (int)text.length() && n < maxChunks) {
        int len = TTS_MAX_CHARS;
        if (start + len >= (int)text.length()) {
            out[n++] = text.substring(start);
            break;
        }
        int cut = text.lastIndexOf('.', start + len);
        if (cut <= start) cut = text.lastIndexOf(',', start + len);
        if (cut <= start) cut = text.lastIndexOf(' ', start + len);
        if (cut <= start) cut = start + len;
        out[n++] = text.substring(start, cut + 1);
        start = cut + 1;
        while (start < (int)text.length() && text[start] == ' ') start++;
    }
    return n;
}

bool TtsClient::speak(const String& text) {
    if (!ready()) { LOGE(TAG, "Disabled or no Gemini key"); return false; }
    if (text.length() == 0) { LOGE(TAG, "Nothing to say"); return false; }
    if (WiFi.status() != WL_CONNECTED) { LOGE(TAG, "Offline"); return false; }
    if (!speaker.ready()) { LOGW(TAG, "No speaker wired - skipping"); return false; }

    return speakFromGemini(text) >= 0;
}

bool TtsClient::speakLong(const String& text) {
    if (text.length() <= TTS_MAX_CHARS) return speak(text);

    String chunks[4];
    const size_t n = splitText(text, chunks, 4);
    if (n == 0) return false;

    bool allOk = true;
    for (size_t i = 0; i < n; i++) {
        LOGI(TAG, "Chunk %u/%u", (unsigned)(i + 1), (unsigned)n);
        if (!speak(chunks[i])) allOk = false;
    }
    return allOk;
}

// ------------------------------------------------------------
//  The streaming request
// ------------------------------------------------------------
long TtsClient::speakFromGemini(const String& text) {
    // ---- request body: text + voice + "I want audio" ----
    JsonDocument doc;
    JsonObject gen = doc["generationConfig"].to<JsonObject>();
    JsonArray modes = gen["responseModalities"].to<JsonArray>();
    modes.add("AUDIO");
    JsonObject speech = gen["speechConfig"].to<JsonObject>();
    JsonObject prebuilt = speech["voiceConfig"]["prebuiltVoiceConfig"].to<JsonObject>();
    prebuilt["voiceName"] = TTS_VOICE;
    doc["contents"][0]["role"] = "user";
    doc["contents"][0]["parts"][0]["text"] = String(TTS_STYLE_PREFIX) + text;

    String body;
    serializeJson(doc, body);

    char url[192];
    snprintf(url, sizeof(url), "https://%s%s", TTS_HOST, TTS_PATH);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = TTS_TIMEOUT_MS;
    // The Arduino core ships the Mozilla root CA bundle and wraps it in
    // this helper. It is what makes plain HTTPS work with no user
    // certificate setup.
    cfg.crt_bundle_attach = arduino_esp_crt_bundle_attach;
    cfg.event_handler = nullptr;
    cfg.user_data = nullptr;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { LOGE(TAG, "client init failed"); return -1; }

    esp_http_client_set_header(client, "Content-Type", kContentType);
    esp_http_client_set_header(client, TTS_API_KEY_HEADER, WALLE_GEMINI_API_KEY);

    // ---- send the request ----
    LOGI(TAG, "Request sent (%u chars, voice %s)", (unsigned)text.length(), TTS_VOICE);

    esp_err_t err = esp_http_client_open(client, (int)body.length());
    if (err != ESP_OK) {
        LOGE(TAG, "open failed (%s)", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }
    int written = 0;
    while (written < (int)body.length()) {
        const int w = esp_http_client_write(client, body.c_str() + written,
                                           body.length() - written);
        if (w <= 0) {
            LOGE(TAG, "write failed after %d bytes", written);
            esp_http_client_cleanup(client);
            return -1;
        }
        written += w;
    }
    // 0 terminates the request body for this API.
    esp_http_client_write(client, "", 0);

    const int status = esp_http_client_fetch_headers(client);
    if (status != 200) {
        LOGE(TAG, "HTTP %d", status);
        esp_http_client_cleanup(client);
        return -1;
    }

    // ---- stream the response ----
    if (!speaker.beginStream(TTS_SAMPLE_RATE)) {
        LOGE(TAG, "could not open the speaker stream");
        esp_http_client_cleanup(client);
        return -1;
    }

    ScanState scan = SCAN_LOOKING;
    int matched = 0;                 // bytes of the marker matched so far
    char quad[4];
    int quadLen = 0;
    uint8_t decoded[3];
    int decodedLen = 0;
    size_t totalDecoded = 0;
    bool sawAudio = false;

    char* buf = (char*)malloc(kReadChunk);
    if (!buf) {
        LOGE(TAG, "Out of memory");
        speaker.endStream();
        esp_http_client_cleanup(client);
        return -1;
    }

    for (;;) {
        const int n = esp_http_client_read(client, buf, kReadChunk);
        if (n < 0) {
            LOGE(TAG, "read error %d", n);
            free(buf);
            speaker.endStream();
            esp_http_client_cleanup(client);
            return -1;
        }
        if (n == 0) break;                       // end of body

        for (int i = 0; i < n; i++) {
            const char c = buf[i];

            if (scan == SCAN_LOOKING) {
                if (c == kDataMarker[matched]) {
                    matched++;
                    if (matched == (int)strlen(kDataMarker)) {
                        scan = SCAN_IN_DATA;
                        matched = 0;
                        LOGI(TAG, "Found audio payload, streaming...");
                    }
                } else {
                    // Restart the match; handles overlap like  ""data
                    matched = (c == kDataMarker[0]) ? 1 : 0;
                }
                continue;
            }

            if (scan == SCAN_IN_DATA) {
                if (c == '"') { scan = SCAN_DONE; break; }   // end of the base64
                const int8_t v = b64Value(c);
                if (v < 0) continue;                          // whitespace/newlines
                quad[quadLen++] = c;
                if (quadLen < 4) continue;
                quadLen = 0;

                // Decode 4 chars -> 3 bytes.
                decoded[0] = (uint8_t)((b64Value(quad[0]) << 2) | (b64Value(quad[1]) >> 4));
                decoded[1] = (uint8_t)((b64Value(quad[1]) << 4) | (b64Value(quad[2]) >> 2));
                decoded[2] = (uint8_t)((b64Value(quad[2]) << 6) | b64Value(quad[3]));
                decodedLen = 3;
                speaker.feed(decoded, decodedLen);
                totalDecoded += decodedLen;
                sawAudio = true;
                continue;
            }
        }
    }

    free(buf);
    speaker.endStream();
    esp_http_client_cleanup(client);

    if (!sawAudio || totalDecoded == 0) {
        LOGE(TAG, "No audio in the response");
        return -1;
    }

    LOGI(TAG, "Audio received (%u bytes, %.1f s)",
         (unsigned)totalDecoded, totalDecoded / (float)(TTS_SAMPLE_RATE * 2));
    return (long)totalDecoded;
}