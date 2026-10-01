// ============================================================
//  Command dispatcher implementation
//  See command_dispatch.h for the priority rules.
// ============================================================
#include "command_dispatch.h"
#include "behavior.h"
#include "cliff_sensor.h"
#include "dance.h"
#include "gemini_client.h"
#include "log.h"
#include "maneuver.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "persona.h"
#include "app_link.h"
#include "robot_state.h"
#include "safety.h"
#include "tts_client.h"
#include "audio_output.h"

extern TtsClient tts;      // single instances, owned by behavior.cpp
extern GeminiClient gemini;

static const char* TAG = "S3";

CommandDispatcher commands;

namespace {

uint32_t gMotionStartedMs = 0;

// Reflects a held motion command onto the wheels. Every value a
// controller can ask for is in this table - the app never sends
// speeds, it only names the direction.
void applyMotion(uint8_t command) {
    switch (command) {
        case WALLE_CMD_MOVE_FORWARD:  motors.forward(SPEED_WALK);  break;
        case WALLE_CMD_MOVE_BACKWARD: motors.backward(SPEED_SLOW); break;
        case WALLE_CMD_TURN_LEFT:     motors.left(SPEED_SLOW);     break;
        case WALLE_CMD_TURN_RIGHT:    motors.right(SPEED_SLOW);    break;
        case WALLE_CMD_ROTATE_LEFT:   motors.turnLeft(SPEED_TURN); break;
        case WALLE_CMD_ROTATE_RIGHT:  motors.turnRight(SPEED_TURN);break;
        default: return;
    }
    gMotionStartedMs = millis();
}

void stopMotion() {
    gMotionStartedMs = 0;
    // A controller press also cancels anything the robot was doing on
    // its own, including a timed maneuver.
    maneuver.cancel("controller command");
    motors.stop();
}

// Maps a safety block onto the error the app knows how to explain.
// The wording lives in the app, not here: the firmware only says what
// happened.
uint8_t errorForBlock(uint8_t reason) {
    switch (reason) {
        case WHEELS_BLOCKED_CLIFF:  return WALLE_ERR_CLIFF;
        case WHEELS_BLOCKED_SENSOR: return WALLE_ERR_SENSOR_FAULT;
        default:                    return WALLE_ERR_BUSY;
    }
}

}  // namespace

// ------------------------------------------------------------
//  Outbound status
// ------------------------------------------------------------
void CommandDispatcher::notify(uint8_t status, uint8_t value, uint16_t arg) {
    appLink.sendStatus(status, value, arg);
}

void CommandDispatcher::notifyAck(uint8_t command) {
    notify(WALLE_ST_ACK, command);
}

void CommandDispatcher::notifyError(uint8_t err) {
    LOGW(TAG, "reporting error %u", err);
    notify(WALLE_ST_ERROR, err);
}

void CommandDispatcher::notifyText(uint8_t op, const char* text) {
    appLink.sendText(op, text);
}

// ------------------------------------------------------------
const char* CommandDispatcher::sourceName(CommandSource s) {
    switch (s) {
        case SOURCE_APP:      return "app";
        case SOURCE_SERIAL:   return "serial";
        case SOURCE_INTERNAL: return "internal";
        case SOURCE_NONE:     return "none";
        default:              return "?";
    }
}

uint8_t CommandDispatcher::robotState() const {
    return gRobotSM ? (uint8_t)gRobotSM->state() : (uint8_t)STATE_BOOT;
}

void CommandDispatcher::releaseControl() {
    _owner = SOURCE_NONE;
    _maneuverByOwner = false;
    safety.setExternallyDriven(false);
    appLink.setDriving(false);
}

void CommandDispatcher::onAppLost() {
    const bool wasOwner = (_owner == SOURCE_APP);
    stopMotion();
    if (wasOwner) releaseControl();

    if (wasOwner) {
        safety.forceStop("app lost");
        LOGW(TAG, "App control released");
    }
}

