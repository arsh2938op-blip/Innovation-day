// ============================================================
//  Motor controller - 4x DC motors through a motor DRIVER MODULE
//  ------------------------------------------------------------
//  The ESP32-S3 never touches motor current. Every pin below goes to
//  an H-bridge driver INPUT (2 x TB6612FNG is the reference design:
//  4 channels, one per wheel). See config.h section 4 for why four
//  independent channels are required and which drivers do not work.
//
//  Per wheel the driver takes:
//     1x PWM   - speed        (LEDC channel, MOTOR_PWM_FREQ)
//     2x DIR   - direction    (IN1 / IN2, "coast" = both LOW)
//  plus a shared enable/standby pin per driver board.
//
//  Motor index order, used everywhere in this firmware:
//     0 front-left   1 back-left   2 front-right   3 back-right
//
//  The side sign (_side) exists because the left and right wheels are
//  mirrored on the chassis: to drive FORWARD the left wheels and the
//  right wheels have to be driven in opposite electrical directions.
//  Getting this wrong is the single most common reason a 4-wheel
//  robot spins on the spot instead of moving, so test it with
//  MotorController::setMotor() on one wheel at a time.
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
    void setDriverEnabled(bool on);                // toggles every STBY/EN pin
    bool driverEnabled() const { return _driverEnabled; }
    void update();                                 // call from loop() to advance the ramp

    // ---- driver module identity (logs + test menu) ----
    const char* driverName() const { return MOTOR_DRIVER_NAME; }
    uint8_t channels() const { return _channels; }

private:
    void applyAll(int16_t l1, int16_t l2, int16_t r1, int16_t r2);
    void buildPinTable();

    struct Pins { int8_t pwm, in1, in2; };
    Pins _pins[4] = {};
    uint8_t _channels = 0;

    // left motors are 0,1   ->  sign flip for correct "forward" on mirrored chassis
    static const int8_t _side[4];   // +1 right, -1 left

    int16_t _target[4] = {0, 0, 0, 0};
    int16_t _current[4] = {0, 0, 0, 0};
    uint32_t _lastRampMs = 0;
    bool _configured = false;
    bool _driverEnabled = false;

    void writePins(uint8_t index, int16_t speed);
    void rampStep();
};

extern MotorController motors;