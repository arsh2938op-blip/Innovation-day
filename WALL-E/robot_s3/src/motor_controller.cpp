// ============================================================
//  Motor controller implementation
//  See motor_controller.h for the driver-module contract.
// ============================================================
#include "motor_controller.h"
#include "log.h"

static const char* TAG = "MOTOR";

MotorController motors;

// The left and right wheels are mirrored on the chassis, so driving
// them "the same way" in electrical terms drives them in opposite
// directions. This array is the fix, and its order must match the
// pin table in config.h section 4 and buildPinTable() below.
const int8_t MotorController::_side[4] = {-1, -1, +1, +1};

namespace {
// The level that puts a driver board into standby / enable.
inline uint8_t enableLevel(bool on) {
#if MOTOR_ENABLE_ACTIVE_HIGH
    return on ? HIGH : LOW;
#else
    return on ? LOW : HIGH;
#endif
}
}  // namespace

// ------------------------------------------------------------
//  Which physical pins drive which logical wheel.
//
//  4-channel chassis (the normal case):
//     0 front-left, 1 back-left, 2 front-right, 3 back-right
//  2-channel chassis (a 2-channel driver such as one L298N):
//     0 left, 1 right  -> skid steer, so "turn" becomes "spin"
//
//  Building this from MOTOR_COUNT rather than hard-coding it is what
//  lets someone with a single L298N still boot a working robot.
// ------------------------------------------------------------
void MotorController::buildPinTable() {
    if (MOTOR_COUNT >= 4) {
        _pins[0] = {MOTOR_L1_PIN, MOTOR_L1_IN1, MOTOR_L1_IN2};   // front-left
        _pins[1] = {MOTOR_L2_PIN, MOTOR_L2_IN1, MOTOR_L2_IN2};   // back-left
        _pins[2] = {MOTOR_R1_PIN, MOTOR_R1_IN1, MOTOR_R1_IN2};   // front-right
        _pins[3] = {MOTOR_R2_PIN, MOTOR_R2_IN1, MOTOR_R2_IN2};   // back-right
        _channels = 4;
    } else if (MOTOR_COUNT == 3) {
        // Unusual, but a 3-wheel layout exists: treat it as one left
        // plus two right.
        _pins[0] = {MOTOR_L1_PIN, MOTOR_L1_IN1, MOTOR_L1_IN2};
        _pins[1] = {MOTOR_R1_PIN, MOTOR_R1_IN1, MOTOR_R1_IN2};
        _pins[2] = {MOTOR_R2_PIN, MOTOR_R2_IN1, MOTOR_R2_IN2};
        _channels = 3;
    } else {
        _pins[0] = {MOTOR_L1_PIN, MOTOR_L1_IN1, MOTOR_L1_IN2};   // left
        _pins[1] = {MOTOR_R1_PIN, MOTOR_R1_IN1, MOTOR_R1_IN2};   // right
        _channels = 2;
    }
}

