// ============================================================
//  App link - the phone / web app as a second controller
//  ------------------------------------------------------------
//  The app is NOT a special case. It speaks the EXACT same
//  WallePacket as the ESP-NOW remote, over TCP instead of radio, and
//  every command lands in the same command_dispatch.cpp. There is no
//  app-specific behaviour anywhere in the firmware, which is exactly
//  why the app can never drift from the remote's rules.
//
//  WHY TCP
//  A phone cannot speak ESP-NOW, and the robot is already on the same
//  Wi-Fi network the phone is on. A TCP server is one small object,
//  needs no libraries, and - unlike ESP-NOW - can carry the
//  variable-length text frames that ask/speak need.
//
//  HOW STREAM FRAMING WORKS HERE
//  -----------------------------
//  TCP is a byte stream with no message boundaries: one read can
//  return half a packet, or three packets at once. So bytes are fed
//  into a fixed buffer and only acted on once a WHOLE frame is
//  present:
//
//      fixed   10 bytes, exactly like the radio
//      text    8 byte header + `len` payload bytes
//
//  A leading magic byte is required on every frame, and anything that
//  is not 0xA5 is skipped. That gives free resynchronisation: if the
//  app reconnects mid-frame, or a stray byte arrives, the link
//  recovers on the next real packet instead of wedging forever.
//
//  WATCHDOG
//  This link owns the same "stop if the controller goes quiet" rule as
//  the remote. While the app is driving, no packets for
//  APP_TIMEOUT_MS stops the robot. Link loss must never leave a wheel
//  turning, and that guarantee cannot depend on which controller sent
//  the command.
// ============================================================
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "walle_protocol.h"

enum AppLinkState : uint8_t {
    APP_OFFLINE = 0,     // server not started (no Wi-Fi)
    APP_LISTENING,        // waiting for the app to connect
    APP_CONNECTED,        // a client is attached
};

class AppLink {
public:
    // Starts the server. Returns false only if the port could not be
    // opened, which basically never happens.
    bool begin();

    // Call every loop(). Accepts a client, drains its socket, enforces
    // the timeout watchdog and flushes pending outbound data.
    void update(uint32_t now);

    // ---- state ----
    AppLinkState state() const { return _state; }
    static const char* nameOf(AppLinkState s);
    bool connected() const { return _state == APP_CONNECTED; }

    // Set by the dispatcher while the app is driving, so the watchdog
    // knows a stop would actually be meaningful.
    void setDriving(bool driving) { _driving = driving; }
    bool driving() const { return _driving; }

    // ---- outbound ----
    void sendStatus(uint8_t status, uint8_t value = 0, uint16_t arg = 0);
    void sendAck(uint8_t command);
    void sendError(uint8_t err);
    void sendText(uint8_t op, const char* text);

    // Stop being the controlling app. Called when the app goes away so
    // the next controller may take the wheels.
    void onDisconnect();

private:
    void closeClient();
    void handleFixedPacket();
    void handleTextFrame(uint8_t op, const char* text);
    bool writeRaw(const uint8_t* data, size_t len);

    WiFiServer  _server;
    WiFiClient  _client;

    AppLinkState _state = APP_OFFLINE;
    bool     _started = false;
    bool     _driving = false;
    uint32_t _lastRxMs = 0;
    uint32_t _lastStatusMs = 0;
    uint8_t  _txSeq = 0;

    // ---- stream reassembly ----
    // Big enough for the largest legal frame: 8 byte text header plus
    // the maximum payload.
    uint8_t  _buf[WALLE_PROTO_TEXT_HEADER + WALLE_TEXT_MAX + 1] = {0};
    uint16_t _rxLen = 0;
    uint16_t _rxWant = WALLE_PROTO_PACKET_SIZE;
    uint16_t _textLen = 0;
    uint8_t  _textOp = 0;

    // Set when a handbrake is needed: the app is driving but has gone
    // quiet past APP_TIMEOUT_MS.
    bool     _watchdogFired = false;
};

extern AppLink appLink;