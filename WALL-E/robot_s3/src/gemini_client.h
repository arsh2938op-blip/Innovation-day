// ============================================================
//  Gemini client - the AI backend
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class GeminiClient {
public:
    bool ready() const { return strlen(WALLE_GEMINI_API_KEY) > 0; }

    // Blocking (bounded by GEMINI_TIMEOUT_MS). Returns the plain text answer.
    bool ask(const String& userText, String* outText);

    // Same, but with a caller-supplied instruction (used for jokes/banter).
    bool askWithPrompt(const String& userText, const char* extraPrompt, String* outText);

    // Overrides the system instruction for every later request. This
    // is how the app's persona takes effect: Persona::prompt() is
    // handed here, and null restores GEMINI_SYSTEM_PROMPT.
    void setSystemInstruction(const char* text);
    const char* systemInstruction() const;

    // Quick connectivity probe for the hardware test menu.
    bool ping();

private:
    const char* _instruction = nullptr;   // nullptr = use GEMINI_SYSTEM_PROMPT
};
