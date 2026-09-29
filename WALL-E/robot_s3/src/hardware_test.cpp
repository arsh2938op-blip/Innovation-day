// ============================================================
//  Hardware test mode + the serial console
//  ------------------------------------------------------------
//  Two entry points:
//    runHardwareTestMode() - interactive menu, one part at a time
//    pollSerialConsole()   - normal build, type commands over USB
//
//  The console does NOT talk to the motors or the behaviour layer
//  directly any more: it goes through command_dispatch.cpp, the
//  exact same path the wireless remote uses. That is deliberate -
//  it means the serial console and the remote cannot disagree about
//  priority, and it keeps one implementation of every command.
// ============================================================
#include "hardware_test.h"
#include "config.h"
#include "command_dispatch.h"
#include "log.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "gemini_client.h"
#include "camera_manager.h"
#include "wifi_manager.h"
#include "remote_link.h"
#include "robot_state.h"
#include "behavior.h"
#include "dance.h"

static const char* TAG = "TEST";

extern GeminiClient gemini;

static int readMenuChoice() {
    while (!Serial.available()) delay(20);
    String line = Serial.readStringUntil('\n');
    line.trim();
    return line.toInt();
}

static void pressEnter(const char* what) {
    Serial.printf("\n%s finished. Press ENTER...\n", what);
    while (!Serial.available()) delay(20);
    Serial.readStringUntil('\n');
}

// ------------------------------------------------------------
//  individual tests
// ------------------------------------------------------------
static void testOled() {
    Serial.println("[TEST] Cycling all expressions...");
    oled.setStatus("oled test");
    for (int e = 0; e < EXPR_COUNT; e++) {
        oled.setExpression((Expression)e);
        Serial.printf("   %s\n", OledDisplay::nameOf((Expression)e));
        delay(1200);
    }
    oled.setExpression(EXPR_IDLE);
    pressEnter("OLED");
}

static void testMotor(uint8_t index) {
    Serial.printf("[TEST] Motor %u ...\n", index);
    if (!motors.motorsConfigured()) { Serial.println("      Motors NOT configured (TODO_CONFIGURE_GPIO in config.h)"); pressEnter("MOTOR"); return; }
    static const int16_t seq[] = { 120, -120, 0 };
    for (int16_t s : seq) {
        motors.emergencyStop();
        motors.setMotor(index, s);
        delay(700);
    }
    motors.emergencyStop();
    pressEnter("MOTOR");
}

static void testCamera() {
    uint8_t* jpg = nullptr;
    size_t len = 0;
    cameraManager.begin();
    cameraManager.capture(&jpg, &len);
    if (!jpg) {
        Serial.println("[TEST] No camera. Expected: see README camera section.");
    } else {
        Serial.printf("[TEST] Frame captured, %u bytes\n", (unsigned)len);
        cameraManager.release(jpg);
    }
    pressEnter("CAM");
}

static void testWifi() {
    Serial.printf("[TEST] Wi-Fi SSID: %s\n", strlen(WALLE_WIFI_SSID) ? WALLE_WIFI_SSID : "(not set)");
    wifi.begin();
    uint32_t start = millis();
    while (!wifi.connected() && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        wifi.update(millis());
        delay(50);
    }
    Serial.printf("[TEST] %s\n", wifi.connected() ? "CONNECTED" : "FAILED (robot still works offline)");
    pressEnter("WIFI");
}

static void testGemini() {
    if (!gemini.ping()) Serial.println("[TEST] Gemini unreachable");
    else {
        String reply;
        if (gemini.ask("Say hello in 5 words.", &reply)) Serial.printf("[TEST] Reply: %s\n", reply.c_str());
    }
    pressEnter("GEMINI");
}

static void testRemote() {
    Serial.println("[TEST] Waiting 15 s for a remote to press a button...");
    Serial.println("[TEST] (the remote must be flashed and powered on)");
    const uint32_t start = millis();
    bool seen = false;
    while (millis() - start < 15000) {
        remoteLink.update(millis());
        if (remoteLink.state() == REMOTE_CONNECTED) { seen = true; break; }
        delay(20);
    }
    Serial.printf("[TEST] Remote link: %s\n", remoteLink.nameOf(remoteLink.state()));
    if (!seen) Serial.println("[TEST] No remote heard. Check the channel matches and both are flashed.");
    pressEnter("REMOTE");
}

// ------------------------------------------------------------
void runHardwareTestMode() {
    motors.begin();
    motors.emergencyStop();
    oled.begin();
    oled.splash("HARDWARE TEST", "Pick a test");
    Serial.println();
    Serial.println("============= WALL-E HARDWARE TEST (ESP32-S3) =============");
    Serial.println(" 1) OLED expressions        6) Camera");
    Serial.println(" 2) Motor 1                 7) Wi-Fi");
    Serial.println(" 3) Motor 2                 8) Gemini");
    Serial.println(" 4) Motor 3                 9) Wireless remote");
    Serial.println(" 5) Motor 4                 0) Exit (start the robot)");
    Serial.println("=============================================================");
    Serial.println(" (the microphone, speaker, STT and TTS tests were removed)");
    Serial.println();

    bool running = true;
    while (running) {
        Serial.print("\ntest> ");
        while (!Serial.available()) delay(20);
        int c = readMenuChoice();

        // safety: motors are always stopped between tests
        motors.emergencyStop();

        switch (c) {
            case 0: running = false; break;
            case 1: testOled();     break;
            case 2: testMotor(0);   break;
            case 3: testMotor(1);   break;
            case 4: testMotor(2);   break;
            case 5: testMotor(3);   break;
            case 6: testCamera();   break;
            case 7: testWifi();     break;
            case 8: testGemini();   break;
            case 9: testRemote();   break;
            default: Serial.println("Unknown choice."); break;
        }
    }
    Serial.println("[TEST] Exiting test mode");
}

