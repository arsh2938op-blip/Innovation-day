// ============================================================
//  Motor controller - 4x DC motors through a PWM+direction driver
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class MotorController {
public:
    void begin();

    // ---- named motions (all of these ramp softly) ----
    void forward(uint8_t speed = SPEED_WALK);
    void backward(uint8_t speed = SPEED_SLOW);
    void turnLeft(uint8_t speed = SPEED_TURN);    // pivot on the spot
    void turnRight(uint8_t speed = SPEED_TURN);
    void left(uint8_t speed = SPEED_SLOW);        // arc / strafing-ish left
    void right(uint8_t speed = SPEED_SLOW);
    void stop();
    void coast();                                 // PWM 0, direction pins released

    // ---- raw per-motor control (used by dance + tests) ----
    void setMotor(uint8_t index, int16_t speed);  // -255..255, 0 = stop

    // ---- safety ----
    bool motorsConfigured() const { return _configured; }
    void emergencyStop();                          // non-ramping, used on errors
    void setDriverEnabled(bool on);                // toggles the driver STBY/EN pin
    void update();                                 // call from loop() to advance the ramp

private:
    void applyAll(int16_t l1, int16_t l2, int16_t r1, int16_t r2);

    struct Pins { int8_t pwm, in1, in2; };
    Pins _pins[MOTOR_COUNT] = {
        {MOTOR_L1_PIN, MOTOR_L1_IN1, MOTOR_L1_IN2},   // 0 front-left
        {MOTOR_L2_PIN, MOTOR_L2_IN1, MOTOR_L2_IN2},   // 1 back-left
        {MOTOR_R1_PIN, MOTOR_R1_IN1, MOTOR_R1_IN2},   // 2 front-right
        {MOTOR_R2_PIN, MOTOR_R2_IN1, MOTOR_R2_IN2},   // 3 back-right
    };
    // left motors are 0,1   ->  sign flip for correct "forward" on mirrored chassis
    static const int8_t _side[MOTOR_COUNT];   // +1 right, -1 left

    int16_t _target[MOTOR_COUNT] = {0, 0, 0, 0};
    int16_t _current[MOTOR_COUNT] = {0, 0, 0, 0};
    uint32_t _lastRampMs = 0;
    bool _configured = false;
    bool _driverEnabled = false;

    void writePins(uint8_t index, int16_t speed);
    void rampStep();
};

extern MotorController motors;
