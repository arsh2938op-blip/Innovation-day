// ============================================================
//  Command dispatcher - the robot's single command interface
//  ------------------------------------------------------------
//  Every controller funnels its commands through dispatch():
//    * the ESP32-WROOM wireless remote  (ESP-NOW, SOURCE_REMOTE)
//    * the companion app                (TCP,     SOURCE_APP)
//    * the serial console               (UART,    SOURCE_SERIAL)
//    * the firmware itself              (          SOURCE_INTERNAL)
//
//  There is exactly ONE implementation of each command, so adding a
//  new controller never means adding a new version of a robot
//  command. That is the whole reason the app can be built without
//  touching the firmware: it is just another caller of this class.
//
//  PRIORITY RULES
//  -------------
//    P0  STOP / BYE / IDLE from ANY source always runs. It cannot be
//        refused, queued or overridden. It cancels dance, a Gemini
//        call, exploration, a maneuver and remote driving at once.
//    P1  While a controller owns the wheels, movement commands from
//        other sources are REFUSED with an error. Exactly one source
//        can ever own the wheels - there is never a contest.
//    P2  Otherwise the most recent movement command wins, and WALL-E
//        goes back to driving itself (IDLE) afterwards.
//    P3  Mode and expression commands are always accepted from any
//        source, except that anything which would MOVE the robot is
//        ignored while a controller owns the wheels.
//
//  ABOVE ALL OF THAT: SAFETY WINS. Every motion command is offered to
//  the safety guard first (safety.h). A cliff, a dead sensor or a
//  conversation in progress refuses the command, whatever asked for
//  it and whatever the priority table says.
//
//  A refused command still gets an error status back to the
//  controller, so the UI can tell the user WHY nothing happened.
// ============================================================
#pragma once

#include <Arduino.h>
#include "walle_protocol.h"

enum CommandSource : uint8_t {
    SOURCE_NONE = 0,
    SOURCE_REMOTE,
    SOURCE_SERIAL,
    SOURCE_APP,
    SOURCE_INTERNAL,
};

class CommandDispatcher {
public:
    // Executes one command id. Returns true if it ran.
    //
    // `arg` is the packet's uint16 argument. It carries the step count
    // for move_steps and is ignored by every other command; the radio
    // remote leaves it at 0 because it has no way to set it.
    bool dispatch(uint8_t command, CommandSource source, uint16_t arg = 0);

    // Call every loop(). Backstop only: stops the wheels if a motion
    // command somehow went stale without a STOP or a link timeout.
    void tick(uint32_t now);

    // Called by a link when that controller goes away. Always stops
    // the wheels and releases the control lock.
    void onRemoteLost();
    void onAppLost();

    // For logs and for the status frames the robot sends back.
    static const char* sourceName(CommandSource s);

    // Current RobotState as a plain byte, for the status protocol.
    uint8_t robotState() const;

    // Who owns the wheels right now.
    bool remoteHasControl() const { return _owner == SOURCE_REMOTE; }
    bool appHasControl() const { return _owner == SOURCE_APP; }
    bool hasController() const { return _owner != SOURCE_NONE; }
    CommandSource owner() const { return _owner; }

    // ---- outbound status, fanned out to EVERY controller ----
    // One call reaches the radio remote AND the app, which is what
    // makes the two impossible to desynchronise.
    void notify(uint8_t status, uint8_t value = 0, uint16_t arg = 0);
    void notifyAck(uint8_t command);
    void notifyError(uint8_t err);

    // Pushes WALL-E's answer to whichever controller is attached, as a
    // WALLE_MSG_TEXT frame. No-op when nobody is listening.
    void notifyText(uint8_t op, const char* text);

private:
    bool runMotion(uint8_t command, CommandSource source);
    bool runManeuver(uint8_t command, CommandSource source, uint16_t arg);

    void releaseControl();

    CommandSource _owner = SOURCE_NONE;
    uint32_t _lastRemoteMotionMs = 0;  // for the stale-motion backstop

    // True while a controller's timed maneuver is running. Used to hand
    // the wheels back the moment it finishes, so a controller that asked
    // for "4 steps" does not stay locked out of the wheels afterwards.
    bool _maneuverByOwner = false;
};

extern CommandDispatcher commands;