// ============================================================
//  App link implementation - TCP transport for the companion app
//  See app_link.h for the framing rules.
// ============================================================
#include "app_link.h"
#include "command_dispatch.h"
#include "behavior.h"
#include "log.h"
#include "motor_controller.h"
#include "safety.h"

static const char* TAG = "APP";

AppLink appLink;

const char* AppLink::nameOf(AppLinkState s) {
    switch (s) {
        case APP_OFFLINE:   return "OFFLINE";
        case APP_LISTENING: return "LISTENING";
        case APP_CONNECTED: return "CONNECTED";
        default:            return "?";
    }
}

// ------------------------------------------------------------
bool AppLink::begin() {
#if WALLE_ENABLE_APP_LINK
    if (_started) return true;

    _server.begin(APP_TCP_PORT);
    // Nagle batching would add latency to a 10-byte command; the app is
    // interactive, so turn it off.
    _server.setNoDelay(true);

    _started = true;
    _state = APP_LISTENING;
    LOGI(TAG, "TCP server on port %u", (unsigned)APP_TCP_PORT);
    return true;
#else
    LOGW(TAG, "Disabled in config.h (WALLE_ENABLE_APP_LINK=0)");
    return false;
#endif
}

void AppLink::closeClient() {
    if (_client) {
        _client.stop();
        LOGI(TAG, "Client disconnected");
    }
    _client = WiFiClient();
    _rxLen = 0;
    _rxWant = WALLE_PROTO_PACKET_SIZE;
    _textLen = 0;
    _driving = false;
    if (_state == APP_CONNECTED) _state = APP_LISTENING;

    // Hand the control lock back, whatever we were doing.
    commands.onAppLost();
}

// ------------------------------------------------------------
//  Outbound
// ------------------------------------------------------------
bool AppLink::writeRaw(const uint8_t* data, size_t len) {
    if (!_client || _client.connected() == false) return false;

    // Respect the socket buffer instead of blocking: a phone on a slow
    // link must never stall the robot's main loop.
    size_t written = 0;
    while (written < len) {
        const int room = _client.availableForWrite();
        if (room <= 0) { delay(2); continue; }

        const size_t chunk = min((size_t)room, len - written);
        const size_t n = _client.write(data + written, chunk);
        if (n == 0) {
            if (!_client.connected()) { closeClient(); return false; }
            delay(2);
            continue;
        }
        written += n;
    }
    return true;
}

void AppLink::sendStatus(uint8_t status, uint8_t value, uint16_t arg) {
    if (!_client || !_client.connected()) return;
    const WallePacket p = walle_make_packet(WALLE_MSG_STATUS, status, value, 0, arg, _txSeq++);
    writeRaw((const uint8_t*)&p, sizeof(p));
}

void AppLink::sendAck(uint8_t command) {
    sendStatus(WALLE_ST_ACK, command);
}

void AppLink::sendError(uint8_t err) {
    LOGW(TAG, "reporting error %u to app", err);
    sendStatus(WALLE_ST_ERROR, err);
}

void AppLink::sendText(uint8_t op, const char* text) {
    if (!_client || !_client.connected() || !text) return;

    const size_t len = strlen(text);
    if (len == 0 || len > WALLE_TEXT_MAX) {
        LOGW(TAG, "reply too long (%u bytes), sending truncated", (unsigned)len);
    }
    const uint16_t n = (uint16_t)(len > WALLE_TEXT_MAX ? WALLE_TEXT_MAX : len);

    WalleTextHeader h;
    h.magic    = WALLE_PROTO_MAGIC;
    h.version  = WALLE_PROTO_VERSION;
    h.type     = WALLE_MSG_TEXT;
    h.op       = op;
    h.flags    = 0;
    h.len      = n;
    h.reserved = 0;

    uint8_t frame[WALLE_PROTO_TEXT_HEADER + WALLE_TEXT_MAX];
    memcpy(frame, &h, sizeof(h));
    memcpy(frame + sizeof(h), text, n);

    writeRaw(frame, sizeof(h) + n);
    LOGI(TAG, "Sent reply (%u bytes)", (unsigned)n);
}

// ------------------------------------------------------------
//  Inbound
// ------------------------------------------------------------
void AppLink::handleFixedPacket() {
    if (_rxLen != WALLE_PROTO_PACKET_SIZE) return;

    WallePacket p;
    memcpy(&p, _buf, sizeof(p));

    if (!walle_packet_valid(p, (uint8_t)_rxLen)) {
        LOGW(TAG, "bad packet");
        sendError(WALLE_ERR_BAD_PACKET);
        return;
    }
    if (p.type != WALLE_MSG_COMMAND) return;   // only controllers talk to us

    _lastRxMs = millis();

#if APP_LINK_LOG_PACKETS
    LOGI(TAG, "rx cmd=%s val=%u arg=%u",
         walle_command_name(p.cmd), p.value, p.arg);
#endif

    // Exactly the same path as the radio remote.
    commands.dispatch(p.cmd, SOURCE_APP);

    // The dispatcher already sends its own ack/error through
    // CommandDispatcher::notify(), so nothing is added here. One reply
    // per command, never a stream.
}

