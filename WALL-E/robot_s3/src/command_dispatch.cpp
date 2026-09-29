// ============================================================
//  Command dispatcher — the robot's single command interface
//  See command_dispatch.h for the priority rules.
// ============================================================
#include "command_dispatch.h"
#include "behavior.h"
#include "dance.h"
#include "log.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "remote_link.h"
#include "robot_state.h"

static const char* TAG = "S3";

CommandDispatcher commands;

namespace {

// The remote's control window. Same value as REMOTE_CONTROL_HOLD_MS
// in config.h; the dispatcher is the consumer, the link is the owner.
constexpr uint32_t kControlHoldMs = REMOTE_CONTROL_HOLD_MS;

// If a movement command somehow survives this long without the
// state machine changing anything, stop anyway. This is the belt to
// the link's braces: MAX_DRIVE_MS is the absolute motor watchdog.
constexpr uint32_t kMotionSafetyMs = MAX_DRIVE_MS;

uint32_t gMotionStartedMs = 0;

// Reflects the current motion command onto the wheels. Every value a
// remote can ask for is in this table - the remote itself never sends
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

// The robot is under a command that is holding the wheels.
bool motionActive() {
    return gMotionStartedMs != 0 && (millis() - gMotionStartedMs) < kMotionSafetyMs;
}

void stopMotion() {
    if (!motionActive() && gMotionStartedMs == 0) {
        motors.stop();
        return;
    }
    gMotionStartedMs = 0;
    motors.stop();
}

}  // namespace

const char* CommandDispatcher::sourceName(CommandSource s) {
    switch (s) {
        case SOURCE_REMOTE:   return "remote";
        case SOURCE_SERIAL:   return "serial";
        case SOURCE_APP:      return "app";
        case SOURCE_INTERNAL: return "internal";
        default:              return "?";
    }
}

uint8_t CommandDispatcher::robotState() const {
    return gRobotSM ? (uint8_t)gRobotSM->state() : (uint8_t)STATE_BOOT;
}

void CommandDispatcher::onRemoteLost() {
    if (!_remoteControls && gMotionStartedMs == 0) return;
    stopMotion();
    _remoteControls = false;
    if (gRobotSM && gRobotSM->state() == STATE_REMOTE_MANUAL) {
        gRobotSM->request(STATE_IDLE);
    }
    LOGW(TAG, "Remote control released");
}

void CommandDispatcher::tick(uint32_t now) {
    // Backstop: a held button re-sends every REMOTE_REPEAT_MS, so a
    // motion command older than the control window means something
    // went wrong upstream. Stop rather than assume.
    if (_remoteControls && gMotionStartedMs != 0 &&
        (now - _lastRemoteMotionMs) > kControlHoldMs * 4) {
        LOGW(TAG, "Stale remote motion -> STOP");
        onRemoteLost();
    }
}

bool CommandDispatcher::dispatch(uint8_t command, CommandSource source) {
    const uint32_t now = millis();
    const char* src = sourceName(source);

    // A command that moves the robot needs actual motor pins.
    if (walle_cmd_is_motion(command) && !motors.motorsConfigured()) {
        LOGW(TAG, "%s: %s refused, motors unset", src, walle_command_name(command));
        remoteLink.sendError(WALLE_ERR_NOT_CONFIGURED);
        return false;
    }

    // ---- P0: STOP always wins, from any source, no questions ----
    if (command == WALLE_CMD_STOP || command == WALLE_CMD_IDLE || command == WALLE_CMD_BYE) {
        const bool wasRemoteDriving = (source == SOURCE_REMOTE);
        LOGI(TAG, "%s: %s", src, walle_command_name(command));

        behavior.halt();                 // cancels dance, Gemini, explore
        stopMotion();
        remoteLink.setDriving(false);
        _remoteControls = false;

        // Only a remote STOP resumes autonomy. An app or serial STOP
        // leaves the robot parked, so it cannot drive off by itself
        // right after somebody hit the emergency stop.
        if (wasRemoteDriving) {
            behavior.setAutonomous(true);
            LOGI(TAG, "Autonomy resumed after remote stop");
        }
        remoteLink.sendAck(command);
        return true;
    }

    // ---- link housekeeping ----
    if (command == WALLE_CMD_HELLO || command == WALLE_CMD_PING) {
        remoteLink.sendAck(command);
        return true;
    }

    // ---- P1: remote owns the wheels ----
    if (walle_cmd_is_motion(command)) {
        if (_remoteControls && source != SOURCE_REMOTE) {
            // Deterministic refusal: no two sources ever fight.
            LOGW(TAG, "%s: %s refused, remote has control", src,
                 walle_command_name(command));
            remoteLink.sendError(WALLE_ERR_BUSY);
            return false;
        }

        behavior.suspendAutonomy(now);   // park self-driving behaviour
        applyMotion(command);
        _lastRemoteMotionMs = now;
        if (source == SOURCE_REMOTE) {
            remoteLink.setDriving(true);
            _remoteControls = true;
        }
        gRobotSM->request(STATE_REMOTE_MANUAL);
        LOGI(TAG, "%s: %s", src, walle_command_name(command));
        return true;
    }

    // ---- P3: modes and expressions ----
    switch (command) {
        case WALLE_CMD_DANCE:
            if (_remoteControls) { LOGW(TAG, "%s: dance refused, remote has control", src); return false; }
            behavior.requestDance();
            break;

        case WALLE_CMD_EXPLORE:
            if (_remoteControls) { LOGW(TAG, "%s: explore refused, remote has control", src); return false; }
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

        case WALLE_CMD_EXPR_HAPPY:     oled.setExpression(EXPR_HAPPY);     break;
        case WALLE_CMD_EXPR_THINKING:  oled.setExpression(EXPR_THINKING);  break;
        case WALLE_CMD_EXPR_SURPRISED: oled.setExpression(EXPR_SURPRISED); break;
        case WALLE_CMD_EXPR_CONFUSED:  oled.setExpression(EXPR_CONFUSED);  break;
        case WALLE_CMD_EXPR_IDLE:      oled.setExpression(EXPR_IDLE);      break;

        default:
            LOGW(TAG, "%s: unknown command 0x%02X", src, command);
            remoteLink.sendError(WALLE_ERR_UNKNOWN_CMD);
            return false;
    }

    LOGI(TAG, "%s: %s", src, walle_command_name(command));
    remoteLink.sendAck(command);
    return true;
}
