// ============================================================
//  Maneuver implementation
//  See maneuver.h for why these are timed rather than measured.
// ============================================================
#include "maneuver.h"
#include "log.h"
#include "motor_controller.h"
#include "safety.h"

static const char* TAG = "MOTOR";

Maneuver maneuver;

namespace {

// Reversing is slower than driving forward on a geared TT chassis,
// and a back-away is the one maneuver you never want to be slow,
// so it gets a slightly different speed rather than its own constant.
const uint8_t kRetreatSpeed = (MANEUVER_SPEED > 60) ? (uint8_t)(MANEUVER_SPEED - 40)
                                                    : MANEUVER_SPEED;

uint32_t clampMs(uint32_t ms) {
    if (ms < 60)   ms = 60;                      // below this nothing moves
    if (ms > MANEUVER_MAX_MS) ms = MANEUVER_MAX_MS;
    return ms;
}

}  // namespace

const char* Maneuver::nameOf(ManeuverKind k) {
    switch (k) {
        case MAN_FORWARD:     return "forward";
        case MAN_BACKWARD:    return "backward";
        case MAN_TURN_LEFT:   return "turn left";
        case MAN_TURN_RIGHT:  return "turn right";
        case MAN_TURN_AROUND: return "turn around";
        case MAN_RETREAT:     return "retreat";
        case MAN_NONE:        return "none";
        default:              return "?";
    }
}

void Maneuver::begin() {
    _kind = MAN_NONE;
    _durationMs = 0;
    _blockedLogged = false;
}

// ------------------------------------------------------------
//  Geometry
// ------------------------------------------------------------
uint32_t Maneuver::msForDistanceCm(uint16_t cm, uint8_t speed) {
    if (cm == 0 || MANEUVER_CM_PER_SECOND <= 0) return 0;

    // Walk backwards from "steps" to a millisecond duration, then apply
    // the calibration trim. Integer math keeps this exact and cheap.
    uint32_t ms = ((uint32_t)cm * 1000UL) / (uint32_t)MANEUVER_CM_PER_SECOND;
    ms = (uint32_t)((float)ms * MANEUVER_SPEED_MULTIPLIER);

    // A different speed needs a different amount of time for the same
    // distance. MANEUVER_CM_PER_SECOND was measured at MANEUVER_SPEED.
    if (speed != 0 && MANEUVER_SPEED != 0 && speed != MANEUVER_SPEED) {
        ms = (ms * MANEUVER_SPEED) / speed;
    }
    return clampMs(ms);
}

uint32_t Maneuver::msForDegrees(uint16_t degrees) {
    if (degrees == 0) return 0;
    if (degrees > 360) degrees = 360;

    uint32_t ms = ((uint32_t)degrees * (uint32_t)MANEUVER_MS_PER_TURN_360) / 360UL;
    ms = (uint32_t)((float)ms * MANEUVER_SPEED_MULTIPLIER);
    return clampMs(ms);
}

// ------------------------------------------------------------
//  Launching
// ------------------------------------------------------------
bool Maneuver::launch(ManeuverKind kind, uint32_t ms, const char* what) {
    if (ms == 0) {
        LOGW(TAG, "%s: zero duration, refusing", what);
        return false;
    }

    // One motion at a time. Starting a new one replaces the old, and
    // the old one's deadline is dropped: whatever was asked for last
    // is what runs.
    if (_kind != MAN_NONE && _kind != kind) {
        LOGI(TAG, "%s replaces %s", what, nameOf(_kind));
        motors.stop();
    }

    _kind = kind;
    _durationMs = ms;
    _startedMs = millis();
    _blockedLogged = false;

    LOGI(TAG, "Maneuver: %s (%s, %u ms)", nameOf(kind), what, (unsigned)ms);

#if MANEUVER_CALIBRATION
    LOGI(TAG, "CALIBRATE: expect %.1f cm over this maneuver", 
         ((float)_durationMs / 1000.0f) * MANEUVER_CM_PER_SECOND *
         (1.0f / MANEUVER_SPEED_MULTIPLIER));
#endif

    applyMotion();
    return true;
}

bool Maneuver::startForward(uint32_t ms) {
    return launch(MAN_FORWARD, clampMs(ms), "timed");
}
bool Maneuver::startBackward(uint32_t ms) {
    return launch(MAN_BACKWARD, clampMs(ms), "timed");
}
bool Maneuver::startTurnLeft(uint32_t ms) {
    return launch(MAN_TURN_LEFT, clampMs(ms), "timed");
}
bool Maneuver::startTurnRight(uint32_t ms) {
    return launch(MAN_TURN_RIGHT, clampMs(ms), "timed");
}

