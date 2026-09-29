// ============================================================
//  Remote link — ESP-NOW transport to the ESP32-WROOM remote
//  ------------------------------------------------------------
//  This module does ONE job: move WallePackets over the air and
//  keep track of whether the remote is alive. It contains no
//  robot logic at all — every packet is handed to the command
//  dispatcher (command_dispatch.h), and every decision about what
//  the robot does with it is made there.
//
//  It also owns the safety watchdog: if the remote stops sending,
//  the robot is stopped. That is deliberately in the link and not
//  in the dispatcher, so link loss can never leave the motors on.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "walle_protocol.h"

enum RemoteLinkState {
    REMOTE_DISCONNECTED = 0,   // no remote has ever been heard from
    REMOTE_CONNECTING,         // we are up, nothing heard yet
    REMOTE_CONNECTED,          // packets arriving normally
    REMOTE_TIMEOUT,            // packets stopped; robot was stopped
};

class RemoteLink {
public:
    // Brings up ESP-NOW. Safe to call when Wi-Fi is still connecting:
    // ESP-NOW shares the radio, so this only pins the channel.
    bool begin();

    // Call every loop(). Handles the timeout watchdog and the
    // periodic keepalive. Never blocks.
    void update(uint32_t now);

    // ---- inbound ----
    RemoteLinkState state() const { return _state; }
    static const char* nameOf(RemoteLinkState s);
    bool connected() const { return _state == REMOTE_CONNECTED; }

    // Set by the dispatcher when a remote movement command starts or
    // ends. While true the timeout watchdog stops the motors.
    void setDriving(bool driving) { _remoteDriving = driving; }
    bool driving() const { return _remoteDriving; }

    // ---- outbound ----
    // Sends a status frame to the remote. Silently dropped when no
    // remote is known yet, so callers do not need to check.
    void sendStatus(uint8_t status, uint8_t value = 0, uint16_t arg = 0);

    // Convenience for the OLED: "remote ok" / "remote lost", or
    // nullptr when the caller should not show remote status.
    const char* statusText() const;

    // ---- inbound packet handling ----
    // Public only because the ESP-NOW C callbacks (free functions in
    // the .cpp, which keeps esp_now.h out of this header) reach it.
    // Not part of the API.
    void handlePacket(const uint8_t* mac, const uint8_t* data, int len);

    // Acknowledgement helpers, used by the dispatcher.
    void sendAck(uint8_t command);
    void sendError(uint8_t err);

private:
    void setState(RemoteLinkState s, uint32_t now);
    void applyChannelPolicy();
    void stopForSafety(const char* why);

    RemoteLinkState _state = REMOTE_DISCONNECTED;
    uint8_t  _peer[6] = {0};
    bool     _havePeer = false;
    uint32_t _lastRxMs = 0;
    uint32_t _lastStatusMs = 0;
    uint8_t  _txSeq = 0;
    bool     _initialised = false;
    // Set by the dispatcher while the remote is driving, so the
    // watchdog knows a stop would actually be meaningful.
    bool     _remoteDriving = false;
};

extern RemoteLink remoteLink;
