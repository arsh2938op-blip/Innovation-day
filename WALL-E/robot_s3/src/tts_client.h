// ============================================================
//  Text-to-Speech client — Google Gemini TTS
// ------------------------------------------------------------
//  Endpoint: .../models/gemini-2.5-flash-preview-tts:generateContent
//
//  The response looks like:
//    { "candidates": [ { "content": { "parts": [ {
//        "inlineData": {
//          "mimeType": "audio/L16;codec=pcm;rate=24000",
//          "data": "<a very long base64 string>"
//    } ] } } ] }
//
//  WHY THIS FILE IS NOT JUST "http.POST() + deserializeJson()"
//  -------------------------------------------------------
//  That naive version needs the whole base64 string in RAM, plus the
//  decoded PCM, plus the JSON document - over 1 MB for a 15 second
//  sentence. It cannot work on an ESP32.
//
//  So the response is READ IN CHUNKS and the base64 is decoded
//  incrementally straight into the speaker's ring buffer. Peak extra
//  RAM is one HTTP chunk. See src/audio_output.h for why.
//
//  Nothing here is Google-specific magic: AudioOutput only ever sees
//  raw 16-bit mono PCM at TTS_SAMPLE_RATE.
#pragma once

#include <Arduino.h>
#include "config.h"

class TtsClient {
public:
    bool ready() const;

    // Speaks `text` immediately. Blocking for the duration of the
    // request (bounded by TTS_TIMEOUT_MS) because the audio is being
    // played as it downloads. Returns false on any failure; the
    // caller is expected to carry on regardless.
    bool speak(const String& text);

    // Same, but chunks long text so one very long reply does not turn
    // into one very long, expensive request.
    bool speakLong(const String& text);

    // Long replies are split on sentence boundaries.
    static size_t splitText(const String& text, String* outChunks, size_t maxChunks);

private:
    // Pulls the base64 out of the streamed response and feeds PCM to
    // the speaker. Returns the number of decoded bytes, or -1 on a
    // transport error.
    long speakFromGemini(const String& text);
};