#include "stt_client.h"
#include "log.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

static const char* TAG = "STT";

bool SttClient::transcribe(const uint8_t* pcm, size_t bytes, String* outText) {
    outText->clear();

    if (!ready()) { LOGE(TAG, "Disabled or no API key"); return false; }
    if (!pcm || bytes == 0) { LOGE(TAG, "No audio to transcribe"); return false; }

    // ---- base64 encode the clip (sent in the JSON body) ----
    // mbedTLS is used directly instead of base64::encode() because the
    // Arduino helper returns a String, which would double the RAM needed
    // for a 96 kB recording.
    size_t b64Len = ((bytes + 2) / 3) * 4 + 1;
    char* b64 = (char*)malloc(b64Len);
    if (!b64) { LOGE(TAG, "Out of memory encoding %u bytes", (unsigned)bytes); return false; }
    size_t written = 0;
    if (mbedtls_base64_encode((unsigned char*)b64, b64Len, &written,
                              (const unsigned char*)pcm, bytes) != 0) {
        LOGE(TAG, "base64 encode failed");
        free(b64);
        return false;
    }
    b64[written] = 0;

    JsonDocument doc;
    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["encoding"] = "LINEAR16";
    cfg["sampleRateHertz"] = AUDIO_SAMPLE_RATE;
    cfg["languageCode"] = STT_LANGUAGE_CODE;
    doc["audio"]["content"] = b64;

    String body;
    serializeJson(doc, body);
    free(b64);

    LOGI(TAG, "Request sent (%u bytes audio)", (unsigned)bytes);
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(STT_TIMEOUT_MS);
    String url = String("https://") + STT_HOST + STT_PATH;
    if (!http.begin(url)) { LOGE(TAG, "begin() failed"); return false; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-goog-api-key", WALLE_STT_API_KEY);

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

    const char* transcript = rdoc["results"][0]["alternatives"][0]["transcript"] | "";
    if (!transcript || !*transcript) {
        // An empty result is normal for silence - not a hard error.
        LOGW(TAG, "No speech recognised");
        return false;
    }

    *outText = transcript;
    LOGI(TAG, "Transcript received: \"%s\"", outText->c_str());
    return true;
}
