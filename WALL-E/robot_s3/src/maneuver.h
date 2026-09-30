// ============================================================
//  Maneuver - timed, non-blocking motion primitives
//  ------------------------------------------------------------
//  The named motions on MotorController (forward, turnLeft, ...) say
//  WHICH way to go and then keep going until somebody stops them.
//  That is exactly right for a held remote button and exactly wrong
//  for "drive 4 steps and stop", which is what a controller, the
//  app or an obstacle-avoidance routine actually needs.
//
//  A maneuver is one of those plus a deadline:
//
//      startForwardSteps(4)   ->  forward for N x STEP_DISTANCE_CM
//      startTurnAround()      ->  pivot 180 degrees on the spot
//      startRetreatSteps(3)   ->  the back-away after a cliff stop
//
//  WHY THERE ARE NO ENCODERS IN THE HONEST ANSWER
//  ----------------------------------------------
//  There are no wheel encoders on this robot, so a distance cannot be
//  measured - only estimated. The firmware converts a distance into a
//  duration using MANEUVER_CM_PER_SECOND / MANEUVER_MS_PER_TURN_360
//  (calibrated once, see config.h section 10b) and then times it.
//
//  That is fine for "get over there" and fine for a dance. It drifts
//  with battery level and floor surface, so do not rely on it for
//  anything that has to be accurate to the centimetre. Real odometry
//  would need encoders on the motors and a different motor driver.
//
//  EVERY maneuver is a SAFETY-OWNED motion: update() asks the guard
//  first and stops the instant it is told to, so a cliff detected
//  half way through "move 8 steps" truncates the maneuver rather
//  than being ignored until it finishes.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

enum ManeuverKind : uint8_t {
    MAN_NONE = 0,
    MAN_FORWARD,
    MAN_BACKWARD,
    MAN_TURN_LEFT,
    MAN_TURN_RIGHT,
    MAN_TURN_AROUND,
    MAN_RETREAT,
};

class Maneuver {
public:
    void begin();

    // ---- the primitives ----
    bool startForward(uint32_t ms);
    bool startBackward(uint32_t ms);
    bool startTurnLeft(uint32_t ms);
    bool startTurnRight(uint32_t ms);

    // Distance based. `steps` is in STEP_DISTANCE_CM units and is
    // clamped to 1..WALLE_MAX_STEPS.
    bool startForwardSteps(uint16_t steps);
    bool startRetreatSteps(uint16_t steps);

    // Angle based. `degrees` is clamped to 1..360.
    bool startTurnDegrees(uint16_t degrees);
    bool startTurnAround();                 // 180 degrees

    // ---- main loop ----
    void update(uint32_t now);

    // Finishes the maneuver now.
    void stop();

    // Aborts it, e.g. because something took priority. Reason is
    // logged; this never leaves a wheel turning.
    void cancel(const char* why);

    // ---- state ----
    bool busy() const { return _kind != MAN_NONE; }
    ManeuverKind kind() const { return _kind; }
    static const char* nameOf(ManeuverKind k);
    uint32_t remainingMs(uint32_t now) const;

    // ---- geometry helpers, exposed for the test menu and status ----
    static uint32_t msForDistanceCm(uint16_t cm, uint8_t speed);
    static uint32_t msForDegrees(uint16_t degrees);

private:
    bool launch(ManeuverKind kind, uint32_t ms, const char* what);
    void applyMotion();

    ManeuverKind _kind = MAN_NONE;
    uint32_t _startedMs = 0;
    uint32_t _durationMs = 0;
    uint8_t  _speed = MANEUVER_SPEED;
    bool     _blockedLogged = false;
};

extern Maneuver maneuver;