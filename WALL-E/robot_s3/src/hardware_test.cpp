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
#include "robot_state.h"
#include "behavior.h"
#include "dance.h"
#include "tts_client.h"
#include "audio_output.h"
#include "cliff_sensor.h"
#include "maneuver.h"
#include "persona.h"
#include "safety.h"
#include "app_link.h"
#include <math.h>

static const char* TAG = "TEST";

extern GeminiClient gemini;
extern TtsClient    tts;

// Read one positive integer after a command word, e.g. "steps 4".
// Returns 0 when there is no number, so the caller can pick a default.
static uint16_t readCountArg(const char* prompt) {
    Serial.print(prompt);
    while (!Serial.available()) delay(20);
    String line = Serial.readStringUntil('\n');
    line.trim();
    return (uint16_t)line.toInt();
}

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

static void testSpeaker() {
    Serial.println("[TEST] Playing a 440 Hz tone for 2 seconds...");
    if (!speaker.ready()) {
        Serial.println("[TEST] Speaker NOT available (set SPK_I2S_* in include/config.h)");
        pressEnter("SPK");
        return;
    }
    const size_t n = TTS_SAMPLE_RATE * 2;             // 2 s of 16-bit mono
    int16_t* buf = (int16_t*)malloc(n * sizeof(int16_t));
    if (!buf) { Serial.println("[TEST] Out of memory"); pressEnter("SPK"); return; }
    for (size_t i = 0; i < n; i++) {
        buf[i] = (int16_t)(11000.0 * sin(2.0 * M_PI * 440.0 * i / TTS_SAMPLE_RATE));
    }
    speaker.queue((uint8_t*)buf, n * sizeof(int16_t));
    while (speaker.busy()) { speaker.pump(); delay(2); }
    Serial.println("[TEST] Tone finished");
    pressEnter("SPK");
}

static void testTts() {
    if (!tts.ready()) {
        Serial.println("[TEST] TTS needs WALLE_GEMINI_API_KEY in secrets.h");
        pressEnter("TTS");
        return;
    }
    if (!speaker.ready()) {
        Serial.println("[TEST] No speaker wired, cannot hear the audio");
    }
    Serial.println("[TEST] Asking Gemini for a short sentence...");
    oled.setExpression(EXPR_THINKING);
    String reply;
    if (!gemini.ask("Say hello to the room in one short sentence.", &reply)) {
        Serial.println("[TEST] Gemini failed");
        pressEnter("TTS");
        return;
    }
    Serial.printf("[TEST] Text: %s\n", reply.c_str());

    oled.setExpression(EXPR_SPEAKING);
    oled.setCaption(reply.c_str());
    if (tts.speak(reply)) Serial.println("[TEST] TTS request sent and streamed");
    else                  Serial.println("[TEST] TTS request FAILED");

    // Let the tail of the audio play out.
    while (speaker.busy()) { speaker.pump(); delay(5); }
    oled.setExpression(EXPR_IDLE);
    pressEnter("TTS");
}

// ------------------------------------------------------------
//  10. DRIVER MODULE
//  Checks the enable lines and every channel in isolation. This is
//  the test that catches "only one side of the robot moves", which
//  is almost always a forgotten STBY on the second driver board.
// ------------------------------------------------------------
static void testDriver() {
    Serial.printf("[TEST] Driver: %s\n", motors.driverName());
    Serial.printf("[TEST] Channels: %u (4 = differential steering works)\n",
                  motors.channels());
    if (motors.channels() < 4) {
        Serial.println("[TEST] !! Fewer than 4 channels: steering is skid-steer only.");
        Serial.println("[TEST]    With 2 channels the 'turn' commands can only spin.");
    }
    Serial.printf("[TEST] Motor pins configured: %s\n",
                  motors.motorsConfigured() ? "yes" : "NO - see config.h section 4");

    // Standby toggle: watch that the driver really enables/disables.
    Serial.println("[TEST] Toggling the driver enable pin (watch for a click)...");
    motors.setDriverEnabled(false);
    Serial.println("[TEST]   enabled = false (should be silent)");
    delay(700);
    motors.setDriverEnabled(true);
    Serial.println("[TEST]   enabled = true");
    delay(700);
    motors.emergencyStop();

    pressEnter("DRIVER");
}

