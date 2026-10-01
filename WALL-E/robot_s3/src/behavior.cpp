#include "behavior.h"
#include "log.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "dance.h"
#include "camera_manager.h"
#include "wifi_manager.h"
#include "gemini_client.h"
#include "tts_client.h"
#include "audio_output.h"
#include "cliff_sensor.h"
#include "maneuver.h"
#include "command_dispatch.h"
#include "persona.h"
#include "safety.h"

static const char* TAG = "ROBOT";
static const char* TAGAI = "AI";

Behavior behavior;

GeminiClient gemini;
TtsClient    tts;

// The firmware only decides WHEN to be funny; Gemini writes the actual line.
const char* Behavior::kJokePrompt =
    "Tell me one very short joke about being a small rusty robot. "
    "One or two sentences maximum.";

// ------------------------------------------------------------
void Behavior::begin(RobotStateMachine* sm) {
    _sm = sm;
    _conv = CONV_IDLE_STEP;
    _exploreNextMs = 0;
    _lastSelfChatMs = millis();

    // Start with the compiled-in personality, so the robot has a voice
    // even before the app has connected and sent its own.
    persona.begin();

    // Push it into the AI client now, and again before every request:
    // the app may replace the persona at any time while connected.
    gemini.setSystemInstruction(persona.prompt());
    LOGI(TAG, "I am %s", persona.name());
}

uint32_t Behavior::randomRange(uint32_t min, uint32_t max) {
    if (max <= min) return min;
    return min + (esp_random() % (max - min));
}

void Behavior::halt() {
    _conv = CONV_IDLE_STEP;
    _prompt = "";
    _reply = "";
    _jokeMode = false;
    _speakReplies = (TTS_SPEAK_REPLIES != 0);
    dance.stop();
    maneuver.cancel("halt");     // no timed motion may outlive a halt
    speaker.stop();               // cut off any audio mid-sentence
    motors.emergencyStop();
    if (_sm) _sm->request(STATE_IDLE);
    oled.setExpression(EXPR_IDLE);

    // Releases the voice block so WALL-E can drive again afterwards.
    safety.noteVoiceEnd();
}

// ------------------------------------------------------------
//  Bring the wheels to a stop before WALL-E says anything.
//
//  Every entry point into a conversation goes through this one
//  function. That is what guarantees there is no code path which asks
//  Gemini or TTS while a wheel is still turning.
// ------------------------------------------------------------
void Behavior::parkForVoice() {
    dance.stop();
    maneuver.cancel("answering");
    motors.emergencyStop();

    // Tells the safety guard to refuse motion for as long as the
    // conversation lasts, so no controller can drive it mid-sentence.
    safety.noteVoiceStart();

    if (_sm) _sm->request(STATE_THINKING);
}

// ------------------------------------------------------------
//  Speak arbitrary text (no Gemini involved).
//  Reuses the conversation machinery so the speaking state, the face
//  and the error handling are identical to a normal reply.
// ------------------------------------------------------------
void Behavior::requestSpeak(const String& text) {
    if (text.length() == 0) return;
    if (_conv != CONV_IDLE_STEP) { LOGW(TAG, "Busy, ignoring speak request"); return; }

    // Stop first: the wheels are going quiet before the amp comes on.
    parkForVoice();

    if (!tts.ready()) {
        LOGW(TAG, "TTS unavailable");
        oled.setExpression(EXPR_ERROR);
        oled.setStatus("tts?");
        safety.noteVoiceEnd();
        if (_sm) _sm->request(STATE_IDLE);
        return;
    }
    if (!speaker.ready()) {
        LOGW(TAG, "No speaker wired");
        oled.setStatus("no spk");
        safety.noteVoiceEnd();
        if (_sm) _sm->request(STATE_IDLE);
        return;
    }

    _reply = text;
    _speakReplies = true;
    oled.setCaption(_reply.c_str());
    if (_sm) _sm->request(STATE_SPEAKING);
    _conv = CONV_TTS_STEP;
}

