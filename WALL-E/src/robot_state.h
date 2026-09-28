// ============================================================
//  Robot state machine (the "brain" of the behaviour layer)
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

enum RobotState {
    STATE_BOOT,
    STATE_IDLE,
    STATE_LISTENING,
    STATE_THINKING,
    STATE_SPEAKING,
    STATE_EXPLORING,
    STATE_OBSERVING,
    STATE_MOVING,
    STATE_DANCING,
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