// ------------------------------------------------------------
//  11. CLIFF SENSOR
//  The single most important test to get right: it is what stops
//  the robot falling off the table. Hold the robot on the table and
//  watch that the numbers make sense.
// ------------------------------------------------------------
static void testCliff() {
    Serial.println("[TEST] === CLIFF SENSOR (HC-SR04, tilted down) ===");

    if (!cliffSensor.enabled()) {
        Serial.println("[TEST] SENSOR_ENABLE = 0 in config.h");
        pressEnter("CLIFF"); return;
    }
    if (!cliffSensor.configured()) {
        Serial.println("[TEST] Pins not set: check SENSOR_TRIG_PIN / SENSOR_ECHO_PIN.");
        Serial.println("[TEST] ECHO MUST go through a 1k/2k divider - 5 V will kill the S3.");
        pressEnter("CLIFF"); return;
    }

    Serial.printf("[TEST] Mount angle      : %d deg (cos = %.3f)\n",
                  SENSOR_MOUNT_ANGLE_DEG, (double)(cosf(SENSOR_MOUNT_ANGLE_DEG * 0.0174532925f)));
    Serial.printf("[TEST] Nominal floor     : %.1f cm   <-- MEASURE THIS\n",
                  (double)SENSOR_NOMINAL_GROUND_CM);
    Serial.printf("[TEST] Warn below / drop above: %.1f / %.1f cm\n",
                  (double)(SENSOR_NOMINAL_GROUND_CM - SENSOR_WARN_CLEAR_CM),
                  (double)(SENSOR_NOMINAL_GROUND_CM + SENSOR_TRIP_DROP_CM));

    Serial.println("[TEST]");
    Serial.println("[TEST] Hold the robot ON THE TABLE for 8 s, then LIFT it so the");
    Serial.println("[TEST] sensor sees nothing (that is what a table edge looks like).");
    Serial.println("[TEST]");
    Serial.printf("[TEST]   %-8s %-10s %-10s %s\n",
                  "time", "slant cm", "ground cm", "state");

    const uint32_t start = millis();
    while (millis() - start < 8000) {
        cliffSensor.update(millis());
        Serial.printf("[TEST]   %-8lu %-10u %-10u %s\n",
                      (unsigned long)((millis() - start) / 1000),
                      cliffSensor.slantCm(), cliffSensor.groundCm(),
                      CliffSensor::nameOf(cliffSensor.state()));
        delay(1000);
    }

    Serial.println();
    Serial.println("[TEST] Lift the robot now and watch for: DROP, then back-away.");
    const uint32_t start2 = millis();
    while (millis() - start2 < 4000) {
        safety.update(millis());
        cliffSensor.update(millis());
        Serial.printf("[TEST]   ground %u cm  state %-8s wheels %-9s %s\n",
                      cliffSensor.groundCm(),
                      CliffSensor::nameOf(cliffSensor.state()),
                      SafetyGuard::reasonName(safety.blockReason()),
                      safety.wheelsAllowed() ? "ALLOWED" : "blocked");
        delay(500);
    }

    motors.emergencyStop();
    Serial.printf("[TEST] Nominal floor for you to copy into config.h: about %u cm\n",
                  cliffSensor.groundCm());
    pressEnter("CLIFF");
}