// A remote (or app) movement command took the wheels: park the
// autonomous behaviour until the remote lets go. An in-flight
// Gemini call is left alone - the dispatcher owns the motors.
void Behavior::suspendAutonomy(uint32_t now) {
    dance.stop();
    if (_sm->state() == STATE_EXPLORING || _sm->state() == STATE_MOVING ||
        _sm->state() == STATE_OBSERVING) {
        motors.stop();
        _sm->request(STATE_IDLE);
    }
    // Push the next self-initiated action out of the way so the
    // robot does not wander the moment the remote stops driving.
    _lastSelfChatMs = now;
    _exploreNextMs  = now + CONVO_IDLE_GAP_MS_MAX;
}

void Behavior::requestChat(const String& prompt) {
    if (_conv != CONV_IDLE_STEP) return;
    _prompt   = prompt;
    _reply    = "";
    _jokeMode = false;
    parkForVoice();                 // stops the wheels BEFORE the request
    _conv = CONV_GEMINI_REQUEST;
}
void Behavior::requestDance()  { if (_conv != CONV_IDLE_STEP) return; _sm->request(STATE_DANCING); }
void Behavior::requestExplore(){ if (_conv != CONV_IDLE_STEP) return; _sm->request(STATE_EXPLORING); }
void Behavior::requestIdle()   { halt(); }
void Behavior::requestJoke() {
    if (_conv != CONV_IDLE_STEP) return;
    _reply    = "";
    _prompt   = "";
    _jokeMode = true;
    parkForVoice();                 // stops the wheels BEFORE the request
    _conv = CONV_GEMINI_REQUEST;
}

// ------------------------------------------------------------
//  MAIN UPDATE
// ------------------------------------------------------------
void Behavior::update(uint32_t now) {
    if (!_sm) return;

    // 1. offline handling outranks everything else
    if (WALLE_ENABLE_WIFI && wifi.configured() && !wifi.connected()) {
        if (_sm->state() != STATE_OFFLINE && _conv == CONV_IDLE_STEP) {
            LOGW(TAG, "Offline - pausing AI features");
            dance.stop();
            _sm->request(STATE_OFFLINE);
        }
    } else if (_sm->state() == STATE_OFFLINE) {
        LOGI(TAG, "Back online");
        _sm->request(STATE_IDLE);
    }

    // 2. a conversation, if one is running, owns the robot
    if (_conv != CONV_IDLE_STEP) {
        updateConversation(now);
        return;
    }

    // 3. state specific behaviour
    switch (_sm->state()) {
        case STATE_DANCING: {
            if (!dance.running()) {           // dance was started, run it
                dance.start();
            }
            dance.update(now);
            if (!dance.running()) {
                oled.setExpression(EXPR_HAPPY);
                _sm->request(STATE_IDLE);
                _lastSelfChatMs = now;
            }
            break;
        }

        case STATE_EXPLORING: updateExploration(now); break;

        // MOVING / OBSERVING are the transient sub-states of exploring.
        // They own the motors, so they MUST stop them when their deadline
        // expires - otherwise the robot would drive off forever.
        case STATE_MOVING:
        case STATE_OBSERVING:
            if (now >= _exploreNextMs) {
                motors.stop();
                _sm->request(STATE_EXPLORING);
            }
            break;

        case STATE_IDLE:      updateIdle(now);      break;
        case STATE_OFFLINE:   oled.setStatus("offline"); break;

        case STATE_MANUAL:
            // The app owns the motors here, and the dispatcher stops
            // them on release, timeout or link loss. Nothing for us to
            // do, and in particular nothing here may restart autonomy
            // behind the app's back.
            break;

        default:
            // THINKING only exists inside a Gemini conversation, so
            // reaching here means something external pushed us - go
            // home rather than sit in a state with no owner.
            motors.stop();
            _sm->request(STATE_IDLE);
            break;
    }
}

// ------------------------------------------------------------
//  CONVERSATION  (listen -> transcribe -> ask Gemini -> speak)
// ------------------------------------------------------------
void Behavior::advanceConversation(ConvStep next, uint32_t now) {
    (void)now;
    _conv = next;
}