// ------------------------------------------------------------
//  The persona
// ------------------------------------------------------------
void CommandDispatcher::setPersona(const char* json) {
    if (persona.apply(json)) {
        // Apply it to the AI client immediately rather than waiting for
        // the next question, so a change is visible even if the robot
        // is mid-conversation.
        gemini.setSystemInstruction(persona.prompt());
        LOGI(TAG, "Persona accepted: %s", persona.name());
        notifyAck(WALLE_CMD_SET_PERSONA);
    } else {
        // The previous persona is untouched: all or nothing.
        LOGW(TAG, "Persona rejected - keeping the old one");
        notifyError(WALLE_ERR_BAD_ARG);
    }
}

// ------------------------------------------------------------
void CommandDispatcher::tick(uint32_t now) {
    // A timed maneuver runs to completion by itself. When it does, hand
    // the wheels back: otherwise the app that asked for "4 steps" would
    // keep the lock until somebody pressed STOP, and WALL-E's own
    // autonomy would stay locked out.
    if (_maneuverByOwner && !maneuver.busy()) {
        LOGI(TAG, "%s maneuver finished", sourceName(_owner));
        releaseControl();
        if (gRobotSM && gRobotSM->state() == STATE_MOVING) {
            gRobotSM->request(STATE_IDLE);
        }
    }

    // Backstop: the app re-sends a held command every
    // APP_HELD_REPEAT_MS, so a motion command older than four times
    // the control window means something went wrong upstream.
    if (_owner != SOURCE_NONE && gMotionStartedMs != 0 &&
        (now - _lastMotionMs) > APP_CONTROL_HOLD_MS * 4) {
        LOGW(TAG, "Stale motion from %s -> STOP", sourceName(_owner));
        stopMotion();
        releaseControl();
        safety.forceStop("stale motion");
        onAppLost();
    }
}

// ------------------------------------------------------------
//  A held movement command (the app's direction buttons)
// ------------------------------------------------------------
bool CommandDispatcher::runMotion(uint8_t command, CommandSource source) {
    // Only one controller may own the wheels.
    if (_owner != SOURCE_NONE && _owner != source) {
        LOGW(TAG, "%s: %s refused, %s has control",
             sourceName(source), walle_command_name(command), sourceName(_owner));
        notifyError(WALLE_ERR_BUSY);
        return false;
    }

    // SAFETY: asked before anything touches the motors.
    if (!safety.wheelsAllowed()) {
        const uint8_t reason = safety.blockReason();
        LOGW(TAG, "%s: %s refused, wheels blocked (%s)",
             sourceName(source), walle_command_name(command),
             SafetyGuard::reasonName(reason));
        notifyError(errorForBlock(reason));
        return false;
    }

    behavior.suspendAutonomy(millis());   // park self-driving behaviour

    // stopMotion(), not maneuver.stop(): this also clears the held-motion
    // timestamp. Leaving it set would make commands.tick() believe a
    // stale button was still held and cut a long maneuver short.
    stopMotion();

    applyMotion(command);
    _lastMotionMs = millis();
    _owner = source;
    safety.setExternallyDriven(true);
    appLink.setDriving(true);

    gRobotSM->request(STATE_MANUAL);
    LOGI(TAG, "%s: %s", sourceName(source), walle_command_name(command));
    return true;
}

