// ============================================================
//  Microphone capture (I2S) + simple voice-activity detection
// ------------------------------------------------------------
//  Produces 16 kHz / 16-bit / mono LINEAR16 PCM, which is exactly
//  what the Speech-to-Text API expects.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class AudioInput {
public:
    bool begin();
    bool ready() const { return _ready; }

    // Free the capture buffer (call before begin() on the next take).
    void end();

    // ---- recording ----
    bool startRecording(uint16_t maxSeconds);
    bool isRecording() const { return _recording; }
    uint32_t recordedMs() const { return millis() - _recStartMs; }
    // Non-blocking: keeps filling the buffer, returns true when enough audio
    // has been captured (max length reached, or speech followed by silence).
    bool pollRecording();
    void abortRecording();

    const uint8_t* pcm() const { return _buffer; }
    size_t pcmLength() const { return _length; }

    // ---- level metering / VAD ----
    int  lastPeakDb() const { return _peakDb; }
    bool speechDetected() const { return _speechSeen; }

    // Blocking is only used inside the hardware test menu.
    void runMeterTest(uint16_t seconds);

private:
    bool _ready = false;
    bool _recording = false;
    bool _speechSeen = false;
    uint8_t* _buffer = nullptr;
    size_t _cap = 0, _length = 0;
    uint32_t _recStartMs = 0, _lastVoiceMs = 0;
    int _peakDb = -90;

    size_t readSamples(int16_t* dst, size_t maxSamples);
    void   freeBuffer();
};

extern AudioInput mic;
