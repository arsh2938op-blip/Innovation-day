#include "wifi_manager.h"
#include "log.h"
#include <WiFi.h>

static const char* TAG = "WIFI";

WifiManager wifi;

void WifiManager::begin() {
    _hasCredentials = (strlen(WALLE_WIFI_SSID) > 0);
    if (!_hasCredentials) {
        LOGW(TAG, "No SSID in secrets.h - starting in offline mode");
        WiFi.mode(WIFI_OFF);
        WiFi.disconnect(true);
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // keep latency low for small HTTPS requests
    WiFi.setAutoReconnect(true);
    _nextAttemptMs = 0;            // connect on the first update()
}

void WifiManager::update(uint32_t now) {
    if (!_hasCredentials) return;

    if (WiFi.status() == WL_CONNECTED) {
        if (!_connected) onConnect();
        return;
    }

    if (_connected) onDisconnect();

    if (_connecting) {
        if (now - _connectedSinceMs > WIFI_CONNECT_TIMEOUT_MS) {
            LOGE(TAG, "Connect timeout");
            WiFi.disconnect(true);
            _connecting = false;
            _nextAttemptMs = now + WIFI_RETRY_INTERVAL_MS;
        }
        return;
    }

    if (now >= _nextAttemptMs) startConnect();
}

void WifiManager::startConnect() {
    LOGI(TAG, "Connecting...");
    _connecting = true;
    _connectedSinceMs = millis();
    WiFi.disconnect(true);
    WiFi.begin(WALLE_WIFI_SSID, WALLE_WIFI_PASSWORD);
    _nextAttemptMs = millis() + WIFI_RETRY_INTERVAL_MS;
}

void WifiManager::onConnect() {
    _connected = true;
    _connecting = false;
    _rssi = WiFi.RSSI();
    LOGI(TAG, "Connected  ip=%s  rssi=%d dBm", WiFi.localIP().toString().c_str(), _rssi);
}

void WifiManager::onDisconnect() {
    _connected = false;
    _connecting = false;
    LOGW(TAG, "Disconnected - offline mode");
}