void Behavior::updateConversation(uint32_t now) {
    switch (_conv) {

        // ---- ask Gemini (blocking, time bounded) ----
        case CONV_GEMINI_REQUEST: {
            _sm->request(STATE_THINKING);
            if (!gemini.ready()) { oled.setExpression(EXPR_ERROR); conversationFinished(now); return; }

            // The persona may have been replaced by the app since the
            // last question, so re-read it every single time.
            gemini.setSystemInstruction(persona.prompt());

            bool ok = _jokeMode ? gemini.askWithPrompt("Go on then.", kJokePrompt, &_reply)
                                : gemini.ask(_prompt, &_reply);
            _jokeMode = false;
            if (!ok) {
                oled.setExpression(EXPR_CONFUSED);
                oled.setStatus("gemini?");
                conversationFinished(now);
                return;
            }

            // The persona's suffix, added here rather than being
            // trusted to the model: Gemini is told to add it, but the
            // robot must not depend on that to sound like itself.
            persona.decorate(&_reply);

            // The reply is always shown on the face, and — when a
            // speaker is wired — spoken out loud through Gemini TTS.
            oled.setCaption(_reply.c_str());
            if (_speakReplies) {
                advanceConversation(CONV_TTS_STEP, now);
                break;
            }
            conversationFinished(now);
            break;
        }

        // ---- speak the reply (streams audio straight to the amp) ----
        case CONV_TTS_STEP: {
            _sm->request(STATE_SPEAKING);

            if (!tts.ready()) {
                LOGW(TAG, "TTS unavailable - reply stays on the face");
                conversationFinished(now);
                return;
            }
            if (!tts.speak(_reply)) {
                // Never let a speech failure become a robot failure.
                oled.setExpression(EXPR_CONFUSED);
                oled.setStatus("tts?");
                LOGW(TAG, "Speech failed, carrying on silently");
                conversationFinished(now);
                return;
            }
            // Keep the SPEAKING face up until the audio has actually
            // finished playing, not just until it finished downloading.
            if (speaker.busy()) { advanceConversation(CONV_PLAYING, now); break; }
            conversationFinished(now);
            break;
        }

        // ---- wait for the speaker to drain ----
        case CONV_PLAYING: {
            if (speaker.busy()) break;              // main loop pumps it
            conversationFinished(now);
            break;
        }

        case CONV_DONE:
        default:
            conversationFinished(now);
            break;
    }
}

void Behavior::conversationFinished(uint32_t now) {
    // Push the answer out while it still exists in _reply: the app is
    // the one controller that can show words, the OLED caption is only
    // 21 characters.
    if (_reply.length() > 0) {
        commands.notifyText(WALLE_OP_REPLY, _reply.c_str());
    }

    _prompt = "";
    _reply  = "";
    _conv = CONV_IDLE_STEP;
    _sm->request(STATE_IDLE);
    oled.setCaption("");
    oled.setStatus("");
    oled.setExpression(EXPR_IDLE);
    _lastSelfChatMs = now;
    scheduleNextSelfChat(now);

    // Only now may the wheels move again.
    safety.noteVoiceEnd();
}

// ------------------------------------------------------------
//  IDLE - occasionally start something on its own
// ------------------------------------------------------------
void Behavior::scheduleNextSelfChat(uint32_t now) {
    _exploreNextMs = now + randomRange(CONVO_IDLE_GAP_MS_MIN, CONVO_IDLE_GAP_MS_MAX);
}

void Behavior::updateIdle(uint32_t now) {
    oled.setExpression(EXPR_IDLE);

    // autonomy off -> the robot just sits there until told otherwise
    if (!_autonomous) return;

    if (WALLE_ENABLE_WIFI && wifi.configured() && !wifi.connected()) return;
    if (now < _exploreNextMs) return;

    // Do not start anything while the safety guard is unhappy. A cliff
    // stop, a dead sensor or WALL-E answering a question all mean the
    // robot must stay where it is.
    if (!safety.wheelsAllowed()) {
        motors.stop();
        return;
    }

    const int roll = esp_random() % 100;
    if (roll < JOKE_CHANCE_PCT) {
        LOGI(TAG, "Telling a joke");
        requestJoke();
    } else if (roll < JOKE_CHANCE_PCT + DANCE_CHANCE_PCT) {
        LOGI(TAG, "Dancing");
        _sm->request(STATE_DANCING);
    } else {
        LOGI(TAG, "Exploring");
        _sm->request(STATE_EXPLORING);
        _sm->request(STATE_MOVING);
    }
    _exploreNextMs = now + randomRange(EXPLORE_MOVE_MS_MIN, EXPLORE_MOVE_MS_MAX);
}

