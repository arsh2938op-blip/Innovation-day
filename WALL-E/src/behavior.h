// ============================================================
//  Behaviour layer
//  ------------------------------------------------------------
//  Owns the robot state machine and decides what WALL-E does:
//    * explore its surroundings
//    * listen -> transcribe -> ask Gemini -> speak
//    * tell a joke when nobody is talking to it
//    * dance
//
//  All timing is non-blocking. The three network calls (STT, Gemini,
//  TTS) are synchronous but time-bounded (see config.h) and each one
//  runs as a single step of the conversation script, so the state
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

    // ---- external triggers (serial console, test mode) ----
    void requestTalk();
    void requestDance();
    void requestJoke();
    void requestExplore();
    void halt();                      // stop everything immediately

private:
    // steps of the "someone talked to me" script
    enum ConvStep {
        CONV_IDLE_STEP,
        CONV_RECORD_START,
        CONV_RECORDING,
        CONV_STT_REQUEST,
        CONV_GEMINI_REQUEST,
        CONV_TTS_REQUEST,
        CONV_PLAYING,
        CONV_DONE
    };

    RobotStateMachine* _sm = nullptr;
    ObstacleDetector*  _obstacles = nullptr;

    // conversation
    ConvStep _conv = CONV_IDLE_STEP;
    uint8_t  _ttsChunk = 0, _ttsChunkMax = 0;
    String   _transcript, _reply;
    String   _chunks[4];
    bool     _jokeMode = false;

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
