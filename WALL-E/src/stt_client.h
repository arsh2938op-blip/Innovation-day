// ============================================================
//  Speech-to-Text client
// ------------------------------------------------------------
//  Default: Google Cloud STT v1 (synchronous recognise), taking
//  16 kHz / 16-bit / mono base64 LINEAR16 audio in the JSON body.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class SttClient {
public:
    bool ready() const { return STT_ENABLED && strlen(WALLE_STT_API_KEY) > 0; }

    // Blocking (bounded by STT_TIMEOUT_MS). Returns the transcript in
    // *outText. Returns false on any failure - never throws, never loops.
    bool transcribe(const uint8_t* pcm, size_t bytes, String* outText);
};
