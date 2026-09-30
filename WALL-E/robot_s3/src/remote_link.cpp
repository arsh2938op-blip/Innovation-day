// ============================================================
//  Remote link — ESP-NOW transport to the ESP32-WROOM remote
//  ------------------------------------------------------------
//  See remote_link.h for the contract. This file is transport +
//  liveness only: it never touches motors, the OLED or behaviour.
// ============================================================
#include "remote_link.h"
#include "command_dispatch.h"
#include "log.h"
#include "motor_controller.h"

#include <esp_wifi.h>
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>

static const char* TAG = "S3";

RemoteLink remoteLink;

namespace {
// Broadcast address: used only to discover the remote's real MAC.
// Once we have heard from it we always answer that MAC unicast.
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW C callbacks. These cannot be members without dragging
// esp_now.h into the header, so they are free functions here.
void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    remoteLink.handlePacket(mac, data, len);
}

void onSend(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        LOGW(TAG, "send to %02X:%02X failed", mac[3], mac[4], mac[5]);
    }
}
}  // namespace

const char* RemoteLink::nameOf(RemoteLinkState s) {
    switch (s) {
        case REMOTE_DISCONNECTED: return "DISCONNECTED";
        case REMOTE_CONNECTING:   return "CONNECTING";
        case REMOTE_CONNECTED:    return "CONNECTED";
        case REMOTE_TIMEOUT:      return "TIMEOUT";
        default:                  return "?";
    }
}

const char* RemoteLink::statusText() const {
    switch (_state) {
        case REMOTE_CONNECTED: return "remote ok";
        case REMOTE_TIMEOUT:   return "remote lost";
        default:               return "no remote";
    }
}

// ------------------------------------------------------------
//  Bring-up
// ------------------------------------------------------------
void RemoteLink::applyChannelPolicy() {
    uint8_t channel = WALLE_REMOTE_FALLBACK_CHANNEL;
    if (WALLE_REMOTE_PIN_CHANNEL) {
        channel = (uint8_t)WALLE_REMOTE_PIN_CHANNEL;
    } else if (WiFi.isConnected()) {
        // Follow the router so the app link and the remote can share
        // one radio without a channel fight.
        channel = WiFi.channel();
    }
    if (channel == 0) channel = WALLE_REMOTE_FALLBACK_CHANNEL;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    LOGI(TAG, "ESP-NOW channel %u", channel);
}

bool RemoteLink::begin() {
    if (_initialised) return true;

    // ESP-NOW needs the radio in STA mode. If the robot is offline we
    // still start STA (with no SSID) so ESP-NOW works with no router.
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);

    // Never let the radio nap: power save adds tens of ms of latency,
    // which is the whole point of a handheld remote.
    esp_wifi_set_ps(WIFI_PS_NONE);

    applyChannelPolicy();

    if (esp_now_init() != ESP_OK) {
        LOGE(TAG, "esp_now_init failed - remote disabled");
        return false;
    }

    esp_now_register_recv_cb(::onReceive);
    esp_now_register_send_cb(::onSend);

    // Broadcast peer: lets a remote that has just powered on find us.
    // We reply unicast, so the remote only ever needs this one entry.
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcast, 6);
    peer.channel = 0;              // 0 = current channel
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;          // no key exchange at this layer
    if (esp_now_add_peer(&peer) != ESP_OK) {
        LOGE(TAG, "could not add broadcast peer");
        esp_now_deinit();
        return false;
    }

    _initialised = true;
    _state = REMOTE_CONNECTING;
    LOGI(TAG, "ESP-NOW ready - waiting for remote");
    return true;
}

