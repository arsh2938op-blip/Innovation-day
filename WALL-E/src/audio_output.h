// ============================================================
//  Speaker output (I2S -> amplifier -> 8 ohm speaker)
//  Playback is non-blocking: queue() copies the PCM in, pump()
//  streams it out a chunk at a time so the main loop keeps running.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class AudioOutput {
public:
    bool begin();
    bool ready() const { return _ready; }

    // Takes ownership of a heap PCM buffer obtained from TtsClient.
    bool queue(uint8_t* pcm, size_t bytes);

    // Call from loop(). Streams the queued audio out.
    void pump();

    bool busy() const { return _playing; }
    float progress() const { return _total ? (float)_sent / (float)_total : 0.0f; }
    bool finished() const { return _playing && _prepared >= _total && _sent >= _total; }

    void stop();
    void setVolume(uint8_t v);

private:
    bool _ready = false;
    bool _playing = false;
    uint8_t* _pcm = nullptr;
    size_t _total = 0;
    size_t _prepared = 0;   // bytes already gain-scaled (write cursor)
    size_t _sent = 0;       // bytes actually handed to the I2S driver (read cursor)
    size_t _chunk = 0;
    uint32_t _startMs = 0;
    uint8_t _vol = SPK_VOLUME;

    void freeBuffer();
};

extern AudioOutput speaker;
