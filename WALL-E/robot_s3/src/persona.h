// ============================================================
//  Persona - who WALL-E is, decided by the app
//  ------------------------------------------------------------
//  The app owns the robot's personality. On every connect it sends
//  SET_PERSONA followed by a TEXT frame (op 0x04) carrying compact
//  JSON:
//
//    {"n":"Vulkan","s":"Friend!","m":"happy","p":"You are ..."}
//
//  See shared/walle_protocol.h for the key meanings.
//
//  WHY THE APP DECIDES THIS, NOT THE FIRMWARE
//  ------------------------------------------
//  A demo build should be able to change who the robot is without
//  reflashing anything, and the persona has to arrive before the
//  first question. Both are impossible if it is compiled in. The
//  firmware keeps a compiled-in default (PERSONA_* in config.h) so
//  the robot still has a personality if the app never sends one - a
//  robot that answers in WALL-E's voice is better than a robot that
//  answers in nobody's.
//
//  TWO THINGS HAPPEN TO A REPLY
//  ---------------------------
//  1. `p` replaces GEMINI_SYSTEM_PROMPT as the system instruction, so
//     every answer is generated in that voice.
//  2. `s` is appended to the answer - but only if Gemini did not
//     already end with it. A child asking the same question twice
//     should not get "Friend! Friend!", and Gemini is explicitly told
//     to add it, so it usually does.
//
//  ALL OR NOTHING
//  --------------
//  A persona is applied completely or refused completely. A payload
//  missing a key, or with an oversized field, is rejected with
//  WALLE_ERR_BAD_ARG and the previous persona is left completely
//  untouched. Partially applying one would produce a robot whose
//  voice and whose name disagree, which is impossible to debug from
//  the outside.
//
//  MEMORY
//  ------
//  Fixed buffers, no heap: about 300 bytes of .bss total. The payload
//  is at most WALLE_TEXT_MAX bytes, so the largest field is bounded by
//  construction.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "walle_protocol.h"

class Persona {
public:
    // Loads the compiled-in default from config.h. Called at boot so
    // the robot is never voiceless.
    void begin();

    // Parses and stores a WALLE_OP_PERSONA payload.
    // Returns false and leaves the current persona UNCHANGED when the
    // payload is malformed, incomplete or has an oversized field.
    bool apply(const char* json);

    // ---- what the rest of the firmware reads ----

    // The Gemini system instruction. Never empty.
    const char* prompt() const { return _prompt; }

    // Appended to a reply before it is spoken. May be empty.
    const char* suffix() const { return _suffix; }

    // The robot's name. Never empty.
    const char* name() const { return _name; }

    // Mood, purely informational - shown in logs.
    const char* mood() const { return _mood; }

    // True when a persona arrived from the app, rather than being the
    // compiled-in default.
    bool fromApp() const { return _fromApp; }

    // Appends the suffix to `reply` unless it is already there.
    // Case-insensitive, and tolerates trailing whitespace, because
    // Gemini puts a full stop after the prompt's example suffix.
    void decorate(String* reply) const;

private:
    // Pulls one single-letter key out of the compact JSON. `allowEmpty`
    // is true for the two descriptive fields: a robot with no suffix and
    // no mood is perfectly valid, but a robot with no NAME and no
    // PROMPT has nothing to say, and is refused.
    bool field(const char* json, char key, char* out, size_t maxLen,
               bool allowEmpty);

    char _name[PERSONA_NAME_MAX]   = {0};
    char _suffix[PERSONA_SUFFIX_MAX] = {0};
    char _mood[PERSONA_MOOD_MAX]   = {0};
    char _prompt[PERSONA_PROMPT_MAX] = {0};
    bool _fromApp = false;
};

extern Persona persona;