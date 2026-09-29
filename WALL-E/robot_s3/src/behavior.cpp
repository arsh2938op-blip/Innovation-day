#include "behavior.h"
#include "log.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "dance.h"
#include "camera_manager.h"
#include "wifi_manager.h"
#include "gemini_client.h"

static const char* TAG = "ROBOT";
static const char* TAGAI = "AI";

Behavior behavior;

GeminiClient gemini;

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
    dance.stop();
    motors.emergencyStop();
    _sm->request(STATE_IDLE);
    oled.setExpression(EXPR_IDLE);
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
    _sm->request(STATE_THINKING);
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
    _sm->request(STATE_THINKING);
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

        case STATE_REMOTE_MANUAL:
            // The remote link owns the motors here (and stops them on
            // release, timeout or link loss). Nothing for us to do.
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

            bool ok = _jokeMode ? gemini.askWithPrompt("Go on then.", kJokePrompt, &_reply)
                                : gemini.ask(_prompt, &_reply);
            _jokeMode = false;
            if (!ok) {
                oled.setExpression(EXPR_CONFUSED);
                oled.setStatus("gemini?");
                conversationFinished(now);
                return;
            }
            // No TTS any more: the reply is shown on the OLED
            // caption (and forwarded to the app by the caller).
            oled.setCaption(_reply.c_str());
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
    _prompt = "";
    _reply  = "";
    _conv = CONV_IDLE_STEP;
    _sm->request(STATE_IDLE);
    oled.setCaption("");
    oled.setStatus("");
    oled.setExpression(EXPR_IDLE);
    _lastSelfChatMs = now;
    scheduleNextSelfChat(now);
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

    float proximity = 1.0f;
    const bool blocked = _obstacles && _obstacles->detect(&proximity);

    if (blocked) {
        // Only reachable once a real detector is implemented - see
        // ObstacleDetector in behavior.h.
        LOGI(TAG, "Obstacle detected, turning away");
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
