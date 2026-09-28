#include "gemini_client.h"
#include "log.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char* TAG = "GEMINI";

bool GeminiClient::ask(const String& userText, String* outText) {
    return askWithPrompt(userText, nullptr, outText);
}

bool GeminiClient::askWithPrompt(const String& userText, const char* extraPrompt, String* outText) {
    outText->clear();

    if (!ready()) { LOGE(TAG, "No API key configured"); return false; }
    if (WiFi.status() != WL_CONNECTED) { LOGE(TAG, "Offline"); return false; }

    String sys = GEMINI_SYSTEM_PROMPT;
    if (extraPrompt && *extraPrompt) { sys += " "; sys += extraPrompt; }

    JsonDocument doc;
    JsonArray contents = doc["contents"].to<JsonArray>();
    JsonObject turn = contents.add<JsonObject>();
    turn["role"] = "user";
    turn["parts"][0]["text"] = userText;

    JsonArray sysParts = doc["systemInstruction"]["parts"].to<JsonArray>();
    sysParts.add<JsonObject>()["text"] = sys;

    JsonObject gen = doc["generationConfig"].to<JsonObject>();
    gen["maxOutputTokens"] = GEMINI_MAX_TOKENS;
    gen["temperature"] = GEMINI_TEMPERATURE;

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(GEMINI_TIMEOUT_MS);
    String url = String("https://") + GEMINI_HOST + GEMINI_PATH;
    if (!http.begin(url)) { LOGE(TAG, "begin() failed"); return false; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader(GEMINI_API_KEY_HEADER, WALLE_GEMINI_API_KEY);

    LOGI(TAG, "Request sent (%u chars)", (unsigned)userText.length());
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

    const char* text = rdoc["candidates"][0]["content"]["parts"][0]["text"] | "";
    if (!text || !*text) {
        // safety blocks and empty candidates come back as a 200
        const char* block = rdoc["promptFeedback"]["blockReason"] | "";
        if (block && *block) LOGE(TAG, "Blocked: %s", block);
        else LOGE(TAG, "Empty response");
        return false;
    }

    *outText = text;
    LOGI(TAG, "Response received (%u chars)", (unsigned)outText->length());
    return true;
}

bool GeminiClient::ping() {
    if (!ready()) { LOGE(TAG, "No API key configured"); return false; }
    if (WiFi.status() != WL_CONNECTED) { LOGE(TAG, "Offline"); return false; }

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(GEMINI_TIMEOUT_MS);
    String url = String("https://") + GEMINI_HOST + "/v1beta/models";
    if (!http.begin(url)) return false;
    http.addHeader(GEMINI_API_KEY_HEADER, WALLE_GEMINI_API_KEY);

    int code = http.GET();
    http.end();
    if (code == HTTP_CODE_OK) { LOGI(TAG, "Reachable (models list HTTP 200)"); return true; }
    LOGE(TAG, "HTTP %d", code);
    return false;
}