// ------------------------------------------------------------
//  EXPLORING - move, pause, observe, decide
// ------------------------------------------------------------
void Behavior::updateExploration(uint32_t now) {
    if (now < _exploreNextMs) return;

    // The safety guard owns the cliff reaction. While it is unhappy
    // (a drop, a dead sensor, a conversation in progress) exploration
    // must not plan anything new - the guard is already stopping the
    // robot and walking it backwards off the edge.
    if (!safety.wheelsAllowed()) {
        motors.stop();
        _sm->request(STATE_IDLE);
        return;
    }

    // How close to the edge, 0..1. Used to avoid choosing a fast move
    // when the floor is running out.
    float proximity = 0.0f;
    const bool blocked = _obstacles && _obstacles->detect(&proximity);

    if (blocked) {
        // Normally unreachable, because safety.update() has already
        // reacted to the same sensor before we get here. Kept so a
        // DIFFERENT detector (a front ToF, a bump switch) plugged in
        // later still works without changing this file.
        LOGI(TAG, "Obstacle detected (%.0f%%), turning away", (double)(proximity * 100));
        motors.stop();
        motors.turnRight(SPEED_TURN);
        _sm->request(STATE_MOVING);
        _exploreNextMs = now + randomRange(EXPLORE_MOVE_MS_MIN, EXPLORE_MOVE_MS_MAX);
        return;
    }

    // Near the limit: turn rather than drive on.
    if (proximity > 0.6f) {
        LOGI(TAG, "Edge close (%.0f%%), turning", (double)(proximity * 100));
        motors.stop();
        motors.turnRight(SPEED_TURN);
        _sm->request(STATE_MOVING);
        _exploreNextMs = now + randomRange(EXPLORE_MOVE_MS_MIN, EXPLORE_MOVE_MS_MAX);
        return;
    }

    switch (esp_random() % 5) {
        case 0:
            LOGI(TAG, "Observing");
            motors.stop();
            oled.setExpression(EXPR_EXPLORING);
            _sm->request(STATE_OBSERVING);
            _exploreNextMs = now + EXPLORE_OBSERVE_MS;
            if (esp_random() % 100 < EXPLORE_ACT_CHANCE_PCT) randomFunAction(now);
            break;

        case 1:
        case 2:
            LOGI(TAG, "Moving forward");
            motors.forward(SPEED_SLOW);
            _sm->request(STATE_MOVING);
            _exploreNextMs = now + randomRange(EXPLORE_MOVE_MS_MIN, EXPLORE_MOVE_MS_MAX);
            break;

        case 3:
            LOGI(TAG, "Turning");
            motors.turnLeft(SPEED_TURN);
            _sm->request(STATE_MOVING);
            _exploreNextMs = now + randomRange(EXPLORE_MOVE_MS_MIN, EXPLORE_MOVE_MS_MAX);
            break;

        default:
            LOGI(TAG, "Turning right");
            motors.turnRight(SPEED_TURN);
            _sm->request(STATE_MOVING);
            _exploreNextMs = now + randomRange(EXPLORE_MOVE_MS_MIN, EXPLORE_MOVE_MS_MAX);
            break;
    }
}

// ------------------------------------------------------------
void Behavior::randomFunAction(uint32_t now) {
    switch (esp_random() % 3) {
        case 0: _sm->request(STATE_DANCING); break;
        case 1: oled.setExpression(EXPR_SURPRISED); break;
        default: oled.setExpression(EXPR_ANGRY); break;
    }
    (void)now;
}