void MotorController::begin() {
#if !WALLE_ENABLE_MOTORS
    _configured = false;
    LOGW(TAG, "Disabled in config.h (WALLE_ENABLE_MOTORS=0)");
    return;
#else
    buildPinTable();

    _configured = true;
    for (uint8_t i = 0; i < _channels; i++) {
        const Pins& p = _pins[i];
        if (PIN_IS_UNSET(p.pwm) || PIN_IS_UNSET(p.in1) || PIN_IS_UNSET(p.in2)) {
            _configured = false;
            LOGE(TAG, "Pins for motor %u not configured in include/config.h", i);
        } else {
            pinMode(p.in1, OUTPUT);
            pinMode(p.in2, OUTPUT);
            // Start with the direction pins at rest BEFORE the PWM can
            // produce anything, so a driver never sees a floating
            // direction input and lurches on boot.
            digitalWrite(p.in1, LOW);
            digitalWrite(p.in2, LOW);
            ledcSetup(i, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
            ledcAttachPin(p.pwm, i);
            ledcWrite(i, 0);
        }
    }

    // Every standby line the driver(s) have. Two boards = two pins,
    // and forgetting the second one is why "one side of the robot does
    // not work" is such a common bug.
    const int8_t enablePins[2] = {MOTOR_ENABLE_PIN, MOTOR_ENABLE2_PIN};
    for (uint8_t i = 0; i < 2; i++) {
        if (PIN_IS_UNSET(enablePins[i])) continue;
        pinMode(enablePins[i], OUTPUT);
        digitalWrite(enablePins[i], enableLevel(false));   // disabled = safe
    }

    emergencyStop();

    if (_configured) {
        LOGI(TAG, "Driver: %s", MOTOR_DRIVER_NAME);
        LOGI(TAG, "Ready (%u channels, %d Hz PWM, enable %s)",
             _channels, MOTOR_PWM_FREQ,
             PIN_IS_UNSET(MOTOR_ENABLE_PIN) ? "none wired" : "wired");
        if (_channels < 4) {
            LOGW(TAG, "Fewer than 4 channels: steering is skid-steer only");
        }
    } else {
        LOGW(TAG, "Disabled - set the motor pins in include/config.h section 4");
    }
#endif
}

void MotorController::setDriverEnabled(bool on) {
    const uint8_t level = enableLevel(on);
    const int8_t pins[2] = {MOTOR_ENABLE_PIN, MOTOR_ENABLE2_PIN};

    bool any = false;
    for (uint8_t i = 0; i < 2; i++) {
        if (PIN_IS_UNSET(pins[i])) continue;
        digitalWrite(pins[i], level);
        any = true;
    }

    _driverEnabled = on;
    if (any && !on) emergencyStop();
}

void MotorController::writePins(uint8_t index, int16_t speed) {
    if (index >= _channels) return;
    const Pins& p = _pins[index];
    if (PIN_IS_UNSET(p.pwm)) return;

    // apply the chassis side sign and clamp
    int16_t v = (int16_t)(speed * _side[index]);
    if (v >  255) v =  255;
    if (v < -255) v = -255;

    if (v == 0) {
        // Both direction inputs LOW is "coast" on every common driver
        // (TB6612, DRV8833, L9110). It is also the safest state: the
        // H-bridge is off, so nothing is pushed into the motor.
        ledcWrite(index, 0);
        digitalWrite(p.in1, LOW);
        digitalWrite(p.in2, LOW);
        return;
    }
    if (v > 0) {
        digitalWrite(p.in1, HIGH);
        digitalWrite(p.in2, LOW);
    } else {
        digitalWrite(p.in1, LOW);
        digitalWrite(p.in2, HIGH);
    }
    ledcWrite(index, (uint8_t)(v > 0 ? v : -v));
}

void MotorController::rampStep() {
    const uint32_t now = millis();
    if (now - _lastRampMs < MOTOR_RAMP_DELAY_MS) return;
    _lastRampMs = now;

    for (uint8_t i = 0; i < _channels; i++) {
        int16_t t = _target[i], c = _current[i];
        if (c == t) continue;
        int16_t step = (t > c) ? MOTOR_RAMP_STEPS : -MOTOR_RAMP_STEPS;
        c += step;
        if ((step > 0 && c > t) || (step < 0 && c < t)) c = t;
        _current[i] = c;
        writePins(i, c);
    }
}

void MotorController::applyAll(int16_t l1, int16_t l2, int16_t r1, int16_t r2) {
    if (!_configured) return;
    if (!_driverEnabled) setDriverEnabled(true);
    _target[0] = l1; _target[1] = l2; _target[2] = r1; _target[3] = r2;
    _lastRampMs = millis() - MOTOR_RAMP_DELAY_MS;   // allow immediate first step
    rampStep();
}

void MotorController::forward(uint8_t s)  { applyAll( s,  s,  s,  s); }
void MotorController::backward(uint8_t s) { applyAll(-s, -s, -s, -s); }
void MotorController::turnLeft(uint8_t s){ applyAll(-s, -s,  s,  s); }
void MotorController::turnRight(uint8_t s){applyAll( s,  s, -s, -s); }
void MotorController::left(uint8_t s)     { applyAll(-s/2, -s/2,  s,  s); }
void MotorController::right(uint8_t s)    { applyAll( s,  s, -s/2, -s/2); }

void MotorController::stop() {
    for (uint8_t i = 0; i < _channels; i++) _target[i] = 0;
    rampStep();
    for (uint8_t i = 0; i < _channels; i++) {
        _current[i] = 0;
        writePins(i, 0);
    }
}

void MotorController::coast() { stop(); }

void MotorController::emergencyStop() {
    for (uint8_t i = 0; i < _channels; i++) {
        _target[i] = 0;
        _current[i] = 0;
        writePins(i, 0);
    }
}

void MotorController::update() { rampStep(); }

void MotorController::setMotor(uint8_t index, int16_t speed) {
    if (!_configured || index >= _channels) return;
    if (!_driverEnabled) setDriverEnabled(true);
    _target[index] = speed;
    _current[index] = speed;
    writePins(index, speed);
}