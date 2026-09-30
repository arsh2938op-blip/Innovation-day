// ============================================================
//  Safety guard - the one authority on "may the wheels move?"
//  ------------------------------------------------------------
//  Everything that could move the robot asks this first: the command
//  dispatcher (remote, app, serial), the maneuver sequencer, the
//  dance and the autonomous behaviour. Keeping the decision in one
//  small file is what makes it answerable.
//
//  TWO THINGS BLOCK THE WHEELS
//  --------------------------
//  1. THE CLIFF SENSOR. A drop, or a sensor that has stopped
//     reporting at all. Nothing overrides this, and no command can
//     wave it through: it is the whole reason WALL-E stays on the
//     table.
//  2. THE ROBOT IS TALKING. While WALL-E is thinking (Gemini) or
//     speaking (TTS) the wheels are blocked. A robot that rolls
//     around mid-sentence cannot be heard and cannot be stopped by
//     the person listening to it.
//
//  WHAT HAPPENS ON A DROP
//  ----------------------
//    stop (emergency, not a ramp)  ->  back away from the edge
//    ->  wait out a cooldown  ->  carry on
//
//  The stop is immediate and unconditional: emergencyStop() bypasses
//  the soft-start ramp on purpose, because the point is to be
//  stopped NOW rather than decelerating over the edge.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "walle_protocol.h"

class CliffSensor;

enum WheelsBlockReason : uint8_t {
    WHEELS_OK = 0,
    WHEELS_BLOCKED_CLIFF,      // the sensor sees a drop
    WHEELS_BLOCKED_SENSOR,     // the sensor is not reporting
    WHEELS_BLOCKED_VOICE,      // Gemini is thinking or TTS is speaking
    WHEELS_BLOCKED_COOLDOWN,   // just backed away from a drop
};

class SafetyGuard {
public:
    void begin(CliffSensor* sensor);

    // Call FIRST in loop(), before anything that can drive.
    // Runs the cliff reaction. Never blocks longer than one echo
    // sample.
    void update(uint32_t now);

    // ---- the question every mover asks ----
    bool wheelsAllowed() const { return _block == WHEELS_OK; }
    uint8_t blockReason() const { return _block; }
    static const char* reasonName(uint8_t r);

    // Speed policy. Returns `requested` normally, but drops it to a
    // crawl while the sensor is only WARN, so WALL-E edges towards a
    // possible drop instead of charging at it.
    uint8_t speedFor(uint8_t requested) const;

    // ---- voice ----
    // Behavior brackets every conversation with these, which is how
    // "the motors stop while it answers" is enforced.
    void noteVoiceStart();
    void noteVoiceEnd();
    bool voiceBusy() const { return _voiceBusy; }

    // ---- external control ----
    // While a remote or the app owns the wheels, the guard must not
    // start a manoeuvre of its own. The dispatcher keeps this set.
    void setExternallyDriven(bool on) { _external = on; }
    bool externallyDriven() const { return _external; }

    // Hard stop on demand (a STOP command, a link timeout, an error).
    // This NEVER leaves a permanent block: the robot must still accept
    // the next command. Pass withCooldown to also keep it parked for a
    // while (used after a cliff).
    void forceStop(const char* why, bool withCooldown = false);

    // Sensor state, forwarded for the OLED status line and the links.
    uint8_t sensorState() const;

private:
    void onSensorStateChanged(uint8_t next, uint32_t now);
    void startBackAway(uint32_t now);

    CliffSensor* _sensor = nullptr;

    uint8_t  _block = WHEELS_OK;
    bool     _voiceBusy = false;
    bool     _external = false;

    uint8_t  _lastSensorState = WALLE_CLIFF_UNKNOWN;
    uint32_t _cooldownUntilMs = 0;
    bool     _backingAway = false;
};

extern SafetyGuard safety;