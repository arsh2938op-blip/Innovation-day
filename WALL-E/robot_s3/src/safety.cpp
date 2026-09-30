// ============================================================
//  Safety guard implementation
//  See safety.h for the rules.
// ============================================================
#include "safety.h"
#include "cliff_sensor.h"
#include "log.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "robot_state.h"
#include "dance.h"
#include "maneuver.h"

static const char* TAG = "SAFE";

SafetyGuard safety;

const char* SafetyGuard::reasonName(uint8_t r) {
    switch (r) {
        case WHEELS_OK:                return "ok";
        case WHEELS_BLOCKED_CLIFF:     return "cliff";
        case WHEELS_BLOCKED_SENSOR:    return "no sensor";
        case WHEELS_BLOCKED_VOICE:     return "speaking";
        case WHEELS_BLOCKED_COOLDOWN:  return "cooldown";
        default:                       return "?";
    }
}

void SafetyGuard::begin(CliffSensor* sensor) {
    _sensor = sensor;
    _block = WHEELS_OK;
    _voiceBusy = false;
    _external = false;
    _cooldownUntilMs = 0;
    _backingAway = false;
    _lastSensorState = sensor ? sensor->state() : WALLE_CLIFF_UNKNOWN;
    LOGI(TAG, "Guard ready (cliff %s, voice %s)",
         sensor && sensor->configured() ? "on" : "OFF",
         SAFETY_BLOCK_ON_VOICE ? "blocking" : "ignored");
}

// ------------------------------------------------------------
//  A sensor state CHANGED. This is the only place that reacts, so a
//  drop can be handled exactly once instead of on every loop.
// ------------------------------------------------------------
void SafetyGuard::onSensorStateChanged(uint8_t next, uint32_t now) {
    const bool wasDrop  = (_lastSensorState == WALLE_CLIFF_DROP);
    const bool wasFault = (_lastSensorState == WALLE_CLIFF_FAULT);
    _lastSensorState = next;

    // ---- a drop, or a sensor that went quiet: STOP NOW ----
    if (next == WALLE_CLIFF_DROP || next == WALLE_CLIFF_FAULT) {
        const char* why = (next == WALLE_CLIFF_DROP) ? "cliff" : "sensor fault";

        // emergencyStop(), not stop(): no ramp, no debounce, no state
        // transition in the way. The wheels are commanded to zero the
        // same call stack we noticed the drop on.
        motors.emergencyStop();

        // Nothing that was driving may keep driving.
        maneuver.cancel("cliff");
        dance.stop();

        _block = (next == WALLE_CLIFF_DROP) ? WHEELS_BLOCKED_CLIFF
                                            : WHEELS_BLOCKED_SENSOR;
        _cooldownUntilMs = now + SAFETY_COOLDOWN_MS;
        _backingAway = false;

        if (gRobotSM) gRobotSM->request(STATE_IDLE);
        oled.setExpression(EXPR_SURPRISED);
        oled.setStatus(why);

        LOGW(TAG, "%s -> STOP (%s)", TAG, why);
        LOGI(TAG, "Blocked for %u ms, then WALL-E will back away",
             (unsigned)SAFETY_COOLDOWN_MS);
        return;
    }

    // ---- back on the floor again ----
    if ((wasDrop || wasFault) && next != WALLE_CLIFF_UNKNOWN) {
        LOGI(TAG, "Floor again (%s) - backing away from the edge",
             CliffSensor::nameOf(next));
        oled.setStatus("backing off");
        startBackAway(now);
    }
}

// ------------------------------------------------------------
void SafetyGuard::startBackAway(uint32_t now) {
    (void)now;
    // While a remote or the app holds the wheels, the guard keeps its
    // hands off: the human is in charge and may be driving at a wall.
    if (_external) {
        LOGI(TAG, "External control active - not auto-recovering");
        _block = WHEELS_OK;
        return;
    }

    if (!maneuver.startRetreatSteps(SAFETY_BACKAWAY_STEPS)) {
        // Could not move (no manoeuvre room, or the guard blocked us):
        // stay put rather than creep towards the edge.
        LOGW(TAG, "Could not start the back-away, staying stopped");
        _block = WHEELS_BLOCKED_COOLDOWN;
        _cooldownUntilMs = now + SAFETY_COOLDOWN_MS;
        return;
    }

    _backingAway = true;
    _block = WHEELS_OK;   // the manoeuvre itself is the permitted motion
    oled.setExpression(EXPR_CONFUSED);
}

