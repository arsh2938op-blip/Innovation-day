// ============================================================
//  WALL-E remote controller (ESP32-WROOM)
//  ------------------------------------------------------------
//  A handheld, battery-powered remote for the WALL-E robot. It
//  reads buttons, translates them into WALL-E commands and sends
//  them to the ESP32-S3 over ESP-NOW.
//
//  It contains NO robot logic. There is no motor code here, no
//  behaviour, no OLED face: the robot executes everything and owns
//  the motors. The remote only names intents
//  (shared/walle_protocol.h -> WalleCommand).
//
//  main.cpp is deliberately small:
//    setup()  -> buttons, wireless link, optional LED
//    loop()   -> sample buttons, act on edges, refresh held presses
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "walle_protocol.h"
#include "remote_input.h"
#include "remote_link.h"

static const char* TAG = "WROOM";

// The direction most recently sent, so a held button re-sends the
// same command instead of re-deriving it and risking a flip.
static uint8_t  gHeldDirection = WALLE_CMD_NONE;
static uint32_t gLastDirectionMs = 0;
static bool     gAutonomous = true;

// Optional link LED. -1 disables it.
static void updateLed() {
    if (REMOTE_PIN_IS_UNSET(REMOTE_LED_PIN)) return;

    bool on;
    switch (remoteLink.status()) {
        case RS_CONNECTED: on = true;  break;   // solid
        case RS_LOST:      on = false; break;   // off
        default:           on = true;  break;   // searching: solid, we
    }                                         // have nothing better
    digitalWrite(REMOTE_LED_PIN, on ? HIGH : LOW);
}

void setup() {
    Serial.begin(REMOTE_SERIAL_SPEED);
    delay(150);
    Serial.println();
    Serial.println("=== WALL-E remote (ESP32-WROOM) ===");

    // ---- buttons ----
    remoteInput.begin();

    // ---- optional LED ----
    if (!REMOTE_PIN_IS_UNSET(REMOTE_LED_PIN)) {
        pinMode(REMOTE_LED_PIN, OUTPUT);
    }

    // ---- wireless ----
    if (!remoteLink.begin()) {
        Serial.println("[WROOM] FATAL: could not start ESP-NOW");
    }

    // ---- honest boot report ----
    if (remoteInput.anyConfigured()) {
        Serial.printf("[WROOM] Buttons on GPIO: %s\n", remoteInput.describe().c_str());
    } else {
        Serial.println("[WROOM] NO BUTTONS CONFIGURED");
        Serial.println("[WROOM] Every pin is still TODO_CONFIGURE_REMOTE_GPIO (-1).");
        Serial.println("[WROOM] Set them in remote_wroom/include/config.h and flash again.");
    }
    Serial.println("[WROOM] Ready - press a button on the remote");
}

void loop() {
    const uint32_t now = millis();

    remoteInput.update(now);
    remoteLink.update(now);
    updateLed();

    // ---- STOP beats everything, always ----
    // Checked first and unconditionally, so an emergency stop can
    // never be lost to a direction button that is also held.
    if (remoteInput.justPressed(RBTN_STOP)) {
        remoteLink.sendCommand(WALLE_CMD_STOP, false);
        gHeldDirection = WALLE_CMD_NONE;
    }

    // ---- dance (tap) ----
    if (remoteInput.justPressed(RBTN_DANCE)) {
        remoteLink.sendCommand(WALLE_CMD_DANCE, false);
    }

    // ---- autonomy toggle (momentary, self-releasing) ----
    // A toggle on a latching switch would need edge detection per
    // direction; a momentary button is simpler and matches STOP.
    if (remoteInput.justReleased(RBTN_MODE)) {
        gAutonomous = !gAutonomous;
        remoteLink.sendCommand(gAutonomous ? WALLE_CMD_AUTONOMOUS_ON
                                           : WALLE_CMD_AUTONOMOUS_OFF, false);
        Serial.printf("[WROOM] Autonomous %s\n", gAutonomous ? "ON" : "OFF");
    }

    // ---- expressions ----
    if (remoteInput.justPressed(RBTN_EXPR)) {
        remoteLink.sendCommand(WALLE_CMD_EXPR_HAPPY, false);
    }
    if (remoteInput.justPressed(RBTN_SURPRISE)) {
        remoteLink.sendCommand(WALLE_CMD_EXPR_SURPRISED, false);
    }

    // ---- "say something out loud" ----
// A tap asks the ROBOT to think of a line and speak it through its own
// speaker. The remote never carries the Gemini key or any audio.
if (remoteInput.justPressed(RBTN_TALK)) {
    remoteLink.sendCommand(WALLE_CMD_TALK, false);
    Serial.println("[WROOM] Asked the robot to speak");
}

// ---- "turn around" ----
// A tap, not a hold: the robot times the 180 degree pivot itself and
// stops on its own, so there is no key-repeat and no lost STOP.
// The robot's cliff sensor still has veto power over the whole turn.
if (remoteInput.justPressed(RBTN_TURN)) {
    remoteLink.sendCommand(WALLE_CMD_TURN_AROUND, false);
    Serial.println("[WROOM] Asked the robot to turn around");
}

// ---- movement: send on the press edge ----
    const uint8_t direction = remoteInput.heldDirection();

    if (direction != WALLE_CMD_NONE) {
        if (direction != gHeldDirection) {
            // A new direction was pressed.
            gHeldDirection = direction;
            gLastDirectionMs = now;
            remoteLink.sendCommand(direction, true);
        } else if ((now - gLastDirectionMs) >= REMOTE_HOLD_REPEAT_MS) {
            // Still held: refresh. This is what stops the robot's
            // REMOTE_TIMEOUT_MS watchdog from firing mid-drive, and
            // it repairs any packet lost to interference.
            gLastDirectionMs = now;
            remoteLink.sendCommand(direction, true);
        }
    } else if (gHeldDirection != WALLE_CMD_NONE) {
        // Every direction released: tell the robot to stop.
        gHeldDirection = WALLE_CMD_NONE;
        remoteLink.sendCommand(WALLE_CMD_STOP, false);
    }

    delay(2);   // keeps the ESP-NOW task fed without burning the radio
}
