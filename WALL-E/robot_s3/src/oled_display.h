// ============================================================
//  OLED display + WALL-E's face / expression system
// ============================================================
#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// ---------------- expressions ----------------
enum Expression {
    // EXPR_LISTENING was removed with STT: the robot has no microphone.
    // EXPR_SPEAKING is BACK, because TTS is back. The companion app
    // already knows the "speaking" face by name.
    EXPR_BOOT,
    EXPR_IDLE,
    EXPR_THINKING,
    EXPR_SPEAKING,
    EXPR_HAPPY,
    EXPR_CONFUSED,
    EXPR_SURPRISED,
    EXPR_ANGRY,      // also used for "funny" reactions
    EXPR_DANCING,
    EXPR_EXPLORING,
    EXPR_OFFLINE,
    EXPR_ERROR,
    EXPR_SLEEPING,
    EXPR_COUNT
};

class OledDisplay {
public:
    bool begin();                    // false if the panel is not wired / not found
    bool ready() const { return _ready; }

    // ---- expression API ----
    void setExpression(Expression e);
    Expression current() const { return _expr; }
    static const char* nameOf(Expression e);

    // Short status line under the face (e.g. "OK", "OFFLINE", "HI!")
    void setStatus(const char* text);
    // Transient overlay text, e.g. the transcript. Pass nullptr to clear.
    void setCaption(const char* text);

    void splash(const char* line1, const char* line2);
    void update(uint32_t now);       // redraws at OLED_FRAME_MS; call from loop()

private:
    Adafruit_SSD1306* _dpy = nullptr;
    bool _ready = false;
    Expression _expr = EXPR_BOOT;
    uint32_t _lastFrameMs = 0;
    uint32_t _lastBlinkMs = 0;
    uint32_t _animPhaseMs = 0;
    bool _blink = false;
    char _status[16] = {0};
    char _caption[22] = {0};   // fits 128 px at text size 1

    void drawFace(Expression e, uint32_t now);
    void drawEyes(Expression e, int cx, int cy, bool blink);
    void drawMouth(Expression e, int cx, int cy);
    void drawStatus();
    void drawCaption();
};

extern OledDisplay oled;
