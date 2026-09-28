#include "tts_client.h"
#include "log.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

static const char* TAG = "TTS";

static const char* kApiKeyHeader = "x-goog-api-key";

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
        // break on the nearest sentence boundary
        int cut = text.lastIndexOf('.', start + len);
        if (cut <= start) cut = text.lastIndexOf(' ', start + len);
        if (cut <= start) cut = start + len;
        out[n++] = text.substring(start, cut + 1);
        start = cut + 1;
        while (start < (int)text.length() && text[start] == ' ') start++;
    }
    return n;
}

bool TtsClient::synthesize(const String& text, uint8_t** outPcm, size_t* outBytes) {
    *outPcm = nullptr;
    *outBytes = 0;

    if (!ready()) { LOGE(TAG, "No API key configured"); return false; }
    if (text.length() == 0) { LOGE(TAG, "Empty text"); return false; }

    JsonDocument doc;
    doc["text"] = text;
    JsonObject voice = doc["voice"].to<JsonObject>();
    voice["languageCode"] = TTS_VOICE_LANGUAGE;
    voice["name"] = TTS_VOICE_NAME;
    JsonObject ac = doc["audioConfig"].to<JsonObject>();
    ac["audioEncoding"] = "LINEAR16";
    ac["sampleRateHertz"] = AUDIO_SAMPLE_RATE;

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(TTS_TIMEOUT_MS);
    String url = String("https://") + TTS_HOST + TTS_PATH;
    if (!http.begin(url)) { LOGE(TAG, "begin() failed"); return false; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader(kApiKeyHeader, WALLE_TTS_API_KEY);

    LOGI(TAG, "Request sent");
    int code = http.POST(body);
    if (code != HTTP_CODE_OK) {
        LOGE(TAG, "HTTP %d", code);
        http.end();
        return false;
    }

    String resp = http.getString();
    http.end();

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp)) { LOGE(TAG, "Bad JSON response"); return false; }
    const char* b64 = rdoc["audioContent"] | "";
    if (!b64 || !*b64) { LOGE(TAG, "No audioContent in response"); return false; }

    size_t b64Len = strlen(b64);
    size_t rawLen = ((b64Len / 4) + 1) * 3;
    uint8_t* raw = (uint8_t*)malloc(rawLen);
    if (!raw) { LOGE(TAG, "Out of memory decoding audio"); return false; }

    size_t decoded = 0;
    if (mbedtls_base64_decode(raw, rawLen, &decoded,
                              (const unsigned char*)b64, b64Len) != 0) {
        LOGE(TAG, "base64 decode failed");
        free(raw);
        return false;
    }

    *outPcm = raw;
    *outBytes = decoded;
    LOGI(TAG, "Audio received (%u bytes, %.1f s)", (unsigned)decoded, decoded / (float)(AUDIO_SAMPLE_RATE * 2));
    return true;
}

void TtsClient::freeAudio(uint8_t* pcm, size_t) {
    if (pcm) free(pcm);
}
