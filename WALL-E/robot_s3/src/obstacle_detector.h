// ============================================================
//  Obstacle detector interface
//  ------------------------------------------------------------
//  Split out of behavior.h so that a sensor module can implement it
//  without including behaviour (which includes the state machine,
//  Gemini, TTS and the OLED). behaviour.h and cliff_sensor.h both
//  include this one small header, so there are no cycles.
//
//  A detector answers ONE question: "is it safe to drive forward
//  right now?" Everything else - what to do about it, whether to
//  stop, turn or back off - belongs to safety.h and command_dispatch.
//
//  There is now a real implementation: CliffSensor (HC-SR04). Any
//  other sensor (ToF, IR break-beam, bump switch) plugs in the same
//  way, and behaviour does not change at all.
// ============================================================
#pragma once

class ObstacleDetector {
public:
    virtual ~ObstacleDetector() {}

    // Returns true when something is detected in the way.
    // *proximityOut, when not null, receives 0.0 (clear) .. 1.0
    // (right on top of the threshold), so a caller can creep.
    virtual bool detect(float* proximityOut) = 0;

    // Human-readable state name for logs and the OLED status line.
    virtual const char* stateName() const { return "unknown"; }
};