// ------------------------------------------------------------
//  12. MOTION PRIMITIVES
//  Forward / N steps / turn around, with the safety guard live.
// ------------------------------------------------------------
static void testManeuver() {
    Serial.println("[TEST] === MOTION PRIMITIVES ===");
    Serial.printf("[TEST] step = %d cm, %d ms per 360 deg, %d cm/s\n",
                  STEP_DISTANCE_CM, MANEUVER_MS_PER_TURN_360, MANEUVER_CM_PER_SECOND);
    Serial.println("[TEST] The guard stays live: put the robot near an edge and a");
    Serial.println("[TEST] maneuver will be cut short rather than run to the end.");
    Serial.println();

    // Forward
    Serial.println("[TEST] 1) forward 1.2 s");
    motors.emergencyStop();
    if (maneuver.startForward(1200)) {
        while (maneuver.busy()) { safety.update(millis()); maneuver.update(millis()); delay(10); }
    }
    motors.emergencyStop();
    Serial.println("[TEST]    done. Measure how far it went - that is your calibration.");
    pressEnter("STEPS-1");

    // Steps
    const uint16_t steps = readCountArg("[TEST] 2) how many steps? ");
    if (steps > 0) {
        Serial.printf("[TEST] Driving %u steps (%u cm)...\n",
                      steps, steps * STEP_DISTANCE_CM);
        motors.emergencyStop();
        const uint32_t t0 = millis();
        if (maneuver.startForwardSteps(steps)) {
            while (maneuver.busy()) {
                safety.update(millis());
                cliffSensor.update(millis());
                maneuver.update(millis());
                Serial.printf("\r[TEST]   %s, %lu ms left   ",
                              Maneuver::nameOf(maneuver.kind()),
                              (unsigned long)maneuver.remainingMs(millis()));
                delay(20);
            }
            Serial.println();
        }
        motors.emergencyStop();
        Serial.printf("[TEST]    %u steps took %lu ms for %u cm\n",
                      steps, (unsigned long)(millis() - t0), steps * STEP_DISTANCE_CM);
    }
    pressEnter("STEPS-2");

    // Turn around
    Serial.println("[TEST] 3) turn around (180 deg)");
    motors.emergencyStop();
    if (maneuver.startTurnAround()) {
        while (maneuver.busy()) { safety.update(millis()); maneuver.update(millis()); delay(10); }
    }
    motors.emergencyStop();
    Serial.println("[TEST]    it should now be facing the way it started.");
    pressEnter("STEPS-3");

    // Back-away, as used after a cliff stop
    Serial.println("[TEST] 4) the back-away used after a cliff stop");
    motors.emergencyStop();
    if (maneuver.startRetreatSteps(SAFETY_BACKAWAY_STEPS)) {
        while (maneuver.busy()) { maneuver.update(millis()); delay(10); }
    }
    motors.emergencyStop();
    pressEnter("STEPS-4");
}

