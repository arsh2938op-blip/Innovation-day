// ============================================================
//  Behaviour layer
//  ------------------------------------------------------------
//  Owns the robot state machine and decides what WALL-E does:
//    * explore its surroundings
//    * answer a question with Gemini (text only - see below)
//    * tell a joke when nobody is talking to it
//    * dance
//
//  VOICE NOTE: STT and TTS were removed from this firmware, so
//  there is no microphone capture and no speech synthesis any
//  more. WALL-E's Gemini personality is untouched - a reply is
//  now shown on the OLED caption and forwarded to the companion
//  app instead of being spoken.
//
//  All timing is non-blocking. The one network call (Gemini) is
//  synchronous but time-bounded (see GEMINI_TIMEOUT_MS) and runs
//  as a single step of the conversation script, so the state
//  machine never gets stuck.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "robot_state.h"

// ------------------------------------------------------------
//  Obstacle detection interface
//  ------------------------------------------------------------
//  !! MISSING HARDWARE !!
//  The parts list has no distance/bump/ToF sensor, so there is no
//  reliable way to know what is in front of WALL-E yet. Instead of
//  pretending, everything that would need a sensor goes through
//  this interface. The default implementation always says "clear",
//  which makes WALL-E drive in straight lines and rely on timed
//  turns instead of actual avoidance.
//
//  To add real avoidance, implement this interface and call
//  Behavior::setObstacleDetector() during setup.
// ============================================================
class ObstacleDetector {
public:
    virtual ~ObstacleDetector() {}
    // Returns true if something is detected in front, 0..1 = how close.
    virtual bool detect(float* proximityOut) { (void)proximityOut; return false; }
};

class Behavior {
public:
    void begin(RobotStateMachine* sm);
    void setObstacleDetector(ObstacleDetector* d) { _obstacles = d; }

    // Call once per loop() iteration.
    void update(uint32_t now);

    // ---- external triggers (serial console, remote, test mode) ----
    void requestChat(const String& prompt);   // ask Gemini about `prompt`
    void requestJoke();
    void requestDance();
    void requestExplore();
    void requestIdle();
    void halt();                      // stop everything immediately

    // ---- autonomous mode ----
    // While autonomy is on, IDLE schedules jokes / dance / explore
    // on its own. Turning it off parks the robot in plain IDLE.
    // Driven by the remote's autonomous_on / autonomous_off.
    void setAutonomous(bool on) { _autonomous = on; if (!on) requestIdle(); }
    bool autonomous() const { return _autonomous; }

    // ---- external (remote / app) manual drive ----
    // The remote drives the motors directly through the dispatcher
    // rather than through the state machine, so it needs a way to
    // suspend autonomy without cancelling an in-flight joke.
    void suspendAutonomy(uint32_t now);

private:
    // steps of the "someone talked to me" script
    enum ConvStep {
        CONV_IDLE_STEP,
        CONV_GEMINI_REQUEST,
        CONV_DONE
    };

    RobotStateMachine* _sm = nullptr;
    ObstacleDetector*  _obstacles = nullptr;

    // conversation
    ConvStep _conv = CONV_IDLE_STEP;
    String   _prompt, _reply;
    bool     _jokeMode = false;
    bool     _autonomous = WALLE_AUTONOMOUS_DEFAULT;

    // exploration
    uint32_t _exploreNextMs = 0;
    uint32_t _lastSelfChatMs = 0;

    // jokes
    static const char* kJokePrompt;

    // ---- helpers ----
    void updateConversation(uint32_t now);
    void advanceConversation(ConvStep next, uint32_t now);
    void conversationFinished(uint32_t now);
    void scheduleNextSelfChat(uint32_t now);
    void updateExploration(uint32_t now);
    void updateIdle(uint32_t now);
    void randomFunAction(uint32_t now);
    uint32_t randomRange(uint32_t min, uint32_t max);
};

extern Behavior behavior;
