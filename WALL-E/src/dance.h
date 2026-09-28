// ============================================================
//  Dance mode - a small, fully configurable move sequence
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "oled_display.h"   // for Expression

class Dance {
public:
    void begin();

    // Starts a dance. loops == 0 means "until stopDance()".
    void start(uint8_t loops = DANCE_LOOPS);
    void stop();
    bool running() const { return _running; }

    // Non-blocking driver, call from loop() while in STATE_DANCING.
    void update(uint32_t now);

    struct Step {
        int16_t l1, l2, r1, r2;   // per-motor command, -255..255
        uint16_t durationMs;
        Expression face;
    };

private:
    bool _running = false;
    uint8_t _index = 0;
    uint8_t _loop = 0;
    uint8_t _loops = DANCE_LOOPS;
    uint32_t _stepEndMs = 0;
    bool _paused = false;
    uint8_t _pauseFrames = 0;

    void runStep(uint32_t now);
    void setPause(bool on);
};

extern Dance dance;