// ------------------------------------------------------------
void SafetyGuard::update(uint32_t now) {
    if (_sensor) {
        const uint8_t s = _sensor->state();
        if (s != _lastSensorState) onSensorStateChanged(s, now);
    }

    // ---- is a back-away still running? ----
    if (_backingAway) {
        if (maneuver.busy()) {
            // Keep the block clear for the duration, but only for the
            // manoeuvre we started. The cliff sensor itself is read on
            // every loop, so a drop during the back-away stops it too.
            if (_lastSensorState != WALLE_CLIFF_DROP &&
                _lastSensorState != WALLE_CLIFF_FAULT) {
                _block = WHEELS_OK;
            }
            return;
        }
        _backingAway = false;
        LOGI(TAG, "Backed away");
        oled.setExpression(EXPR_IDLE);
    }

    // ---- recompute the block from first principles ----
    if (_lastSensorState == WALLE_CLIFF_DROP) {
        _block = WHEELS_BLOCKED_CLIFF;
    } else if (_lastSensorState == WALLE_CLIFF_FAULT) {
        _block = WHEELS_BLOCKED_SENSOR;
    } else if ((int32_t)(now - _cooldownUntilMs) < 0) {
        _block = WHEELS_BLOCKED_COOLDOWN;
    } else if (_voiceBusy) {
        _block = WHEELS_BLOCKED_VOICE;
    } else if (_block == WHEELS_BLOCKED_COOLDOWN || _block == WHEELS_BLOCKED_CLIFF ||
               _block == WHEELS_BLOCKED_SENSOR) {
        // A transient block has expired: go back to normal.
        _block = WHEELS_OK;
    }
}

// ------------------------------------------------------------
uint8_t SafetyGuard::speedFor(uint8_t requested) const {
    if (requested == 0) return 0;

    // Edge-on-edge creep while the sensor is only WARNING.
    if (MANEUVER_SLOW_CAUTION && _sensor &&
        _sensor->state() == WALLE_CLIFF_WARN && requested > SENSOR_CAUTION_SPEED) {
        return SENSOR_CAUTION_SPEED;
    }
    // Coming out of a drop: do not immediately sprint away again.
    if (_backingAway && requested > SENSOR_CAUTION_SPEED) {
        return SENSOR_CAUTION_SPEED;
    }
    return requested;
}

void SafetyGuard::noteVoiceStart() {
    _voiceBusy = true;
    // Belt and braces: the caller (Behavior) also stops the motors, but
    // a voice event must never leave a wheel turning even if a caller
    // forgets.
    motors.stop();
    maneuver.cancel("talking");
    dance.stop();
    LOGI(TAG, "Wheels blocked: WALL-E is answering");
}

void SafetyGuard::noteVoiceEnd() {
    _voiceBusy = false;
    LOGI(TAG, "Voice done - wheels released");
}

void SafetyGuard::forceStop(const char* why, bool withCooldown) {
    motors.emergencyStop();
    maneuver.cancel(why);
    dance.stop();
    _external = false;
    _backingAway = false;

    // DELIBERATELY no permanent block here.
    //
    // A stop must leave the robot able to accept the NEXT command. An
    // earlier version set a "manually stopped" block that nothing
    // cleared, which meant one STOP - or one link timeout - killed the
    // robot for the rest of the session. Whether the wheels may move is
    // decided by the sensor, by the voice lock and by the cooldown,
    // and by nothing else.
    if (withCooldown) {
        _cooldownUntilMs = millis() + SAFETY_COOLDOWN_MS;
        _block = WHEELS_BLOCKED_COOLDOWN;
    }
    LOGI(TAG, "Stopped (%s)", why ? why : "no reason");
}

uint8_t SafetyGuard::sensorState() const {
    return _sensor ? _sensor->state() : WALLE_CLIFF_UNKNOWN;
}