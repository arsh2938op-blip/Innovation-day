#include "robot_state.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "log.h"

static const char* TAG = "ROBOT";

RobotStateMachine* gRobotSM = nullptr;

// Minimum time a state is held before another one may be entered.
// Prevents the state machine from flickering between two states.
uint32_t RobotStateMachine::minDwell(RobotState s) {
    switch (s) {
        case STATE_THINKING:  return 200;
        // Speaking is long and blocking; never interrupt it mid-word
        // with a short dwell.
        case STATE_SPEAKING:  return 2000;
        case STATE_EXPLORING: return 500;
        case STATE_OBSERVING: return 300;
        case STATE_MOVING:    return 250;
        case STATE_DANCING:   return 500;
        // A held remote button re-sends continuously, so a long dwell
        // here would only add lag to the next button press.
        case STATE_REMOTE_MANUAL: return 0;
        default:              return 0;
    }
}

const char* RobotStateMachine::nameOf(RobotState s) {
    switch (s) {
        case STATE_BOOT:      return "BOOT";
        case STATE_IDLE:      return "IDLE";
        case STATE_THINKING:  return "THINKING";
        case STATE_SPEAKING:  return "SPEAKING";
        case STATE_REMOTE_MANUAL: return "REMOTE";
        case STATE_EXPLORING: return "EXPLORING";
        case STATE_OBSERVING: return "OBSERVING";
        case STATE_MOVING:    return "MOVING";
        case STATE_DANCING:   return "DANCING";
        case STATE_OFFLINE:   return "OFFLINE";
        default:              return "?";
    }
}

void RobotStateMachine::begin() {
    _state = STATE_BOOT;
    _previous = STATE_BOOT;
    _pendingValid = false;
    _enteredMs = millis();
    _dwellUntilMs = _enteredMs;
    oled.setExpression(EXPR_BOOT);
    LOGI(TAG, "State machine ready");
}

void RobotStateMachine::request(RobotState next) {
    if (next == _state) return;
    if (_pendingValid && _pending == next) return;
    _pending = next;
    _pendingValid = true;
}

bool RobotStateMachine::requestIfIdle(RobotState next) {
    if (_state != STATE_IDLE) return false;
    request(next);
    return true;
}

void RobotStateMachine::update(uint32_t now) {
    if (_pendingValid && now >= _dwellUntilMs) {
        _previous = _state;
        _state = _pending;
        _pendingValid = false;
        _enteredMs = now;
        _dwellUntilMs = now + minDwell(_state);
        enter(_state, now);
    }
}

void RobotStateMachine::enter(RobotState s, uint32_t now) {
    (void)now;
    LOGI(TAG, "%s", nameOf(s));

    // Safety: any state that does not explicitly drive the motors
    // must not leave them running. STATE_REMOTE_MANUAL is the one
    // exception - remote_link.cpp owns the motors there and stops
    // them itself on release, timeout or link loss.
    switch (s) {
        case STATE_MOVING:
        case STATE_DANCING:
        case STATE_EXPLORING:
        case STATE_REMOTE_MANUAL:
            break;                       // motion is owned elsewhere
        default:
            motors.stop();
            break;
    }

    switch (s) {
        case STATE_BOOT:      oled.setExpression(EXPR_BOOT);      break;
        case STATE_IDLE:      oled.setExpression(EXPR_IDLE);      break;
        case STATE_THINKING:  oled.setExpression(EXPR_THINKING);  break;
        case STATE_SPEAKING:  oled.setExpression(EXPR_SPEAKING);  break;
        case STATE_EXPLORING: oled.setExpression(EXPR_EXPLORING); break;
        case STATE_OBSERVING: oled.setExpression(EXPR_EXPLORING); break;
        case STATE_MOVING:    oled.setExpression(EXPR_HAPPY);     break;
        case STATE_DANCING:   oled.setExpression(EXPR_DANCING);   break;
        case STATE_REMOTE_MANUAL: oled.setExpression(EXPR_HAPPY); break;
        case STATE_OFFLINE:   oled.setExpression(EXPR_OFFLINE);   break;
        default: break;
    }
}
