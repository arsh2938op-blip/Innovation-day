// ============================================================
//  Speaker output — I2S -> amplifier -> 8 ohm speaker
// ------------------------------------------------------------
//  WHY THIS IS A STREAM AND NOT A BUFFER
//  --------------------------------------
//  Gemini's TTS returns 24 kHz signed 16-bit PCM: 48 kB for every
//  second of speech. A 15 second answer is 720 kB of PCM and roughly
//  960 kB of base64 inside the HTTP response. Neither fits in an
//  ESP32's RAM, and the ESP32 Arduino HTTPClient in this IDF version
//  has no streaming-body callback to avoid buffering it.
//
//  So instead: TtsClient reads the response in small chunks, base64
//  -decodes them on the fly, and pushes the PCM in here. This class
//  owns a fixed ring buffer (SPK_RING_BYTES) and hands it to I2S in
//  real time. The peak RAM cost of speaking is the ring, not the
//  length of the sentence.
//
//  LOADED ONLY WHEN NEEDED
//  -----------------------
//  For most of its life WALL-E is silent, and the I2S driver, its DMA
//  buffers and a 24 kB ring buffer are the second largest RAM cost in
//  this firmware. With SPK_LAZY_LOAD the audio stack is brought up
//  the first time something actually needs to be spoken and torn down
//  again SPK_RELEASE_IDLE_MS after the last sentence finishes. While
//  it is torn down the amplifier enable pin is low, so the amp is not
//  even powered.
//
//  begin() therefore only VALIDATES the pins - it does not allocate
//  anything and does not touch I2S. That is what makes it safe to
//  call from setup() and forget about.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class AudioOutput {
public:
    // Validates the pins and nothing else. No allocation, no I2S.
    // Cheap to call and always safe to call.
    bool begin();

    // True when the pins are set correctly, i.e. speech is possible
    // if it is ever asked for. Says nothing about whether the audio
    // stack is currently loaded - see loaded().
    bool ready() const { return _ready; }

    // True while the I2S driver and ring buffer are actually up.
    bool loaded() const { return _loaded; }

    // ---- streaming API (used by TtsClient) ----

    // Opens a playback session at `sampleRate`, loading the audio
    // stack if it is not up. Re-programs the I2S clock when the rate
    // differs from the previous session.
    bool beginStream(uint32_t sampleRate);

    // Pushes decoded PCM. Self-pacing: it also drains the ring into
    // I2S, so it must be called from the streaming loop, not from
    // the main loop. May wait (a few ms) when the ring is full, which
    // is the correct back-pressure onto the network read.
    void feed(const uint8_t* pcm, size_t bytes);

    // Marks the stream complete. Any audio still in the ring keeps
    // playing; pump() finishes it.
    void endStream();

    // ---- main loop side ----

    // Drains the tail of a finished stream, then releases the audio
    // stack once it has been idle long enough. Call every loop().
    void pump();

    // True while audio is still waiting to be played out.
    bool busy() const { return _playing; }

    // How far through the current playback we are, 0.0 .. 1.0.
    float progress() const { return _total ? (float)_played / (float)_total : 0.0f; }

    // ---- convenience + test helpers ----

    // Queues a complete in-memory buffer (hardware test tone, etc).
    // Takes ownership of `pcm` and frees it.
    bool queue(uint8_t* pcm, size_t bytes);

    void stop();
    void setVolume(uint8_t v);
    uint8_t volume() const { return _vol; }

private:
    // Lazy load / release. Both are safe to call at any time.
    bool ensureHardware();
    void releaseHardware();

    void applyGain(uint8_t* dst, const uint8_t* src, size_t bytes);
    bool writeToI2s();
    void resetRing();

    bool _ready = false;      // pins are valid
    bool _loaded = false;     // I2S driver + ring are up
    bool _playing = false;
    bool _streaming = false;  // a feed() stream is still open

    uint8_t* _ring = nullptr;
    size_t _cap = 0;
    size_t _head = 0;             // read cursor  (I2S consumes here)
    size_t _tail = 0;             // write cursor (feed writes here)

    uint32_t _rate = TTS_SAMPLE_RATE;
    size_t _total = 0, _played = 0;   // whole-buffer accounting
    uint32_t _startMs = 0;
    uint32_t _lastActiveMs = 0;
    uint8_t _vol = SPK_VOLUME;

    size_t ringUsed() const { return _tail >= _head ? _tail - _head : _cap - _head + _tail; }
    size_t ringFree() const { return _cap - 1 - ringUsed(); }
};

extern AudioOutput speaker;