void AppLink::handleTextFrame(uint8_t op, const char* text) {
    _lastRxMs = millis();

    switch (op) {
        case WALLE_OP_ASK: {
            LOGI(TAG, "ask: %s", text);
            behavior.requestChat(String(text));
            break;
        }
        case WALLE_OP_SPEAK: {
            LOGI(TAG, "speak: %s", text);
            behavior.requestSpeak(String(text));
            break;
        }
        default:
            LOGW(TAG, "unknown text op %u", op);
            sendError(WALLE_ERR_UNKNOWN_CMD);
            break;
    }
}

// ------------------------------------------------------------
//  Main loop
// ------------------------------------------------------------
void AppLink::update(uint32_t now) {
#if WALLE_ENABLE_APP_LINK
    if (!_started) return;

    // ---- accept ----
    if (!_client || !_client.connected()) {
        if (_client) closeClient();

        WiFiClient incoming = _server.available();
        if (incoming) {
            _client = incoming;
            _client.setNoDelay(true);
            _state = APP_CONNECTED;
            _lastRxMs = now;
            _rxLen = 0;
            _rxWant = WALLE_PROTO_PACKET_SIZE;
            _watchdogFired = false;
            LOGI(TAG, "App connected (%s)",
                 _client.remoteIP().toString().c_str());
            // Tell the app what it is talking to.
            sendStatus(WALLE_ST_WELCOME, (uint8_t)commands.robotState());
        } else if (_state != APP_LISTENING) {
            _state = APP_LISTENING;
        }
    }

    if (!_client || !_client.connected()) return;

    // ---- drain the socket ----
    while (_client.available()) {
        const int c = _client.read();
        if (c < 0) break;

        if (_rxLen == 0) {
            // Resynchronise on the magic byte: anything else is noise.
            if (c != WALLE_PROTO_MAGIC) continue;
            _buf[_rxLen++] = (uint8_t)c;
            _rxWant = WALLE_PROTO_PACKET_SIZE;
            continue;
        }

        if (_rxLen >= sizeof(_buf)) {   // defensive: never overflow
            LOGW(TAG, "frame overflow, resetting");
            _rxLen = 0;
            _rxWant = WALLE_PROTO_PACKET_SIZE;
            continue;
        }

        _buf[_rxLen++] = (uint8_t)c;

        // A fixed packet is complete at 10 bytes.
        if (_rxWant == WALLE_PROTO_PACKET_SIZE) {
            if (_rxLen >= WALLE_PROTO_PACKET_SIZE) {
                _buf[WALLE_PROTO_PACKET_SIZE] = 0;
                handleFixedPacket();
                _rxLen = 0;
            }
            continue;
        }

        // A text frame: at the 8 byte header we learn how long it is.
        if (_rxLen == WALLE_PROTO_TEXT_HEADER) {
            WalleTextHeader h;
            memcpy(&h, _buf, sizeof(h));

            if (!walle_text_valid(h, h.op)) {
                LOGW(TAG, "bad text header (op=%u len=%u)", h.op, h.len);
                sendError(WALLE_ERR_BAD_PACKET);
                _rxLen = 0;
                _rxWant = WALLE_PROTO_PACKET_SIZE;
                continue;
            }
            _textOp = h.op;
            _textLen = h.len;
            _rxWant = (uint16_t)(WALLE_PROTO_TEXT_HEADER + h.len);
        }

        if (_rxLen >= _rxWant) {
            _buf[_rxWant] = 0;
            // Skip the header, hand over a NUL-terminated C string.
            handleTextFrame(_textOp, (const char*)_buf + WALLE_PROTO_TEXT_HEADER);
            _rxLen = 0;
            _rxWant = WALLE_PROTO_PACKET_SIZE;
            _textLen = 0;
        }
    }

    // ---- safety watchdog ----
    // Same rule as the radio remote: a controller that stops talking
    // while it is driving must not leave the wheels turning.
    if (_driving && (now - _lastRxMs) > APP_TIMEOUT_MS) {
        if (!_watchdogFired) {
            _watchdogFired = true;
            LOGW(TAG, "App went quiet while driving -> STOP");
            motors.emergencyStop();
            safety.setExternallyDriven(false);
            commands.onAppLost();
            // Tell the app exactly why, so it can show a useful message
            // and start its keepalive again.
            sendError(WALLE_ERR_LINK_TIMEOUT);
        }
    }

    // ---- keepalive floor ----
    if (now - _lastStatusMs >= APP_STATUS_INTERVAL_MS) {
        _lastStatusMs = now;
        sendStatus(WALLE_ST_ROBOT_STATE, (uint8_t)commands.robotState());
    }
#endif
}

void AppLink::onDisconnect() { closeClient(); }