// ------------------------------------------------------------
// ------------------------------------------------------------
//  11. PERSONA
//  The app decides who the robot is. This exercises the exact
//  frames the app sends, so the voice pipeline can be proven
//  without the phone: accept, refuse, and prove a refusal leaves
//  the previous personality completely untouched.
// ------------------------------------------------------------
static void testPersona() {
    Serial.println("[TEST] === PERSONA ===");
    Serial.printf("[TEST] Compiled-in default : %s\n", persona.name());
    Serial.printf("[TEST]   prompt: %s\n", persona.prompt());

    const char* good =
        "{\"n\":\"Pinocchio\",\"s\":\"Buon giorno!\",\"m\":\"happy\","
        "\"p\":\"You are Pinocchio, a cheerful little robot for children. "
        "Always reply. One or two short sentences.\"}";
    const char* missingKey = "{\"n\":\"Nobody\",\"m\":\"happy\",\"p\":\"hi\"}";
    const char* garbage     = "not json at all";

    Serial.println();
    Serial.println("[TEST] 1) a valid payload -> expect ACCEPTED");
    commands.setPersona(good);
    Serial.printf("[TEST]    name   = %s\n", persona.name());
    Serial.printf("[TEST]    mood   = %s\n", persona.mood());
    Serial.printf("[TEST]    suffix = \"%s\"\n", persona.suffix());
    Serial.printf("[TEST]    prompt = %s\n", persona.prompt());

    Serial.println();
    Serial.println("[TEST] 2) missing the \"s\" key -> expect REFUSED");
    commands.setPersona(missingKey);
    Serial.printf("[TEST]    name = %s   (must still be Pinocchio)\n", persona.name());

    Serial.println();
    Serial.println("[TEST] 3) not JSON at all -> expect REFUSED");
    commands.setPersona(garbage);
    Serial.printf("[TEST]    name = %s   (must still be Pinocchio)\n", persona.name());

    Serial.println();
    Serial.println("[TEST] 4) the suffix is added only when it is missing:");
    String a = "I am Pinocchio.";
    persona.decorate(&a);
    Serial.printf("[TEST]    plain       -> \"%s\"\n", a.c_str());
    String b = "I am Pinocchio. buon giorno!";
    persona.decorate(&b);
    Serial.printf("[TEST]    already there -> \"%s\"   (must NOT double up)\n", b.c_str());

    Serial.println();
    Serial.println("[TEST] 5) restoring the compiled-in default");
    persona.begin();
    Serial.printf("[TEST]    name = %s\n", persona.name());

    pressEnter("PERSONA");
}

