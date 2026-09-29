// ============================================================
//  Wi-Fi connection management with automatic reconnect
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class WifiManager {
public:
    void begin();

    // non-blocking: kicks off a connect attempt and returns
    void update(uint32_t now);

    bool connected() const { return _connected; }
    bool configured() const { return _hasCredentials; }
    int  rssi() const { return _rssi; }

private:
    bool _connected = false;
    bool _hasCredentials = false;
    bool _connecting = false;
    uint32_t _nextAttemptMs = 0;
    uint32_t _connectedSinceMs = 0;
    int  _rssi = 0;

    void startConnect();
    void onConnect();
    void onDisconnect();
};

extern WifiManager wifi;
