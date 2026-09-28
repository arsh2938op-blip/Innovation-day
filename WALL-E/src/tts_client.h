// ============================================================
//  Text-to-Speech client
// ------------------------------------------------------------
//  Default: Google Cloud TTS v1 with LINEAR16 output, so the ESP32
//  needs no MP3 decoder (which would not fit comfortably on a C3).
//  If you switch provider, keep returning raw 16-bit mono PCM at
//  AUDIO_SAMPLE_RATE or the speaker will sound wrong.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class TtsClient {
public:
    bool ready() const { return strlen(WALLE_TTS_API_KEY) > 0; }

    // Fetches linear PCM. Blocking (bounded by TTS_TIMEOUT_MS).
    // Returns false and leaves outBytes at 0 on any failure.
    bool synthesize(const String& text, uint8_t** outPcm, size_t* outBytes);
    void freeAudio(uint8_t* pcm, size_t bytes);

    // Long Gemini answers are split into speakable chunks.
    static size_t splitText(const String& text, String* outChunks, size_t maxChunks);
};
