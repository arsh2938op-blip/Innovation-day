#include "hardware_test.h"
#include "config.h"
#include "log.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "audio_input.h"
#include "audio_output.h"
#include "stt_client.h"
#include "gemini_client.h"
#include "tts_client.h"
#include "camera_manager.h"
#include "wifi_manager.h"
#include "robot_state.h"
#include "behavior.h"
#include "dance.h"
#include <math.h>

static const char* TAG = "TEST";

extern SttClient   stt;
extern GeminiClient gemini;
extern TtsClient   tts;

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

static void testMic() {
    if (!mic.ready()) { Serial.println("[TEST] Microphone NOT available (check config.h pins)"); pressEnter("MIC"); return; }
    mic.runMeterTest(6);
    pressEnter("MIC");
}

static void testSpeaker() {
    Serial.println("[TEST] Playing 440 Hz tone for 2 seconds...");
    if (!speaker.ready()) { Serial.println("[TEST] Speaker NOT available (check config.h pins)"); pressEnter("SPK"); return; }
    // Build a short sine wave and feed it through the normal queue path.
    const size_t n = AUDIO_SAMPLE_RATE * 2;             // 2 s
    int16_t* buf = (int16_t*)malloc(n * sizeof(int16_t));
    if (!buf) { Serial.println("[TEST] Out of memory"); pressEnter("SPK"); return; }
    for (size_t i = 0; i < n; i++) {
        buf[i] = (int16_t)(12000.0 * sin(2.0 * M_PI * 440.0 * i / AUDIO_SAMPLE_RATE));
    }
    speaker.queue((uint8_t*)buf, n * sizeof(int16_t));
    while (speaker.busy()) { speaker.pump(); delay(2); }
    pressEnter("SPK");
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
        Serial.println("[TEST] No camera. Expected on an ESP32-C3: see README camera section.");
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

static void testStt() {
    if (!stt.ready()) { Serial.println("[TEST] STT not configured"); pressEnter("STT"); return; }
    if (!mic.ready()) { Serial.println("[TEST] No microphone"); pressEnter("STT"); return; }
    Serial.println("[TEST] Speak now...");
    if (mic.startRecording(AUDIO_MAX_RECORD_MS / 1000)) {
        while (!mic.pollRecording()) delay(20);
        String text;
        stt.transcribe(mic.pcm(), mic.pcmLength(), &text);
        Serial.printf("[TEST] Transcript: %s\n", text.length() ? text.c_str() : "(nothing)");
        mic.abortRecording();
    }
    pressEnter("STT");
}

static void testTts() {
    if (!tts.ready()) { Serial.println("[TEST] TTS not configured"); pressEnter("TTS"); return; }
    uint8_t* pcm = nullptr;
    size_t bytes = 0;
    if (tts.synthesize("Hello, I am WALL-E and I am alive!", &pcm, &bytes)) {
        Serial.printf("[TEST] Got %u bytes of audio\n", (unsigned)bytes);
        if (speaker.ready() && speaker.queue(pcm, bytes)) {
            while (speaker.busy()) { speaker.pump(); delay(2); }
        } else {
            tts.freeAudio(pcm, bytes);
        }
    }
    pressEnter("TTS");
}

// ------------------------------------------------------------
void runHardwareTestMode() {
    motors.begin();
    motors.emergencyStop();
    oled.begin();
    oled.splash("HARDWARE TEST", "Pick a test");
    Serial.println();
    Serial.println("================= WALL-E HARDWARE TEST =================");
    Serial.println(" 1) OLED expressions        7) Motor 4");
    Serial.println(" 2) Microphone              8) Camera");
    Serial.println(" 3) Speaker                 9) Wi-Fi");
    Serial.println(" 4) Motor 1                10) Gemini");
    Serial.println(" 5) Motor 2                11) STT");
    Serial.println(" 6) Motor 3                12) TTS");
    Serial.println(" 0) Exit (start the robot)");
    Serial.println("=========================================================");

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
            case 2: testMic();      break;
            case 3: testSpeaker();  break;
            case 4: testMotor(0);   break;
            case 5: testMotor(1);   break;
            case 6: testMotor(2);   break;
            case 7: testMotor(3);   break;
            case 8: testCamera();   break;
            case 9: testWifi();     break;
            case 10: testGemini();  break;
            case 11: testStt();     break;
            case 12: testTts();     break;
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
    Serial.println("  talk    - listen and answer");
    Serial.println("  joke    - ask Gemini for a joke");
    Serial.println("  dance   - start dancing   (stop / dance off)");
    Serial.println("  explore - start exploring (idle / explore off)");
    Serial.println("  say X   - send X straight to Gemini and speak it");
    Serial.println("  stop    - stop everything NOW");
    Serial.println("  status  - print system status");
    Serial.println("  help    - this list");
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

        if (line == "help")      { printConsoleHelp(); }
        else if (line == "talk")  { LOGI(TAG, "Listening..."); behavior.requestTalk(); }
        else if (line == "joke")  { behavior.requestJoke(); }
        else if (line == "dance") { behavior.requestDance(); }
        else if (line == "explore") { behavior.requestExplore(); }
        else if (line == "stop" || line == "dance off" || line == "explore off") {
            behavior.halt();
            Serial.println("  stopped");
        }
        else if (line == "status") {
            Serial.printf("  state=%s  wifi=%s  rssi=%d  motors=%s  mic=%s  spk=%s  cam=%s\n",
                gRobotSM ? RobotStateMachine::nameOf(gRobotSM->state()) : "?",
                wifi.connected() ? "up" : "down", wifi.rssi(),
                motors.motorsConfigured() ? "ok" : "UNSET",
                mic.ready() ? "ok" : "off", speaker.ready() ? "ok" : "off",
                cameraManager.available() ? "yes" : "no");
        }
        else if (line.startsWith("say ")) {
            String prompt = line.substring(4);
            String reply;
            if (gemini.ask(prompt, &reply)) Serial.printf("  %s\n", reply.c_str());
            else Serial.println("  gemini failed");
        }
        else {
            Serial.println("  unknown command - type 'help'");
        }
        line = "";
    }
}
