// ============================================================
//  WALL-E - Innovation Day AI Robot (ESP32-S3)
//  ------------------------------------------------------------
//  main.cpp is intentionally small:
//    setup()  -> boot safely, bring subsystems up
//    loop()   -> tick Wi-Fi, remote link, motors, OLED, state
//
//  All the real work lives in the modules in src/.
//
//  WHAT CHANGED with the S3 rework
//  -------------------------------
//  * The board is an ESP32-S3 (motor/OLED/Gemini/app link all moved
//    here, see platformio.ini).
//  * STT and TTS are GONE: no microphone, no speaker, no audio API
//    clients, no voice states. Gemini text AI is untouched and
//    WALL-E's replies now show on the OLED and in the app.
//  * A wireless ESP32-WROOM remote can drive the robot. All of its
//    commands land in command_dispatch.cpp, the same interface the
//    serial console and the companion app use.
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "log.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "wifi_manager.h"
#include "robot_state.h"
#include "behavior.h"
#include "dance.h"
#include "camera_manager.h"
#include "remote_link.h"
#include "command_dispatch.h"
#include "hardware_test.h"

static RobotStateMachine stateMachine;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== WALL-E booting (ESP32-S3) ===");

    // ---------- 1. SAFETY FIRST: motors stopped before anything else ----------
    motors.begin();
    motors.emergencyStop();
    delay(BOOT_MOTOR_STOP_MS);

    // ---------- 2. peripherals ----------
    oled.begin();
    oled.splash("WALL-E", "booting...");
    oled.setExpression(EXPR_BOOT);

    cameraManager.begin();
    // (no mic.begin() / speaker.begin() - STT and TTS were removed)

    // ---------- 3. behaviour ----------
    gRobotSM = &stateMachine;
    stateMachine.begin();
    dance.begin();
    behavior.begin(&stateMachine);
    behavior.setObstacleDetector(nullptr);   // TODO: implement a real detector

    // ---------- 4. Wi-Fi (never blocks forever) ----------
    wifi.begin();

    // ---------- 5. wireless remote (ESP-NOW) ----------
#if WALLE_ENABLE_REMOTE
    if (remoteLink.begin()) {
        oled.setStatus("remote?");
    } else {
        LOGW("S3", "Remote link unavailable - serial control only");
    }
#endif

#if WALLE_TEST_MODE
    // Interactive hardware test - the user picks what to check.
    oled.splash("HARDWARE TEST", "serial menu");
    runHardwareTestMode();
#endif

    oled.setExpression(EXPR_IDLE);
    oled.setStatus("");
    LOGI("S3", "Ready - type 'help' on serial");
    printConsoleHelp();
}

void loop() {
    const uint32_t now = millis();

    wifi.update(now);              // non-blocking connect / reconnect
#if WALLE_ENABLE_REMOTE
    remoteLink.update(now);        // ESP-NOW RX, timeout watchdog, status
    commands.tick(now);            // stale-motion backstop

    // Show the link state on the face, but only when it CHANGES.
    // Rewriting the same status string every loop would fight the
    // behaviour layer for the OLED status line and flicker it.
    static RemoteLinkState lastRemoteState = REMOTE_DISCONNECTED;
    if (remoteLink.state() != lastRemoteState) {
        lastRemoteState = remoteLink.state();
        oled.setStatus(remoteLink.statusText());
    }
#endif
    motors.update();               // soft-start ramp
    stateMachine.update(now);      // state transitions + motor safety
    behavior.update(now);          // decide what to do next
    oled.update(now);              // redraw the face
    pollSerialConsole();           // manual commands
}
