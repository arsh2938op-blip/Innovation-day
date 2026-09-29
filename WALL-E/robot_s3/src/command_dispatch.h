// ============================================================
//  Command dispatcher — the robot's single command interface
//  ------------------------------------------------------------
//  Every controller (the ESP-NOW remote, the serial console, and
//  later the companion app or a web bridge) funnels its commands
//  through dispatch(). There is exactly ONE implementation of each
//  command, so adding a new controller never means adding a new
//  version of a robot command.
//
//  This is also where command priority is decided. The rules, in
//  order, are:
//
//    P0  STOP / BYE from ANY source is always executed. It can
//        never be refused, queued or overridden. It cancels dance,
//        a Gemini call, exploration and remote driving at once.
//    P1  While the remote holds control (a movement command within
//        REMOTE_CONTROL_HOLD_MS of a live link), movement commands
//        from other sources are REFUSED and logged. Exactly one
//        source can ever own the wheels.
//    P2  Otherwise the most recent movement command wins, and
//        WALL-E goes back to driving itself (IDLE) afterwards.
//    P3  Mode and expression commands are always accepted from any
//        source, except that they are ignored while the remote
//        holds control for anything that would move the robot.
//
//  A refused command still gets an error status back to the remote,
//  so the UI can tell the user why nothing happened.
// ============================================================
#pragma once

#include <Arduino.h>
#include "walle_protocol.h"

enum CommandSource {
    SOURCE_REMOTE = 0,
    SOURCE_SERIAL,
    SOURCE_APP,
    SOURCE_INTERNAL,
};

class CommandDispatcher {
public:
    // Executes one command id. Returns true if it ran.
    bool dispatch(uint8_t command, CommandSource source);

    // Call every loop(). Backstop only: stops the wheels if a motion
    // command somehow went stale without a STOP or a link timeout.
    void tick(uint32_t now);

    // Called by the remote link when the remote goes away. Always
    // stops the wheels and releases the control lock.
    void onRemoteLost();

    // For logs and for the status frames the robot sends back.
    static const char* sourceName(CommandSource s);

    // Current RobotState as a plain byte, for the status protocol.
    uint8_t robotState() const;

    // True while the remote owns the wheels.
    bool remoteHasControl() const { return _remoteControls; }

private:
    bool _remoteControls = false;      // wheels locked to the remote
    uint32_t _lastRemoteMotionMs = 0;  // for the stale-motion backstop
};

extern CommandDispatcher commands;