// ------------------------------------------------------------
//  A timed, self-ending maneuver ("4 steps", "turn around")
// ------------------------------------------------------------
bool CommandDispatcher::runManeuver(uint8_t command, CommandSource source, uint16_t arg) {
    // A maneuver drives the robot, so it takes the same control lock.
    if (_owner != SOURCE_NONE && _owner != source) {
        LOGW(TAG, "%s: %s refused, %s has control",
             sourceName(source), walle_command_name(command), sourceName(_owner));
        notifyError(WALLE_ERR_BUSY);
        return false;
    }
    if (!safety.wheelsAllowed()) {
        const uint8_t reason = safety.blockReason();
        LOGW(TAG, "%s: %s refused, wheels blocked (%s)",
             sourceName(source), walle_command_name(command),
             SafetyGuard::reasonName(reason));
        notifyError(errorForBlock(reason));
        return false;
    }

    behavior.suspendAutonomy(millis());
    stopMotion();          // also cancels any maneuver still in flight

    bool ok = false;
    if (command == WALLE_CMD_MOVE_STEPS) {
        ok = maneuver.startForwardSteps(arg);
    } else if (command == WALLE_CMD_TURN_DEGREES) {
        ok = maneuver.startTurnDegrees(arg);
    } else {
        ok = maneuver.startTurnAround();
    }

    if (!ok) {
        notifyError(WALLE_ERR_BAD_ARG);
        return false;
    }

    // The controller holds the wheels for this maneuver, so a link
    // timeout still stops it mid-move.
    _owner = source;
    safety.setExternallyDriven(true);
    appLink.setDriving(true);
    _lastMotionMs = millis();
    _maneuverByOwner = true;

    gRobotSM->request(STATE_MOVING);
    LOGI(TAG, "%s: %s (arg %u)", sourceName(source), walle_command_name(command), arg);
    return true;
}