bool Maneuver::startForwardSteps(uint16_t steps) {
    if (steps == 0) { LOGW(TAG, "0 steps requested, refusing"); return false; }
    if (steps > WALLE_MAX_STEPS) {
        LOGW(TAG, "%u steps clamped to %u", steps, WALLE_MAX_STEPS);
        steps = WALLE_MAX_STEPS;
    }
    _speed = MANEUVER_SPEED;
    const uint32_t ms = msForDistanceCm(
        (uint16_t)(steps * STEP_DISTANCE_CM), MANEUVER_SPEED);
    return launch(MAN_FORWARD, ms, "steps");
}

bool Maneuver::startRetreatSteps(uint16_t steps) {
    if (steps == 0) { LOGW(TAG, "0 steps requested, refusing"); return false; }
    if (steps > WALLE_MAX_STEPS) steps = WALLE_MAX_STEPS;
    _speed = kRetreatSpeed;
    const uint32_t ms = msForDistanceCm(
        (uint16_t)(steps * STEP_DISTANCE_CM), kRetreatSpeed);
    return launch(MAN_RETREAT, ms, "back-away");
}

bool Maneuver::startTurnDegrees(uint16_t degrees) {
    if (degrees == 0) { LOGW(TAG, "0 degrees requested, refusing"); return false; }
    if (degrees > 360) degrees = 360;
    _speed = SPEED_TURN;
    return launch(degrees == 180 ? MAN_TURN_AROUND : MAN_TURN_LEFT,
                  msForDegrees(degrees), "degrees");
}

bool Maneuver::startTurnAround() {
    _speed = SPEED_TURN;
    return launch(MAN_TURN_AROUND, msForDegrees(180), "180 deg");
}

// ------------------------------------------------------------
//  Motion
// ------------------------------------------------------------
void Maneuver::applyMotion() {
    const uint8_t s = safety.speedFor(_speed);
    switch (_kind) {
        case MAN_FORWARD:     motors.forward(s);           break;
        case MAN_BACKWARD:    motors.backward(s);          break;
        case MAN_TURN_LEFT:   motors.turnLeft(SPEED_TURN);  break;
        case MAN_TURN_RIGHT:  motors.turnRight(SPEED_TURN); break;
        case MAN_TURN_AROUND: motors.turnLeft(SPEED_TURN);  break;  // 180 either way
        case MAN_RETREAT:     motors.backward(s);          break;
        default: break;
    }
}

void Maneuver::update(uint32_t now) {
    if (_kind == MAN_NONE) return;

    // ---- safety first, on every single tick ----
    if (!safety.wheelsAllowed()) {
        if (!_blockedLogged) {
            _blockedLogged = true;
            LOGW(TAG, "Maneuver %s truncated: %s",
                 nameOf(_kind), SafetyGuard::reasonName(safety.blockReason()));
        }
        motors.stop();
        _kind = MAN_NONE;
        _durationMs = 0;
        return;
    }
    _blockedLogged = false;

    if (now - _startedMs >= _durationMs) {
        LOGI(TAG, "Maneuver %s done", nameOf(_kind));
        motors.stop();
        _kind = MAN_NONE;
        _durationMs = 0;
        return;
    }

    // Re-assert the motion. This is what makes the speed policy
    // responsive: if the sensor drops us into caution mode mid-move,
    // the very next tick is slower, with no special case.
    applyMotion();
}

void Maneuver::stop() {
    if (_kind == MAN_NONE) return;
    LOGI(TAG, "Maneuver %s stopped", nameOf(_kind));
    motors.stop();
    _kind = MAN_NONE;
    _durationMs = 0;
}

void Maneuver::cancel(const char* why) {
    if (_kind == MAN_NONE) return;
    LOGW(TAG, "Maneuver %s cancelled (%s)", nameOf(_kind), why ? why : "no reason");
    motors.stop();
    _kind = MAN_NONE;
    _durationMs = 0;
}

uint32_t Maneuver::remainingMs(uint32_t now) const {
    if (_kind == MAN_NONE) return 0;
    const uint32_t done = now - _startedMs;
    return done >= _durationMs ? 0 : (_durationMs - done);
}