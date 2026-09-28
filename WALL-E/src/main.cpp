// ============================================================
//  WALL-E - Innovation Day AI Robot (ESP32-C3)
//  ------------------------------------------------------------
//  main.cpp is intentionally small:
//    setup()  -> boot safely, bring subsystems up
//    loop()   -> tick Wi-Fi, motors, OLED, speaker, state machine
//
//  All the real work lives in the modules in src/.
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
#include "audio_input.h"
#include "audio_output.h"
#include "hardware_test.h"

static RobotStateMachine stateMachine;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== WALL-E booting ===");

    // ---------- 1. SAFETY FIRST: motors stopped before anything else ----------
    motors.begin();
    motors.emergencyStop();
    delay(BOOT_MOTOR_STOP_MS);

    // ---------- 2. peripherals ----------
    oled.begin();
    oled.splash("WALL-E", "booting...");
    oled.setExpression(EXPR_BOOT);

    cameraManager.begin();
    mic.begin();
    speaker.begin();

    // ---------- 3. behaviour ----------
    gRobotSM = &stateMachine;
    stateMachine.begin();
    dance.begin();
    behavior.begin(&stateMachine);
    behavior.setObstacleDetector(nullptr);   // TODO: implement a real detector

    // ---------- 4. Wi-Fi (never blocks forever) ----------
    wifi.begin();

#if WALLE_TEST_MODE
    // Interactive hardware test - the user picks what to check.
    oled.splash("HARDWARE TEST", "serial menu");
    runHardwareTestMode();
#endif

    oled.setExpression(EXPR_IDLE);
    LOGI("BOOT", "Ready - type 'help' on serial");
    printConsoleHelp();
}

void loop() {
    const uint32_t now = millis();

    wifi.update(now);          // non-blocking connect / reconnect
    motors.update();           // soft-start ramp
    speaker.pump();            // stream queued TTS audio
    stateMachine.update(now);  // state transitions + motor safety
    behavior.update(now);      // decide what to do next
    oled.update(now);          // redraw the face
    pollSerialConsole();       // manual commands
}
