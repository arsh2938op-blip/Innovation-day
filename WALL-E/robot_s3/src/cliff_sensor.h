// ============================================================
//  Cliff sensor - HC-SR04, mounted pointing DOWN and forward
//  ------------------------------------------------------------
//  WHAT IT IS FOR
//  WALL-E lives on a table. If it drives off the edge it falls over
//  and the demo is over. This sensor is the difference between a
//  robot that stops at the edge and a robot that ends up on the floor.
//
//  HOW IT WORKS - read include/config.h section 4b first
//  Mounted flat, pointing straight down, the HC-SR04 would only
//  report the height of the robot above the floor, which never
//  changes while driving. It would notice the edge far too late.
//
//  Tilted forward to SENSOR_MOUNT_ANGLE_DEG (45 in practice), the
//  beam lands on the ground AHEAD of the wheels:
//
//      *  (sensor)
//       \ 45 deg
//        \
//         \  d = slant range
//  --------o----------------- floor
//
//  On a flat floor d is constant. Over the edge there is no floor, so
//  d grows quickly. The firmware converts the slant reading into the
//  vertical distance (ground = slant * cos(angle)) and compares that
//  with SENSOR_NOMINAL_GROUND_CM. Bigger than nominal + trip margin
//  means there is nothing under the wheels any more: STOP.
//
//  DESIGN NOTES THAT MATTER
//  * ONE sample per interval, never a burst. Five blocking reads in
//    a row would stall the whole main loop for up to 60 ms, which is
//    exactly the kind of stall that makes a safety system useless.
//    Samples go into a small ring and the MEDIAN of the last valid
//    readings is used, because the HC-SR04 likes to spit out an
//    occasional wild value on a soft or angled surface.
//  * A missing echo is a FAULT, never "safe". An unplugged sensor
//    must not look like a clear road, so after SENSOR_FAULT_LIMIT
//    consecutive misses the state is WALLE_CLIFF_FAULT and the safety
//    guard stops the robot.
//  * Every reading is sanity-checked before it is believed.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "obstacle_detector.h"
#include "walle_protocol.h"

class CliffSensor : public ObstacleDetector {
public:
    bool begin();

    // Non-blocking. Call every loop(); it takes at most one echo
    // sample per SENSOR_INTERVAL_MS, and that one sample is bounded
    // by SENSOR_ECHO_TIMEOUT_US.
    void update(uint32_t now);

    // Takes one reading right now, ignoring the interval. Used by the
    // READ_SENSOR command and by the hardware test menu so the answer
    // is fresh rather than up to 70 ms old.
    bool pollNow();

    // ---- what the rest of the firmware asks ----

    // WalleCliffState, exactly as defined in shared/walle_protocol.h
    // so a controller can read it without including this firmware.
    uint8_t state() const { return _state; }
    static const char* nameOf(uint8_t s);

    // Safe to drive: floor found. WARN is still allowed, but slowly.
    bool safeToDrive() const {
        return _state == WALLE_CLIFF_GROUND || _state == WALLE_CLIFF_WARN;
    }
    // Hard stop: a drop, or a sensor that has stopped reporting.
    bool blocked() const {
        return _state == WALLE_CLIFF_DROP || _state == WALLE_CLIFF_FAULT;
    }

    // Vertical distance from the sensor to the floor, in cm.
    // 0 when there is no valid reading.
    uint16_t groundCm() const { return _groundCm; }
    // The raw diagonal echo distance, in cm. Shown by the test menu so
    // the mount angle can be checked against a tape measure.
    uint16_t slantCm() const { return _slantCm; }

    bool configured() const { return _configured; }
    bool enabled() const { return SENSOR_ENABLE != 0; }

    // ObstacleDetector: true when the sensor blocks movement.
    bool detect(float* proximityOut) override;
    const char* stateName() const override { return nameOf(_state); }

private:
    // One echo measurement. Returns false on timeout / no echo.
    bool sampleOnce(uint16_t* slantCmOut);

    // Applies the newest valid sample to the state machine.
    void classify(uint32_t now);

    // Throws away the sample history, e.g. after a fault recovery.
    void resetHistory();

    bool _configured = false;
    bool _armed = false;

    uint8_t  _state = WALLE_CLIFF_UNKNOWN;

    // Valid sample history, in cm. Not really a "ring": medianOf()
    // sorts it in place, so it is a multiset of the last SENSOR_SAMPLES
    // valid readings, which is exactly what a median needs.
    uint16_t _samples[SENSOR_SAMPLES] = {0};
    uint8_t  _sampleCount = 0;
    uint8_t  _sampleNext  = 0;
    uint8_t  _missStreak  = 0;

    uint16_t _slantCm = 0;
    uint16_t _groundCm = 0;

    // cos(SENSOR_MOUNT_ANGLE_DEG), computed once in begin().
    float _cosAngle = 0.7071f;

    uint32_t _nextPollMs = 0;

    // Set when the state last CHANGED, so the safety guard can fire
    // an event once instead of every loop.
    uint8_t  _lastReported = WALLE_CLIFF_UNKNOWN;
    uint32_t _stateChangedMs = 0;
};

extern CliffSensor cliffSensor;