// ------------------------------------------------------------
//  Inbound
// ------------------------------------------------------------
void RemoteLink::handlePacket(const uint8_t* mac, const uint8_t* data, int len) {
    const uint32_t now = millis();

    if (len != WALLE_PROTO_PACKET_SIZE) return;

    // Copy out of the driver buffer before validating, so a short or
    // hostile frame can never be read past its end.
    WallePacket p;
    memcpy(&p, data, sizeof(p));

    if (!walle_packet_valid(p, (uint8_t)len)) {
        LOGW(TAG, "bad packet (len %d)", len);
        return;
    }
    if (p.type != WALLE_MSG_COMMAND) {
        // Only controllers talk to the robot; ignore anything else.
        return;
    }

    // ---- learn / refresh the peer ----
    const bool newPeer = !_havePeer || memcmp(_peer, mac, 6) != 0;
    if (newPeer) {
        // Replace rather than accumulate peers, so a re-flashed remote
        // never leaves a stale MAC in the table.
        if (_havePeer) esp_now_del_peer(_peer);
        memcpy(_peer, mac, 6);
        _havePeer = true;
        LOGI(TAG, "Remote found: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    _lastRxMs = now;

    // Any valid packet means the remote is alive: CONNECTING and
    // TIMEOUT both promote straight to CONNECTED.
    if (_state != REMOTE_CONNECTED) setState(REMOTE_CONNECTED, now);

    // ---- hand the command to the dispatcher ----
    // Everything about what this MEANS lives on the other side of
    // this call, so a new controller needs no changes here.
    commands.dispatch(p.cmd, SOURCE_REMOTE);

    // HELLO and PING get a protocol-level reply here. Everything else
    // is acknowledged by the dispatcher itself, which fans the ack out
    // to every controller - sending one here as well would just give
    // the remote two acks per command.
    if (p.cmd == WALLE_CMD_PING) {
        sendStatus(WALLE_ST_PONG, (uint8_t)commands.robotState());
    } else if (p.cmd == WALLE_CMD_HELLO) {
        sendStatus(WALLE_ST_WELCOME, (uint8_t)commands.robotState());
    }
}

// ------------------------------------------------------------
//  Liveness + safety watchdog
// ------------------------------------------------------------
void RemoteLink::setState(RemoteLinkState s, uint32_t now) {
    if (_state == s) return;
    _state = s;
    _lastStatusMs = now;
    LOGI(TAG, "Remote %s", nameOf(s));

    // One event-driven status push on a real transition, so the
    // remote's LED/display reacts at once without any polling.
    sendStatus(WALLE_ST_REMOTE_STATE, (uint8_t)s);
}

void RemoteLink::stopForSafety(const char* why) {
    // emergencyStop() bypasses the soft-start ramp: the point of this
    // path is that the robot is stopped NOW.
    motors.emergencyStop();
    _remoteDriving = false;
    // Hand the control lock back so a later source can drive again.
    commands.onRemoteLost();
    LOGW(TAG, "Remote timeout -> STOP (%s)", why);
}

void RemoteLink::update(uint32_t now) {
    if (!_initialised) return;

    if (!_havePeer) {
        // Nothing ever heard from a remote yet.
        if (_state != REMOTE_DISCONNECTED && _state != REMOTE_CONNECTING) {
            setState(REMOTE_CONNECTING, now);
        }
        return;
    }

    const uint32_t silence = now - _lastRxMs;

    if (silence > REMOTE_TIMEOUT_MS) {
        // Lost the remote. Stop unconditionally: never leave the motors
        // running because a packet went missing.
        if (_state != REMOTE_TIMEOUT) {
            setState(REMOTE_TIMEOUT, now);
        }
        // onRemoteLost() is idempotent and also runs when we were not
        // driving, so the control lock is always released on a drop.
        commands.onRemoteLost();
        if (_remoteDriving) stopForSafety("link lost");
        return;
    }

    // Back within the window: the remote is alive again.
    if (_state == REMOTE_TIMEOUT || _state == REMOTE_DISCONNECTED) {
        setState(REMOTE_CONNECTED, now);
    }

    // Periodic floor on the reverse direction, so a remote that only
    // listens still learns the robot state within about a second.
    if (now - _lastStatusMs >= REMOTE_STATUS_INTERVAL_MS) {
        _lastStatusMs = now;
        sendStatus(WALLE_ST_ROBOT_STATE, (uint8_t)commands.robotState());
    }
}

// ------------------------------------------------------------
//  Outbound
// ------------------------------------------------------------
void RemoteLink::sendStatus(uint8_t status, uint8_t value, uint16_t arg) {
    if (!_initialised || !_havePeer) return;   // nobody listening yet

    const WallePacket p = walle_make_packet(WALLE_MSG_STATUS, status, value, 0, arg, _txSeq++);

    esp_now_send(_peer, (const uint8_t*)&p, sizeof(p));
}

// One small frame per command. The remote uses it to light its LED
// and to name the command it just ran - no streaming, no flood.
void RemoteLink::sendAck(uint8_t command) {
    sendStatus(WALLE_ST_ACK, command);
}

void RemoteLink::sendError(uint8_t err) {
    LOGW(TAG, "reporting error %u to remote", err);
    sendStatus(WALLE_ST_ERROR, err);
}
