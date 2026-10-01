// ============================================================
//  Persona implementation
//  See persona.h for the contract and the reasoning.
// ============================================================
#include "persona.h"
#include "log.h"

static const char* TAG = "PERSONA";

Persona persona;

void Persona::begin() {
    strncpy(_name, PERSONA_DEFAULT_NAME, sizeof(_name) - 1);
    strncpy(_suffix, PERSONA_DEFAULT_SUFFIX, sizeof(_suffix) - 1);
    strncpy(_mood, "grumpy", sizeof(_mood) - 1);
    strncpy(_prompt, GEMINI_SYSTEM_PROMPT, sizeof(_prompt) - 1);
    _fromApp = false;
    LOGI(TAG, "Default persona: %s", _name);
}

// ------------------------------------------------------------
//  Pull one single-letter key out of the compact JSON.
//
//  Deliberately hand-rolled rather than ArduinoJson: the payload is
//  240 bytes, the keys are single letters with string values, and a
//  full document parse would cost far more RAM than the answer is
//  worth. It also lets this refuse an over-long field cleanly.
// ------------------------------------------------------------
bool Persona::field(const char* json, char key, char* out, size_t maxLen,
                    bool allowEmpty) {
    const size_t outCap = maxLen - 1;          // room for the terminator

    // Find  "<key>"  then its colon. Anchoring on the quotes is what
    // stops "n" matching the n inside "json" or a prompt.
    char needle[4] = { '"', key, '"', 0 };
    const char* p = strstr(json, needle);
    if (!p) return false;

    p += 3;                                    // past "k"
    while (*p == ' ' || *p == ':') p++;       // skip : and whitespace
    if (*p != '"') return false;              // value must be a string
    p++;

    size_t n = 0;
    while (*p && *p != '"') {
        // Reject rather than truncate: a clipped personality is worse
        // than none, and the caller turns this into BAD_ARG.
        if (n >= outCap) return false;
        // Skip backslash escapes so \" inside the prompt cannot end
        // the string early.
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    if (*p != '"') return false;              // unterminated
    if (n == 0 && !allowEmpty) return false;  // name and prompt are required

    out[n] = 0;
    return true;
}

bool Persona::apply(const char* json) {
    if (!json || !*json) {
        LOGW(TAG, "Empty persona payload");
        return false;
    }

    // Parse into scratch first. Nothing in the live persona is touched
    // until the whole payload has been accepted, which is what makes
    // this all-or-nothing.
    char name[PERSONA_NAME_MAX]      = {0};
    char suffix[PERSONA_SUFFIX_MAX] = {0};
    char mood[PERSONA_MOOD_MAX]      = {0};
    char prompt[PERSONA_PROMPT_MAX]  = {0};

    // n and p are the essence of a personality: with no name and no
    // instruction there is nothing to be. s and m are decoration, so an
    // empty string is allowed for those two.
    const bool haveName   = field(json, 'n', name, sizeof(name), false);
    const bool haveSuffix = field(json, 's', suffix, sizeof(suffix), true);
    const bool haveMood   = field(json, 'm', mood, sizeof(mood), true);
    const bool havePrompt = field(json, 'p', prompt, sizeof(prompt), false);

    if (!haveName || !haveSuffix || !haveMood || !havePrompt) {
        LOGW(TAG, "Persona rejected (n=%d s=%d m=%d p=%d)",
             haveName, haveSuffix, haveMood, havePrompt);
        return false;
    }

    // Accepted - publish it.
    strncpy(_name, name, sizeof(_name) - 1);
    strncpy(_suffix, suffix, sizeof(_suffix) - 1);
    strncpy(_mood, mood, sizeof(_mood) - 1);
    strncpy(_prompt, prompt, sizeof(_prompt) - 1);
    _fromApp = true;

    LOGI(TAG, "Persona set: %s (%s)", _name, _mood);
    LOGI(TAG, "  prompt: %s", _prompt);
    LOGI(TAG, "  suffix: %s", _suffix);
    return true;
}

// ------------------------------------------------------------
//  Case-insensitive "does it already end with the suffix?"
// ------------------------------------------------------------
static bool endsWithNoCase(const String& s, const char* suffix) {
    if (!suffix || !*suffix) return true;
    const size_t n = strlen(suffix);
    String t = s;
    t.trim();
    if (t.length() < n) return false;

    // Compare the tail. Only the suffix is folded to upper case, so a
    // multi-byte character elsewhere in the reply cannot corrupt it.
    String tail = t.substring(t.length() - n);
    tail.toUpperCase();
    String want(suffix);
    want.toUpperCase();
    return tail.equals(want);
}

void Persona::decorate(String* reply) const {
    if (!reply || reply->length() == 0) return;
    if (!_suffix[0]) return;
    if (endsWithNoCase(*reply, _suffix)) return;   // Gemini already did it

    reply->trim();
    reply->concat(" ");
    reply->concat(_suffix);
}