// ------------------------------------------------------------
//  serial console (normal build)
// ------------------------------------------------------------
void printConsoleHelp() {
    Serial.println();
    Serial.println("--- WALL-E console ---");
    Serial.println("  Commands go through the same dispatcher as the");
    Serial.println("  wireless remote, so priority rules are identical.");
    Serial.println();
    Serial.println("  fwd / back / left / right / rotl / rotr  - drive");
    Serial.println("  stop      - stop everything NOW (also resumes autonomy)");
    Serial.println("  dance     - start dancing");
    Serial.println("  explore   - start exploring");
    Serial.println("  auto on   - autonomous behaviour on");
    Serial.println("  auto off  - autonomous behaviour off");
    Serial.println("  happy / thinking / surprised / confused / face idle");
    Serial.println("  joke      - ask Gemini for a joke");
    Serial.println("  say X     - send X to Gemini and show the reply on the OLED");
    Serial.println("  status    - print system status");
    Serial.println("  help      - this list");
    Serial.println("-----------------------");
}

void pollSerialConsole() {
    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { line += c; continue; }

        line.trim();
        if (line.length() == 0) { line = ""; return; }

        // ---- movement, via the shared dispatcher ----
        if      (line == "fwd")   commands.dispatch(WALLE_CMD_MOVE_FORWARD,  SOURCE_SERIAL);
        else if (line == "back")  commands.dispatch(WALLE_CMD_MOVE_BACKWARD, SOURCE_SERIAL);
        else if (line == "left")  commands.dispatch(WALLE_CMD_TURN_LEFT,     SOURCE_SERIAL);
        else if (line == "right") commands.dispatch(WALLE_CMD_TURN_RIGHT,    SOURCE_SERIAL);
        else if (line == "rotl")  commands.dispatch(WALLE_CMD_ROTATE_LEFT,   SOURCE_SERIAL);
        else if (line == "rotr")  commands.dispatch(WALLE_CMD_ROTATE_RIGHT,  SOURCE_SERIAL);

        // ---- stop ----
        else if (line == "stop" || line == "halt") {
            commands.dispatch(WALLE_CMD_STOP, SOURCE_SERIAL);
            Serial.println("  stopped");
        }

        // ---- modes ----
        else if (line == "dance")    commands.dispatch(WALLE_CMD_DANCE,          SOURCE_SERIAL);
        else if (line == "explore")  commands.dispatch(WALLE_CMD_EXPLORE,        SOURCE_SERIAL);
        else if (line == "idle")     commands.dispatch(WALLE_CMD_IDLE,           SOURCE_SERIAL);
        else if (line == "auto on")  commands.dispatch(WALLE_CMD_AUTONOMOUS_ON,  SOURCE_SERIAL);
        else if (line == "auto off") commands.dispatch(WALLE_CMD_AUTONOMOUS_OFF, SOURCE_SERIAL);

        // ---- expressions ----
        else if (line == "happy")     commands.dispatch(WALLE_CMD_EXPR_HAPPY,     SOURCE_SERIAL);
        else if (line == "thinking")  commands.dispatch(WALLE_CMD_EXPR_THINKING,  SOURCE_SERIAL);
        else if (line == "surprised") commands.dispatch(WALLE_CMD_EXPR_SURPRISED, SOURCE_SERIAL);
        else if (line == "confused")  commands.dispatch(WALLE_CMD_EXPR_CONFUSED,  SOURCE_SERIAL);
        else if (line == "face idle") commands.dispatch(WALLE_CMD_EXPR_IDLE,      SOURCE_SERIAL);

        // ---- Gemini (text only; STT/TTS were removed) ----
        else if (line == "joke") {
            behavior.requestJoke();
        }
        else if (line.startsWith("say ")) {
            const String prompt = line.substring(4);
            behavior.requestChat(prompt);
            LOGI(TAG, "Asking Gemini: %s", prompt.c_str());
        }

        // ---- info ----
        else if (line == "status") {
            Serial.printf("  state=%s  wifi=%s  rssi=%d  motors=%s  cam=%s\n",
                gRobotSM ? RobotStateMachine::nameOf(gRobotSM->state()) : "?",
                wifi.connected() ? "up" : "down", wifi.rssi(),
                motors.motorsConfigured() ? "ok" : "UNSET",
                cameraManager.available() ? "yes" : "no");
            Serial.printf("  remote=%s  remote_ctl=%s  autonomy=%s\n",
                RemoteLink::nameOf(remoteLink.state()),
                commands.remoteHasControl() ? "yes" : "no",
                behavior.autonomous() ? "on" : "off");
        }
        else if (line == "help") { printConsoleHelp(); }
        else {
            Serial.println("  unknown command - type 'help'");
        }
        line = "";
    }
}
