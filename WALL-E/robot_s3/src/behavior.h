// ============================================================
//  Behaviour layer
//  ------------------------------------------------------------
//  Owns the robot state machine and decides what WALL-E does:
//    * explore its surroundings, never driving off the table
//    * answer a question with Gemini
//    * say the answer out loud with TTS
//    * tell a joke when nobody is talking to it
//    * dance
//
//  VOICE: there is no microphone, so WALL-E is *spoken to* by the
//  remote, the serial console or the app. It answers in its own voice
//  through the amplifier.
//
//  SAFETY: a conversation always happens with the wheels stopped.
//  requestChat()/requestJoke()/requestSpeak() block the wheels through
//  safety.h, because a robot that rolls around while it is thinking or
//  talking is a robot nobody can hear - and nobody can stop.
//
//  All timing is non-blocking. The two network calls (Gemini, TTS) are
//  synchronous but time-bounded (GEMINI_TIMEOUT_MS / TTS_TIMEOUT_MS) and
//  each runs as a single step of the conversation script, so the state
//  machine never gets stuck.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "robot_state.h"

// ------------------------------------------------------------
//  Obstacle detection
//  ------------------------------------------------------------
//  Anything that needs to know whether it is safe to drive asks the
//  ObstacleDetector interface (obstacle_detector.h). The real
//  implementation is CliffSensor, the HC-SR04 aimed down at 45
//  degrees that watches for the table edge; main.cpp hands it over
//  with Behavior::setObstacleDetector().
//
//  Keeping it an interface means a second sensor - a ToF module, an
//  IR break-beam, a bump switch - can be added later without touching
//  behaviour.cpp at all.
// ============================================================
#include "obstacle_detector.h"

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

    // Speak text out loud through Gemini TTS without asking Gemini for
    // it. Used by the remote's TALK button, the serial console and the
    // companion app, so a plain sentence can be pushed to the speaker.
    void requestSpeak(const String& text);

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
        CONV_TTS_STEP,
        CONV_PLAYING,
        CONV_DONE
    };

    RobotStateMachine* _sm = nullptr;
    ObstacleDetector*  _obstacles = nullptr;

    // conversation
    ConvStep _conv = CONV_IDLE_STEP;
    String   _prompt, _reply;
    bool     _jokeMode = false;
    bool     _autonomous = WALLE_AUTONOMOUS_DEFAULT;

    // Whether Gemini replies are spoken automatically. Kept per instance
    // so requestSpeak() can override it for one line.
    bool     _speakReplies = (TTS_SPEAK_REPLIES != 0);

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
    void parkForVoice();
    void updateExploration(uint32_t now);
    void updateIdle(uint32_t now);
    void randomFunAction(uint32_t now);
    uint32_t randomRange(uint32_t min, uint32_t max);
};

extern Behavior behavior;