// ------------------------------------------------------------
bool CommandDispatcher::dispatch(uint8_t command, CommandSource source, uint16_t arg) {
    const char* src = sourceName(source);

    // ---- argument sanity, before anything else ----
    if (command == WALLE_CMD_MOVE_STEPS && (arg == 0 || arg > WALLE_MAX_STEPS)) {
        LOGW(TAG, "%s: move_steps arg %u out of range 1..%d",
             src, arg, WALLE_MAX_STEPS);
        notifyError(WALLE_ERR_BAD_ARG);
        return false;
    }
    if (command == WALLE_CMD_TURN_DEGREES && (arg == 0 || arg > 360)) {
        LOGW(TAG, "%s: turn_degrees arg %u out of range 1..360", src, arg);
        notifyError(WALLE_ERR_BAD_ARG);
        return false;
    }

    // A command that moves the robot needs real motor pins.
    if (walle_cmd_is_motion(command) && !motors.motorsConfigured()) {
        LOGW(TAG, "%s: %s refused, motor pins unset", src, walle_command_name(command));
        notifyError(WALLE_ERR_NOT_CONFIGURED);
        return false;
    }

    // ---- P0: STOP always wins, from any source, no questions ----
    if (command == WALLE_CMD_STOP || command == WALLE_CMD_IDLE || command == WALLE_CMD_BYE) {
        const CommandSource previousOwner = _owner;
        LOGI(TAG, "%s: %s", src, walle_command_name(command));

        behavior.halt();                 // cancels dance, Gemini, explore
        stopMotion();
        releaseControl();
        safety.forceStop("stop command");

        // Only a controller STOP resumes autonomy. A stop from the
        // firmware's own safety system, or from a crashed app, parks
        // the robot so it cannot drive off by itself afterwards.
        if (previousOwner == SOURCE_APP || previousOwner == SOURCE_SERIAL) {
            behavior.setAutonomous(true);
            LOGI(TAG, "Autonomy resumed after a controller stop");
        }

        // Push the new truth to whoever is listening.
        notifyAck(command);
        notify(WALLE_ST_ROBOT_STATE, robotState());
        notify(WALLE_ST_SENSOR, cliffSensor.state(), cliffSensor.groundCm());
        return true;
    }

    // ---- link housekeeping ----
    if (command == WALLE_CMD_HELLO || command == WALLE_CMD_PING) {
        notifyAck(command);
        return true;
    }

    // ---- held movement ----
    if (walle_cmd_is_motion(command)) {
        if (command == WALLE_CMD_MOVE_STEPS ||
            command == WALLE_CMD_TURN_AROUND ||
            command == WALLE_CMD_TURN_DEGREES) {
            return runManeuver(command, source, arg);
        }
        return runMotion(command, source);
    }

    // ---- "what do you see right now?" ----
    if (command == WALLE_CMD_READ_SENSOR) {
        cliffSensor.pollNow();
        const uint8_t st = cliffSensor.state();
        LOGI(TAG, "Sensor: %s, ground %u cm, slant %u cm",
             CliffSensor::nameOf(st), cliffSensor.groundCm(), cliffSensor.slantCm());
        notify(WALLE_ST_SENSOR, st, cliffSensor.groundCm());
        notifyAck(command);
        return true;
    }

    // ---- the persona ----
    if (command == WALLE_CMD_SET_PERSONA) {
        // Deliberately NO ack here. This command carries no payload: the
        // words arrive in the TEXT frame that follows, and app_link.cpp
        // calls setPersona() when it does. Acking now would tell the app
        // the persona was accepted before anyone had parsed it, and a
        // malformed payload would have no way to report itself.
        LOGI(TAG, "%s: persona frame expected next", src);
        return true;
    }

    // ---- ask / speak: handled by the text frame that follows ----
    if (command == WALLE_CMD_ASK || command == WALLE_CMD_SPEAK) {
        LOGW(TAG, "%s: %s with no text frame", src, walle_command_name(command));
        notifyError(WALLE_ERR_BAD_PACKET);
        return false;
    }

    // ---- P3: modes and expressions ----
    switch (command) {
        case WALLE_CMD_DANCE:
            if (hasController()) {
                LOGW(TAG, "%s: dance refused, %s has control", src, sourceName(_owner));
                notifyError(WALLE_ERR_BUSY);
                return false;
            }
            behavior.requestDance();
            break;

        case WALLE_CMD_EXPLORE:
            if (hasController()) {
                LOGW(TAG, "%s: explore refused, %s has control", src, sourceName(_owner));
                notifyError(WALLE_ERR_BUSY);
                return false;
            }
            behavior.requestExplore();
            break;

        case WALLE_CMD_AUTONOMOUS_ON:
            behavior.setAutonomous(true);
            LOGI(TAG, "Autonomous mode ON");
            break;

        case WALLE_CMD_AUTONOMOUS_OFF:
            behavior.setAutonomous(false);
            LOGI(TAG, "Autonomous mode OFF");
            break;

        // ---- voice: the robot asks Gemini, then speaks the answer ----
        case WALLE_CMD_TALK:
        case WALLE_CMD_JOKE:
            if (!tts.ready() || !speaker.ready()) {
                LOGW(TAG, "%s: %s refused, no TTS/speaker", src, walle_command_name(command));
                notifyError(WALLE_ERR_NOT_CONFIGURED);
                return false;
            }
            if (hasController()) {
                // Talking and driving at once would fight over the face
                // and the wheels; the controller has priority.
                LOGW(TAG, "%s: %s refused, %s has control",
                     src, walle_command_name(command), sourceName(_owner));
                notifyError(WALLE_ERR_BUSY);
                return false;
            }
            if (command == WALLE_CMD_JOKE) behavior.requestJoke();
            else behavior.requestChat("Say something interesting.");
            break;

        case WALLE_CMD_EXPR_HAPPY:     oled.setExpression(EXPR_HAPPY);     break;
        case WALLE_CMD_EXPR_THINKING:  oled.setExpression(EXPR_THINKING);  break;
        case WALLE_CMD_EXPR_SURPRISED: oled.setExpression(EXPR_SURPRISED); break;
        case WALLE_CMD_EXPR_CONFUSED:  oled.setExpression(EXPR_CONFUSED);  break;
        case WALLE_CMD_EXPR_IDLE:      oled.setExpression(EXPR_IDLE);      break;

        default:
            LOGW(TAG, "%s: unknown command 0x%02X", src, command);
            notifyError(WALLE_ERR_UNKNOWN_CMD);
            return false;
    }

    LOGI(TAG, "%s: %s", src, walle_command_name(command));
    notifyAck(command);
    return true;
}