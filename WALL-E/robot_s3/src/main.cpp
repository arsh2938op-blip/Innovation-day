// ============================================================
//  WALL-E - Innovation Day AI Robot (ESP32-S3)
//  ------------------------------------------------------------
//  main.cpp is intentionally small:
//    setup()  -> boot safely, bring subsystems up
//    loop()   -> tick the safety guard, then everything else
//
//  All the real work lives in the modules in src/.
//
//  WHAT THIS FIRMWARE DOES
//  -----------------------
//  * Four motors through a motor DRIVER MODULE. The S3 only drives
//    driver inputs; it never carries motor current.
//  * A HC-SR04 pointed down at 45 degrees that stops WALL-E at the
//    edge of a table instead of letting it fall off.
//  * Motion primitives: forward, N steps, turn around, timed moves -
//    all self-ending and all interruptible by the safety guard.
//  * Motors, an OLED face, a state machine, autonomous behaviour and
//    a dance routine.
//  * TTS: Gemini replies are spoken out loud through the I2S amplifier.
//    The audio stack is only loaded while something is actually being
//    said. Speech-to-Text is NOT here - the robot has no microphone;
//    the app does the speech recognition and sends the words as text.
//  * The personality is the APP's: it sends a persona after connecting
//    (src/persona.*), so who WALL-E is can change without a reflash.
//  * ONE controller: the companion app, over TCP (src/app_link.*).
//    There is no handheld radio remote and no ESP-NOW any more.
//    Everything lands in the same command_dispatch.cpp, which is the
//    only place a command means anything.
//
//  LOOP ORDER IS A SAFETY DECISION, NOT A STYLE CHOICE
//  --------------------------------------------------
//  1. safety   - may the wheels move at all? A cliff must be answered
//                before anything else gets a chance to drive.
//  2. sensor   - refresh the cliff reading.
//  3. links    - radio remote, app, stale-motion backstop.
//  4. maneuver - advance a timed maneuver, or abort it.
//  5. motors   - soft-start ramp.
//  6. audio    - drain the tail of any speech, then unload it.
//  7. state    - transitions and per-state motor safety.
//  8. behavior - decide what to do next.
//  9. oled     - redraw the face.
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
#include "app_link.h"
#include "command_dispatch.h"
#include "cliff_sensor.h"
#include "maneuver.h"
#include "persona.h"
#include "safety.h"
#include "audio_output.h"
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

    // Validates the I2S pins only. The driver and the ring buffer are
    // brought up on the first real sentence and released afterwards.
    speaker.begin();

    // ---------- 3. behaviour ----------
    gRobotSM = &stateMachine;
    stateMachine.begin();
    dance.begin();
    maneuver.begin();

    // ---------- 4. sensors ----------
    // The cliff sensor comes up before the guard, because the guard
    // needs it and because a bad mount angle must be visible on the
    // OLED from the very first second.
    cliffSensor.begin();
    safety.begin(&cliffSensor);

    behavior.begin(&stateMachine);
    // The real detector now exists: the HC-SR04 sees the table edge.
    behavior.setObstacleDetector(&cliffSensor);

    // ---------- 5. Wi-Fi (never blocks forever) ----------
    wifi.begin();

    // ---------- 6. the app link: the only controller ----------
#if WALLE_ENABLE_APP_LINK
    if (appLink.begin()) {
        LOGI("S3", "App link ready - connect a TCP client to port %d", APP_TCP_PORT);
    } else {
        LOGW("S3", "App link unavailable - serial control only");
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

    // 1. SAFETY FIRST. A drop is answered before anything else can
    //    decide to drive.
    safety.update(now);

    // 2. sensors
    cliffSensor.update(now);

    // 3. the app (and the console)
#if WALLE_ENABLE_APP_LINK
    appLink.update(now);           // TCP accept / drain / watchdog
#endif
    commands.tick(now);            // stale-motion backstop

    // 4. a timed maneuver advances, or is aborted by the guard
    maneuver.update(now);

    // 5. motors + audio
    motors.update();               // soft-start ramp
    speaker.pump();                // drain the tail, then unload the audio

    // 6/7. state machine and behaviour
    stateMachine.update(now);      // transitions + per-state motor safety
    behavior.update(now);          // decide what to do next

    // 8/9. face and console
    oled.update(now);
    pollSerialConsole();
}