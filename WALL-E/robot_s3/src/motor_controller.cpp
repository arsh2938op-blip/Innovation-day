#include "motor_controller.h"
#include "log.h"

static const char* TAG = "MOTOR";

MotorController motors;

// left side motors need the opposite sign for "forward" on a mirrored chassis
const int8_t MotorController::_side[MOTOR_COUNT] = {-1, -1, +1, +1};

void MotorController::begin() {
#if !WALLE_ENABLE_MOTORS
    _configured = false;
    LOGW(TAG, "Disabled in config.h (WALLE_ENABLE_MOTORS=0)");
    return;
#else
    _configured = true;
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        const Pins& p = _pins[i];
        if (PIN_IS_UNSET(p.pwm) || PIN_IS_UNSET(p.in1) || PIN_IS_UNSET(p.in2)) {
            _configured = false;
            LOGE(TAG, "Pins for motor %u not configured in include/config.h", i);
        } else {
            pinMode(p.in1, OUTPUT);
            pinMode(p.in2, OUTPUT);
            ledcSetup(i, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
            ledcAttachPin(p.pwm, i);
        }
    }

    if (!PIN_IS_UNSET(MOTOR_ENABLE_PIN)) {
        pinMode(MOTOR_ENABLE_PIN, OUTPUT);
        digitalWrite(MOTOR_ENABLE_PIN, LOW);       // driver disabled = safe
    }

    emergencyStop();

    if (_configured) {
        LOGI(TAG, "Ready (%d motors, %d Hz PWM)", MOTOR_COUNT, MOTOR_PWM_FREQ);
    } else {
        LOGW(TAG, "Disabled - set motor pins in include/config.h");
    }
#endif
}

void MotorController::setDriverEnabled(bool on) {
    if (PIN_IS_UNSET(MOTOR_ENABLE_PIN)) return;
    digitalWrite(MOTOR_ENABLE_PIN, on ? HIGH : LOW);
    _driverEnabled = on;
    if (!on) emergencyStop();
}

void MotorController::writePins(uint8_t index, int16_t speed) {
    if (index >= MOTOR_COUNT) return;
    const Pins& p = _pins[index];
    if (PIN_IS_UNSET(p.pwm)) return;

    // apply the chassis side sign and clamp
    int16_t v = (int16_t)(speed * _side[index]);
    if (v >  255) v =  255;
    if (v < -255) v = -255;

    if (v == 0) {
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

    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
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
    if (!_driverEnabled && !PIN_IS_UNSET(MOTOR_ENABLE_PIN)) setDriverEnabled(true);
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
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) _target[i] = 0;
    rampStep();
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        _current[i] = 0;
        writePins(i, 0);
    }
}

void MotorController::coast() { stop(); }

void MotorController::emergencyStop() {
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        _target[i] = 0;
        _current[i] = 0;
        writePins(i, 0);
    }
}

void MotorController::update() { rampStep(); }

void MotorController::setMotor(uint8_t index, int16_t speed) {
    if (!_configured || index >= MOTOR_COUNT) return;
    if (!_driverEnabled && !PIN_IS_UNSET(MOTOR_ENABLE_PIN)) setDriverEnabled(true);
    _target[index] = speed;
    _current[index] = speed;
    writePins(index, speed);
}
