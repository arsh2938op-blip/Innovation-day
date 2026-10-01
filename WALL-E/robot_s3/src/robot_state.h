// ============================================================
//  Robot state machine (the "brain" of the behaviour layer)
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

// There is no LISTENING state, and there never will be on the robot:
// it has no microphone. Speech recognition lives in the companion app,
// which sends the words over as text. STATE_THINKING is where Gemini
// runs, STATE_SPEAKING where the answer comes out of the amplifier.
enum RobotState {
    STATE_BOOT,
    STATE_IDLE,
    STATE_THINKING,
    STATE_SPEAKING,        // Gemini TTS audio is playing out of the speaker
    STATE_EXPLORING,
    STATE_OBSERVING,
    STATE_MOVING,
    STATE_DANCING,
    // A controller owns the wheels. This was STATE_REMOTE_MANUAL back
    // when there was a handheld radio remote; the app is now the only
    // controller. The VALUE 8 is part of the wire protocol and must
    // not move - the app still reads 8 as "somebody is driving me".
    STATE_MANUAL,
    STATE_OFFLINE,
    STATE_COUNT
};

class RobotStateMachine {
public:
    void begin();

    // Must be called on every loop iteration.
    // Fires onExit() of the old state, runs enter() of the new one,
    // applies the state minimum dwell time and stops the motors safely.
    void update(uint32_t now);

    void request(RobotState next);
    bool requestIfIdle(RobotState next);

    RobotState state() const { return _state; }
    RobotState previous() const { return _previous; }
    static const char* nameOf(RobotState s);
    uint32_t timeInState(uint32_t now) const { return now - _enteredMs; }

private:
    RobotState _state = STATE_BOOT;
    RobotState _previous = STATE_BOOT;
    RobotState _pending = STATE_BOOT;
    bool  _pendingValid = false;
    uint32_t _enteredMs = 0;
    uint32_t _dwellUntilMs = 0;

    void enter(RobotState s, uint32_t now);
    static uint32_t minDwell(RobotState s);
};

// Pointer to the single instance, so the serial console can print the state
// without holding a reference to it.
extern RobotStateMachine* gRobotSM;