// ------------------------------------------------------------
//  12. APP LINK
//  Prints the address to connect to and waits for a TCP client.
//  There is no radio remote any more, so this is the only way to
//  drive the robot from outside the firmware.
// ------------------------------------------------------------
static void testApp() {
    Serial.println("[TEST] === APP LINK (TCP) ===");
    Serial.printf("[TEST] Port: %d\n", APP_TCP_PORT);
    Serial.printf("[TEST] Link: %s\n", AppLink::nameOf(appLink.state()));
    Serial.printf("[TEST] Persona: %s\n", persona.name());

    if (!wifi.connected()) {
        Serial.println("[TEST] Wi-Fi is DOWN, so the app cannot connect yet.");
        Serial.println("[TEST] Connect the robot to the same network as your phone.");
    } else {
        Serial.printf("[TEST] Connect to tcp://%s:%d from a laptop:\n",
                      WiFi.localIP().toString().c_str(), APP_TCP_PORT);
        Serial.printf("[TEST]   nc %s %d\n",
                      WiFi.localIP().toString().c_str(), APP_TCP_PORT);
    }
    Serial.println("[TEST] Every frame starts with the magic byte 0xA5.");
    Serial.println("[TEST] Byte layout: docs/APP_INTEGRATION.md");
    Serial.println("[TEST] On connect the app sends HELLO, a persona, then a sensor read.");

    Serial.println("[TEST] Waiting 20 s for the app...");
    const uint32_t start = millis();
    while (millis() - start < 20000) {
        appLink.update(millis());
        if (appLink.connected()) {
            Serial.println("[TEST] App CONNECTED - drive it from the phone.");
        }
        delay(50);
    }
    Serial.printf("[TEST] Link: %s\n", AppLink::nameOf(appLink.state()));
    Serial.printf("[TEST] Persona the app set: %s\n", persona.name());
    pressEnter("APP");
}
// ------------------------------------------------------------
void runHardwareTestMode() {
    // Bring up only what the tests need. The safety guard and the
    // sensor come up too, so the cliff tests exercise the REAL
    // reaction path rather than a stub.
    motors.begin();
    motors.emergencyStop();
    oled.begin();
    maneuver.begin();
    cliffSensor.begin();
    safety.begin(&cliffSensor);
    speaker.begin();

    wifi.begin();
    appLink.begin();

    oled.splash("HARDWARE TEST", "Pick a test");
    Serial.println();
    Serial.println("============= WALL-E HARDWARE TEST (ESP32-S3) =============");
    Serial.println(" 1) OLED expressions        8) Gemini");
    Serial.println(" 2) Motor 1                 9) Speaker tone");
    Serial.println(" 3) Motor 2                10) TTS (speak a Gemini line)");
    Serial.println(" 4) Motor 3                11) Persona (accept + refuse)");
    Serial.println(" 5) Motor 4                12) App link (TCP)");
    Serial.println(" 6) Camera                 13) Driver module (STBY + channels)");
    Serial.println(" 7) Wi-Fi                  14) Cliff sensor (HC-SR04)");
    Serial.println("                           15) Motion primitives / calibration");
    Serial.println(" 0) Exit (start the robot)");
    Serial.println("=============================================================");
    Serial.println(" (there is no microphone test: the robot has no mic - the");
    Serial.println("  app does the speech recognition and sends the words as text)");
    Serial.println(" DO 14 FIRST: without a correct SENSOR_NOMINAL_GROUND_CM the");
    Serial.println(" robot will refuse to move at all.");
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
            case 9: testSpeaker();  break;
            case 10: testTts();     break;
            case 11: testPersona(); break;
            case 12: testApp();     break;
            case 13: testDriver();  break;
            case 14: testCliff();   break;
            case 15: testManeuver(); break;
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
    Serial.println("  Commands go through the same dispatcher as the app,");
    Serial.println("  so the priority rules are identical.");
    Serial.println();
    Serial.println("  fwd / back / left / right / rotl / rotr  - drive");
    Serial.println("  stop      - stop everything NOW (also resumes autonomy)");
    Serial.println("  steps N   - drive N steps and stop (open loop, no encoders)");
    Serial.println("  turn N    - pivot N degrees (turn 180 to turn around)");
    Serial.println("  dance     - start dancing");
    Serial.println("  explore   - start exploring");
    Serial.println("  auto on   - autonomous behaviour on");
    Serial.println("  auto off  - autonomous behaviour off");
    Serial.println("  happy / thinking / surprised / confused / face idle");
    Serial.println("  joke      - ask Gemini for a joke (spoken out loud)");
    Serial.println("  talk      - ask Gemini for a line, then speak it");
    Serial.println("  speak X   - speak X verbatim through TTS (no Gemini)");
    Serial.println("  say X     - send X to Gemini and show the reply on the OLED");
    Serial.println("  sensor    - one cliff reading, live");
    Serial.println("  watch     - continuous cliff readings + the safety verdict");
    Serial.println("  whoami    - who the robot currently thinks it is");
    Serial.println("  status    - print system status");
    Serial.println("  help      - this list");
    Serial.println();
    Serial.println("  SENSOR / MOTOR / GEOMETRY / SAFETY / APP settings all live");
    Serial.println("  in include/config.h - see the SENSOR and 10b sections.");
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

        // ---- measured motion: N steps, N degrees ----
        // These go through the dispatcher so the cliff guard gets to
        // veto them, exactly as it would a remote or app command.
        else if (line.startsWith("steps ")) {
            const uint16_t n = (uint16_t)line.substring(6).toInt();
            commands.dispatch(WALLE_CMD_MOVE_STEPS, SOURCE_SERIAL, n);
        }
        else if (line == "turn around") {
            commands.dispatch(WALLE_CMD_TURN_AROUND, SOURCE_SERIAL);
        }
        else if (line.startsWith("turn ")) {
            const uint16_t deg = (uint16_t)line.substring(5).toInt();
            commands.dispatch(WALLE_CMD_TURN_DEGREES, SOURCE_SERIAL, deg);
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

        // ---- voice (Gemini TTS) ----
        else if (line == "joke") {
            behavior.requestJoke();
        }
        else if (line == "talk") {
            commands.dispatch(WALLE_CMD_TALK, SOURCE_SERIAL);
        }
        else if (line.startsWith("speak ")) {
            behavior.requestSpeak(line.substring(6));
        }
        else if (line.startsWith("say ")) {
            const String prompt = line.substring(4);
            behavior.requestChat(prompt);
            LOGI(TAG, "Asking Gemini: %s", prompt.c_str());
        }

        // ---- the persona ----
        else if (line == "whoami") {
            Serial.printf("  I am %s (%s), set by %s\n",
                          persona.name(), persona.mood(),
                          persona.fromApp() ? "the app"
                                            : "the compiled-in default");
            Serial.printf("  instruction: %s\n", persona.prompt());
            Serial.printf("  every reply ends with: \"%s\"\n", persona.suffix());
        }
        else if (line.startsWith("persona ")) {
            // Accepts the same compact JSON the app sends, so a
            // personality can be changed from the console mid-demo.
            commands.setPersona(line.substring(8).c_str());
            Serial.printf("  I am now %s\n", persona.name());
        }

        // ---- the cliff sensor ----
        else if (line == "sensor") {
            cliffSensor.pollNow();
            Serial.printf("  cliff=%-7s ground=%u cm  slant=%u cm  state=%s\n",
                CliffSensor::nameOf(cliffSensor.state()),
                cliffSensor.groundCm(), cliffSensor.slantCm(),
                safety.wheelsAllowed() ? "wheels ALLOWED" :
                    SafetyGuard::reasonName(safety.blockReason()));
            Serial.printf("  (configured for %.1f cm, trips at +%.1f cm)\n",
                (double)SENSOR_NOMINAL_GROUND_CM, (double)SENSOR_TRIP_DROP_CM);
        }
        else if (line == "watch") {
            Serial.println("  watching the cliff sensor - type anything to stop");
            while (!Serial.available()) {
                safety.update(millis());
                cliffSensor.update(millis());
                Serial.printf("\r  ground=%3u cm  %-7s  wheels=%-9s  heap=%5u   ",
                              cliffSensor.groundCm(),
                              CliffSensor::nameOf(cliffSensor.state()),
                              SafetyGuard::reasonName(safety.blockReason()),
                              (unsigned)ESP.getFreeHeap());
                delay(60);
            }
            Serial.println();
            Serial.readStringUntil('\n');
        }

        // ---- info ----
        else if (line == "status") {
            Serial.printf("  state=%s  wifi=%s  rssi=%d  motors=%s  cam=%s\n",
                gRobotSM ? RobotStateMachine::nameOf(gRobotSM->state()) : "?",
                wifi.connected() ? "up" : "down", wifi.rssi(),
                motors.motorsConfigured() ? "ok" : "UNSET",
                cameraManager.available() ? "yes" : "no");
            Serial.printf("  driver=%s  channels=%u  enabled=%s\n",
                motors.driverName(), motors.channels(),
                motors.driverEnabled() ? "yes" : "no");
            Serial.printf("  cliff=%-7s ground=%u cm  block=%s  maneuver=%s\n",
                CliffSensor::nameOf(cliffSensor.state()),
                cliffSensor.groundCm(),
                SafetyGuard::reasonName(safety.blockReason()),
                maneuver.busy() ? Maneuver::nameOf(maneuver.kind()) : "none");
            Serial.printf("  app=%s  owner=%s  autonomy=%s\n",
                AppLink::nameOf(appLink.state()),
                CommandDispatcher::sourceName(commands.owner()),
                behavior.autonomous() ? "on" : "off");
            Serial.printf("  persona=%s (%s)  spk=%s  audio=%s  tts=%s\n",
                persona.name(),
                persona.fromApp() ? "from app" : "built in",
                speaker.ready() ? "ok" : "UNSET",
                speaker.loaded() ? "loaded" : "unloaded (idle)",
                tts.ready() ? "ok" : "no-key");
        }
        else if (line == "help") { printConsoleHelp(); }
        else {
            Serial.println("  unknown command - type 'help'");
        }
        line = "";
    }
}
