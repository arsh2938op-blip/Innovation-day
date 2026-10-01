// ============================================================
//  WALL-E - CENTRAL CONFIGURATION
//  ------------------------------------------------------------
//  EVERY hardware-specific value lives in this file.
//  Nothing else in the project should contain a raw GPIO number,
//  an IP address, an API key or a magic timing number.
//
//  >>> GPIO values marked TODO_CONFIGURE_GPIO are UNKNOWN. <<<
//  The firmware boots safe: any subsystem whose pins are still
//  unset is disabled at runtime and reports why on the OLED/serial.
// ============================================================
#pragma once

#include <Arduino.h>

// ------------------------------------------------------------
// 0. Secrets
// ------------------------------------------------------------
#if __has_include("secrets.h")
#include "secrets.h"
#else
// Placeholders so the project still compiles without a secrets file.
#define WALLE_WIFI_SSID      ""
#define WALLE_WIFI_PASSWORD  ""
#define WALLE_GEMINI_API_KEY ""
#endif

// ------------------------------------------------------------
// 1. Feature switches
// ------------------------------------------------------------
#ifndef WALLE_TEST_MODE
#define WALLE_TEST_MODE 0          // 1 = interactive hardware test menu
#endif

#define WALLE_ENABLE_MOTORS     1
#define WALLE_ENABLE_OLED       1
#define WALLE_ENABLE_WIFI       1

// Speaker + Gemini text-to-speech. WALL-E SPEAKS its Gemini replies out
// loud through the amplifier and speaker (section 6 + section 9).
// Speech-to-Text is NOT here: the robot has no microphone. The app
// does the speech recognition and sends the words as text, so the whole
// voice pipeline is:  mic -> app -> TCP -> robot -> Gemini -> robot's
// speaker.
#define WALLE_ENABLE_AUDIO_OUT  1
#define WALLE_ENABLE_TTS        1

// Autonomous behaviour (idle -> joke / dance / explore on its own).
// This is the default state at boot; the remote's
// autonomous_on / autonomous_off commands flip it at runtime.
#define WALLE_AUTONOMOUS_DEFAULT 1

// The camera is DISABLED by default.
// A plain ESP32-S3 module has no camera peripheral either; the camera modules that
// do exist need a specific SoC (e.g. ESP32-P4) and a vendor camera driver.
// Set to 1 only after you have confirmed your board actually has one and you
// have wired the camera pins in section 5 below.
#define WALLE_ENABLE_CAMERA     0

// ------------------------------------------------------------
// 2. Sentinel for pins that are not known yet
// ------------------------------------------------------------
#define TODO_CONFIGURE_GPIO  (-1)
#define PIN_IS_UNSET(p)      ((p) < 0)

// ------------------------------------------------------------
//  HARDWARE PINS  (ESP32-S3)
//  >>> FILL THESE IN FOR YOUR BOARD. <<<
//  The S3 devkit exposes plenty of usable GPIO: no microphone pins are
//  needed, because speech recognition lives in the app, so the motors,
//  the OLED, the speaker and the cliff sensor all fit comfortably.
//
//  Avoid on the S3:
//    * GPIO 26..32  - wired to the SPI flash / PSRAM on most modules
//    * GPIO 45 & 46 - strapping pins (VDD_SPI voltage select)
//    * GPIO 0, 3    - strapping pins (boot mode), and GPIO 0 drives
//                     the on-board LED
//    * GPIO 19, 20  - USB D-/D+ (only free if you do not use USB)
// ============================================================

// ------------------------------------------------------------
// 3. OLED - SSD1306 128x64, I2C
//    Any free S3 GPIO pair works; the I2C bus is fully software.
// ------------------------------------------------------------
#define OLED_I2C_PORT     0
#define OLED_I2C_SDA_PIN  TODO_CONFIGURE_GPIO
#define OLED_I2C_SCL_PIN  TODO_CONFIGURE_GPIO
#define OLED_I2C_ADDR     0x3C      // 0x3D also common
#define OLED_WIDTH        128
#define OLED_HEIGHT       64

// ------------------------------------------------------------
// 4. MOTORS - the ESP32-S3 NEVER drives a motor directly
// ------------------------------------------------------------
//  The S3 is only a LOGIC pin source. Current comes from a battery
//  and a motor driver module (H-bridge) is what pushes it through
//  the windings. No pin in this firmware ever carries motor current:
//  every motor pin goes to a driver INPUT.
//
//  REQUIRED DRIVER TOPOLOGY: 4 INDEPENDENT CHANNELS
//  -----------------------------------------------
//  A four-wheel robot needs four independent H-bridges, because
//  differential steering (turnLeft/turnRight) works by running the
//  left and right wheels at DIFFERENT speeds. Put two motors on one
//  driver channel and they are locked together - the robot can then
//  only drive straight or pivot, it cannot steer.
//
//  Recommended: 2 x TB6612FNG (2 channels each) = 4 channels.
//  Also fine: 2 x DRV8833, 4 x L9110S.
//
//  ONE-CHANNEL / TWO-CHANNEL DRIVERS TO AVOID HERE:
//    * L298N             - 2 channels, and it drops ~2 V across
//                          itself. Fine for a 2-wheel kit, wrong
//                          for this chassis.
//    * one TB6612FNG     - 2 channels, same steering problem.
//    * one DRV8833       - 2 channels, same problem.
//  If a 2-channel driver is all you have, set MOTOR_COUNT to 2 in
//  this file: the chassis then becomes skid-steer and the arc and
//  pivot commands collapse into spin commands.
//
//  Per motor the driver needs 1x PWM (speed) + 2x direction, so the
//  S3 spends 4 PWM + 8 direction pins + the enable pin(s) below.
//  The S3 has plenty, so this is not a constraint.
// ------------------------------------------------------------

// Human-readable driver name: printed at boot and in the test menu
// so the serial log always says what the firmware thinks it drives.
#define MOTOR_DRIVER_NAME     "TB6612FNG x2 (4 independent channels)"

// Driver enable / standby pins. Two driver boards usually means two
// STBY wires; BOTH must be driven or the second board never comes
// alive. Leave the second one -1 if your driver has only one enable.
#define MOTOR_ENABLE_PIN      TODO_CONFIGURE_GPIO   // driver 1 STBY / EN
#define MOTOR_ENABLE2_PIN     TODO_CONFIGURE_GPIO   // driver 2 STBY / EN, or -1

// Most drivers are active-HIGH on STBY. Set to 0 if yours must be
// pulled LOW to enable.
#define MOTOR_ENABLE_ACTIVE_HIGH 1

#define MOTOR_PWM_FREQ        20000                  // 20 kHz = above human hearing
#define MOTOR_PWM_BITS        8
#define MOTOR_COUNT           4

// Pin order used EVERYWHERE in this firmware. This list and the
// _side[] array in motor_controller.cpp must stay in the same order:
//    0 = front-left    1 = back-left    2 = front-right    3 = back-right
static const int8_t MOTOR_L1_PIN = TODO_CONFIGURE_GPIO;  // M1 PWM (front-left)
static const int8_t MOTOR_L2_PIN = TODO_CONFIGURE_GPIO;  // M2 PWM   (back-left)
static const int8_t MOTOR_R1_PIN = TODO_CONFIGURE_GPIO;  // M3 PWM   (front-right)
static const int8_t MOTOR_R2_PIN = TODO_CONFIGURE_GPIO;  // M4 PWM   (back-right)

static const int8_t MOTOR_L1_IN1 = TODO_CONFIGURE_GPIO;
static const int8_t MOTOR_L1_IN2 = TODO_CONFIGURE_GPIO;
static const int8_t MOTOR_L2_IN1 = TODO_CONFIGURE_GPIO;
static const int8_t MOTOR_L2_IN2 = TODO_CONFIGURE_GPIO;
static const int8_t MOTOR_R1_IN1 = TODO_CONFIGURE_GPIO;
static const int8_t MOTOR_R1_IN2 = TODO_CONFIGURE_GPIO;
static const int8_t MOTOR_R2_IN1 = TODO_CONFIGURE_GPIO;
static const int8_t MOTOR_R2_IN2 = TODO_CONFIGURE_GPIO;

// ------------------------------------------------------------
// 4b. CLIFF SENSOR - HC-SR04, mounted pointing DOWN
// ------------------------------------------------------------
//  THE PROBLEM IT SOLVES
//  WALL-E lives on a table. When it drives off the edge it falls and
//  the project is over. One HC-SR04, aimed down and forward, is
//  enough to see the drop before the wheels reach it.
//
//  THE GEOMETRY - why it is tilted at 45 degrees
//  --------------------------------------------
//  Mounted flat (straight down) the sensor only measures the height
//  of the robot above the floor, which is constant while driving -
//  it would never notice the edge until the robot was already over
//  it. Tilted forward to SENSOR_MOUNT_ANGLE_DEG, the beam points
//  ahead of the wheels, so it looks at the ground the robot is
//  ABOUT to drive onto:
//
//        sensor
//          |\
//          | \  45 deg
//          |  \
//          *   \
//              o---------------- ground
//             d  (slant)
//
//  With the sensor at height h above the floor and a flat floor, the
//  slant range d is constant:  d = h / cos(angle). Crossing the
//  table edge, the floor stops: d grows without bound. So the test
//  is simply "is d still about what it should be".
//
//  The firmware converts the slant reading into the useful number:
//
//      ground_cm = slant_cm * cos(SENSOR_MOUNT_ANGLE_DEG)
//      h         = ground_cm / cos(angle)   -> sensor height
//
//  and compares that against SENSOR_NOMINAL_GROUND_CM.
//  >>> MEASURE SENSOR_NOMINAL_GROUND_CM ON THE REAL ROBOT. <<<
//  Hold the robot on the table, print a few readings with the test
//  menu's "cliff sensor" item, and set the nominal value from that.
//  A guessed value here is the single most likely reason WALL-E
//  refuses to move, so do not skip it.
//
//  WIRING - the ECHO pin NEEDS A VOLTAGE DIVIDER
//  ---------------------------------------------
//  The HC-SR04 runs on 5 V and its ECHO output swings to 5 V. The
//  ESP32-S3 is a 3.3 V part and is NOT 5 V tolerant, so ECHO must be
//  divided:
//      HC-SR04 ECHO ---[ 1k ]---+--- ESP32-S3 GPIO (SENSOR_ECHO_PIN)
//                               |
//  [ 2k ]
//  |
//  GND
//  For 5 V -> 3.0 V. VCC -> 5 V, GND -> GND (the ESP32 and the
//  sensor MUST share a ground or the echo is meaningless).
//  TRIG is driven by the ESP32 and only ever sees 3.3 V, so it can
//  be connected straight through.
//
//  TIMING - HC-SR04 quirks the driver already handles for you:
//    * needs ~60 ms between triggers, so SENSOR_INTERVAL_MS is 60+
//    * its "no object" signal is a 38 ms burst, not an error; the
//      driver times it out and reports a fault rather than
//      believing it
//    * it is blind to soft surfaces (a desk mat eats the beam), so
//      SENSOR_NOMINAL_GROUND_CM must be measured on the real surface
// ------------------------------------------------------------
#define SENSOR_ENABLE          1
#define SENSOR_TRIG_PIN        TODO_CONFIGURE_GPIO
#define SENSOR_ECHO_PIN        TODO_CONFIGURE_GPIO

// Geometry. 45 deg is what the physical mount is built for.
#define SENSOR_MOUNT_ANGLE_DEG 45

// Distance the sensor should normally read, expressed as the vertical
// drop from the sensor to the floor. For a 45 deg mount this is the
// height of the sensor above the surface. MEASURE IT.
#define SENSOR_NOMINAL_GROUND_CM 12

// Trip points, both measured the same way (vertical, in cm):
//   below NOMINAL - WARN_CLEAR_CM  -> floor found, drive
//   above NOMINAL + TRIP_DROP_CM  -> DROP, stop now
//   between                        -> WARN, creep or stop
//
// CLAMP_MAX_CM is a hard sanity ceiling: the HC-SR04 can never see
// further than about 400 cm, so anything above this is nonsense and
// is treated as a fault rather than a valid "no floor".
#define SENSOR_WARN_CLEAR_CM    4
#define SENSOR_TRIP_DROP_CM     9
#define SENSOR_CLAMP_MAX_CM     200

// Readings outside this are thrown away (soft surfaces, noise).
#define SENSOR_MIN_VALID_CM     2

// One reading = this many echo samples, median taken. More samples
// reject more spikes at the cost of a slightly later update.
#define SENSOR_SAMPLES          5

// Must be >= 60 ms (HC-SR04 datasheet minimum trigger cycle).
#define SENSOR_INTERVAL_MS      70

// Echo timeout. The HC-SR04 signals "nothing there" with a ~38 ms
// burst; at 343 m/s that is ~13 m, far past anything real here.
#define SENSOR_ECHO_TIMEOUT_US  12000

// FAIL SAFE: if this many consecutive readings fail (no echo at all)
// the robot treats the sensor as FAULT and stops. A broken or
// unplugged sensor must never look like "safe to drive".
#define SENSOR_FAULT_LIMIT      3

// Speeds the safety system is allowed to use while the sensor is
// unhappy. When the robot is creeping towards a possible edge it
// drops to SENSOR_CAUTION_SPEED instead of walking at SPEED_WALK.
#define SENSOR_CAUTION_SPEED    70

// ------------------------------------------------------------
// 5. Camera  (only used when WALLE_ENABLE_CAMERA == 1)
// ------------------------------------------------------------
#define CAMERA_MODEL_UNKNOWN  0
#define CAMERA_PWDN_PIN       TODO_CONFIGURE_GPIO
#define CAMERA_RESET_PIN      TODO_CONFIGURE_GPIO
#define CAMERA_XCLK_PIN       TODO_CONFIGURE_GPIO
#define CAMERA_SDA_PIN        TODO_CONFIGURE_GPIO
#define CAMERA_SCL_PIN        TODO_CONFIGURE_GPIO
#define CAMERA_D0_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_D1_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_D2_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_D3_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_D4_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_D5_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_D6_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_D7_PIN         TODO_CONFIGURE_GPIO
#define CAMERA_VSYNC_PIN      TODO_CONFIGURE_GPIO
#define CAMERA_HREF_PIN       TODO_CONFIGURE_GPIO
#define CAMERA_PCLK_PIN       TODO_CONFIGURE_GPIO
#define CAMERA_LEDC_PIN       TODO_CONFIGURE_GPIO
// Small QVGA frames. Uploaded JPEG size must stay under the API limit.
// The frame size enum (QVGA etc.) comes from the vendor camera library,
// so it is only valid once the camera support is actually enabled.
#define CAMERA_FRAME_SIZE_STR "QVGA"
#define CAMERA_JPEG_QUALITY   12

// ------------------------------------------------------------
// 6. Speaker (I2S out) -> audio amplifier -> 8 ohm speaker
//
//  Speech-to-Text is still not present (no microphone on the robot),
//  but TTS is back: Gemini's text replies are spoken out loud.
//
//  Typical amplifier: MAX98357A (I2S, 3.3 V logic, 5 V supply) or an
//  I2S-capable PAM8403 board.
//    BCLK -> SPK_I2S_BCLK_PIN      LRC/WS -> SPK_I2S_LRCK_PIN
//    DIN  -> SPK_I2S_DOUT_PIN      (MAX98357A: tie SD/EN high to enable)
//
//  >>> THESE PINS ARE STILL PLACEHOLDERS. <<<
//  If they stay -1 the speaker is disabled at boot, WALL-E keeps
//  working, and it just shows replies on its face instead.
// ------------------------------------------------------------
#define SPK_I2S_PORT          0
#define SPK_I2S_BCLK_PIN      TODO_CONFIGURE_GPIO
#define SPK_I2S_LRCK_PIN      TODO_CONFIGURE_GPIO
#define SPK_I2S_DOUT_PIN      TODO_CONFIGURE_GPIO
#define SPK_I2S_DATA_BITS     16

// Optional amplifier enable / mute pin. -1 = not wired to the ESP32.
#define SPK_ENABLE_PIN        TODO_CONFIGURE_GPIO

// MAX98357A / most I2S amps are active HIGH. Set 0 if yours is
// active low.
#define SPK_ENABLE_ACTIVE_HIGH 1

// Software volume, 0..10. Clamped in firmware.
#define SPK_VOLUME            7

// How much decoded audio to hold in RAM while it plays.
//
// This is the whole reason TTS streams instead of buffering: Gemini
// returns 24 kHz PCM, which is 48 kB per SECOND, and the base64 in the
// HTTP response is 64 kB per second. A 10 second answer would need
// over a megabyte if it were buffered whole. Instead the response is
// read in chunks, base64-decoded on the fly and pushed straight into
// this ring, so the firmware never holds more than this at once.
//
// 24576 bytes = 0.5 s at 24 kHz. Bigger = smoother under a slow link,
// at the cost of RAM.
#define SPK_RING_BYTES         24576

// Playback pacing: how far ahead of real time we are allowed to queue.
// Keeps the main loop responsive while the audio streams in.
#define SPK_PREFETCH_MS        120

// LOAD ONLY WHEN NEEDED
// The I2S driver, its DMA buffers and the 24 kB ring are the second
// biggest RAM cost in this firmware, and for most of its life WALL-E
// is silent. With this on, audio_output.h allocates the ring and
// installs the I2S driver the first time something actually needs to
// be spoken, and releases both again once the last sentence finishes.
// The amplifier enable pin is dropped in between, so the amp is not
// even powered while WALL-E is quiet.
#define SPK_LAZY_LOAD           1
// How long after the last sentence the audio stack is released.
#define SPK_RELEASE_IDLE_MS     1500

// ------------------------------------------------------------
// 7. Wi-Fi
// ------------------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_MS   20000
#define WIFI_RETRY_INTERVAL_MS   10000
#define WIFI_OFFLINE_CHECK_MS     5000
#define WIFI_DHCP_TIMEOUT_MS     10000

// ============================================================
//  8. (REMOVED - the ESP32-WROOM radio remote)
// ============================================================
//  There is no longer a handheld remote. The companion app is the
//  only controller, so ESP-NOW is gone from the robot entirely: one
//  radio, one controller, one set of rules, and no radio channel to
//  keep in sync with a router.
//
//  Everything the remote used to do is now either an on-screen
//  button or something only a phone can do:
//
//     buttons          ->  the app's controls
//     hold to drive    ->  the app's joystick, re-sent every 250 ms
//     turn around      ->  app button  (WALLE_CMD_TURN_AROUND)
//     say something    ->  app button  (WALLE_CMD_TALK)
//     a keyboard       ->  the app's text box (WALLE_CMD_ASK)
//     a MICROPHONE     ->  the app's speech recognition
//
//  The speech recognition is the real gain, and it was not
//  previously possible at all. The remote had no microphone, and
//  adding one to the robot would have meant an I2S capture buffer
//  competing with the TTS ring for RAM, plus a speech recogniser in
//  the firmware. In the app it is a browser API; the robot only ever
//  handles TEXT. See docs/APP_INTEGRATION.md section 5.

// ============================================================
//  9. NETWORK SERVICES
// ============================================================

// ---- Gemini ----
#define GEMINI_HOST              "generativelanguage.googleapis.com"
#define GEMINI_PATH              "/v1beta/models/gemini-2.0-flash:generateContent"
#define GEMINI_API_KEY_HEADER    "x-goog-api-key"   // sent as a header, never in a log
#define GEMINI_TIMEOUT_MS        15000
#define GEMINI_MAX_TOKENS        256
#define GEMINI_TEMPERATURE       0.9f
// WALL-E's persona. Keep it short - it is sent on every request.
#define GEMINI_SYSTEM_PROMPT \
  "You are WALL-E, a small grumpy but lovable wheeled robot. " \
  "You are funny, curious and speak in 1-3 very SHORT sentences. " \
  "Never use lists, markdown or emoji. Talk like a grumpy little movie robot."

// ---- Text-to-Speech: Gemini ----
//
//  WHY GEMINI AND NOT GROQ
//  Groq has no text-to-speech endpoint at all (it is a text/LLM and
//  Whisper-STT platform), so "Gemini or Groq" resolves to Gemini.
//  Gemini's TTS models return:
//      "inlineData": { "mimeType": "audio/L16;codec=pcm;rate=24000",
//                      "data": "<base64 PCM>" }
//  L16 is raw signed 16-bit little-endian PCM - no WAV header and,
//  crucially, NO MP3. That means the ESP32 needs no audio decoder at
//  all, which is what makes this viable on a microcontroller.
//
//  Uses the SAME API key as the chat model above (WALLE_GEMINI_API_KEY).
//  No new secret is needed.
#define TTS_HOST                 "generativelanguage.googleapis.com"
#define TTS_PATH                 "/v1beta/models/gemini-2.5-flash-preview-tts:generateContent"
#define TTS_API_KEY_HEADER       "x-goog-api-key"
#define TTS_TIMEOUT_MS           20000

// Prebuilt voices (single speaker). Some need a paid tier; "Kore",
// "Puck", "Charon", "Fenrir", "Aoede" are the usual free ones.
// List: https://ai.google.dev/gemini-api/docs/speech-generation
#define TTS_VOICE                "Puck"

// Tone direction. Prepended to the text as an instruction, which is how
// Gemini TTS is steered - there is no separate "style" parameter.
#define TTS_STYLE_PREFIX \
  "Say this in a dry, slightly grumpy but endearing little robot voice, " \
  "clearly and at a normal pace: "

// Gemini TTS charges per audio token, and a long reply produces a lot of
// audio. WALL-E's persona already keeps replies to 1-3 short sentences;
// this cap is the backstop that stops one rambling answer from talking
// (and billing) for a minute.
#define TTS_MAX_CHARS            240

// Gemini always returns 24 kHz L16. Declared here so the I2S driver and
// the ring buffer can be sized without hard-coding the number.
#define TTS_SAMPLE_RATE          24000

// Speak replies automatically whenever WALL-E talks to Gemini
// (jokes, idle chatter, "say ..."). Turn off to keep speech to explicit
// commands only.
#define TTS_SPEAK_REPLIES        1

// ---- Persona ----
//
// The APP owns the personality: it sends a compact JSON persona right
// after connecting (see shared/walle_protocol.h), so a demo can change
// who the robot is without reflashing.
//
// What is configured HERE is only the fallback used when the app has
// not sent one yet - so a robot is never voiceless, and the firmware
// still works if you drive it from the serial console with no phone.
//
// The buffer sizes are the hard limits. A payload field longer than
// its buffer is REFUSED (WALLE_ERR_BAD_ARG) rather than truncated: a
// robot whose name and whose voice disagree is far harder to debug
// than one that politely declines.
//
// Sizing: a text frame is 240 bytes total. The JSON scaffolding
// ({"n":"","s":"","m":"","p":""}) costs 27, so the prompt can never
// exceed ~193 characters. 208 leaves headroom without letting an
// oversized prompt through.
#define PERSONA_NAME_MAX         24
#define PERSONA_SUFFIX_MAX       24
#define PERSONA_MOOD_MAX         16
#define PERSONA_PROMPT_MAX       216

// The compiled-in fallback personality.
#define PERSONA_DEFAULT_NAME     "WALL-E"
#define PERSONA_DEFAULT_SUFFIX   "Beep."

// ============================================================
//  10. MOTION / SPEED
// ============================================================
#define SPEED_STOP             0
#define SPEED_SLOW             90    // 0..255
#define SPEED_WALK             150
#define SPEED_FAST             200
#define SPEED_TURN             120
#define MOTOR_RAMP_STEPS       12    // soft-start ramp steps
#define MOTOR_RAMP_DELAY_MS    20

// ============================================================
//  10b. CHASSIS GEOMETRY  (open-loop, no encoders)
//  ============================================================
//  These three numbers are what make "move 4 steps" and "turn
//  around" work. There are NO wheel encoders on this robot, so every
//  distance is an OPEN-LOOP ESTIMATE: the firmware works out how long
//  a command should take, then times it.
//
//  That is good enough for "drive over there and stop" and good
//  enough for a dance. It is NOT good enough to guarantee a metre.
//
//  HOW TO CALIBRATE (do it once, with the robot on its wheels)
//    1. Set MANEUVER_CALIBRATION 1 and flash the test firmware.
//    2. Run the "steps" test with 10 steps and put a mark where the
//       front of the chassis stops.
//    3. Measure the real distance with a tape.
//    4. new_turn = old_turn * measured / intended
//    5. Write the result into MANEUVER_MS_PER_TURN_360 and repeat
//       with MANEUVER_CM_PER_SECOND until both are within ~10%.
//    6. Set MANEUVER_CALIBRATION back to 0.
//
//  SPEED_MULTIPLIER is the fine trim left at the end, because motor
//  speed drifts with battery level. 1.0 = as calibrated.
#define MANEUVER_CALIBRATION        0   // 1 = log distances while driving

// How long one full 360 degree pivot on the spot takes at SPEED_TURN.
#define MANEUVER_MS_PER_TURN_360    1400

// Real distance per second at MANEUVER_SPEED, in centimetres.
#define MANEUVER_CM_PER_SECOND      28

// Trim applied to every computed duration. Raise it if the robot
// under-travels (batteries flatter = it creeps), lower it to shorten.
#define MANEUVER_SPEED_MULTIPLIER   1.0f

// Speed used for the measured maneuvers, so calibration is stable.
#define MANEUVER_SPEED              SPEED_WALK

// One "step" is this many centimetres. 10 cm reads naturally as a
// small shuffle; 25 cm as a proper stride.
#define STEP_DISTANCE_CM            10

// Guard rails. A controller cannot ask for 0 steps or 5000 steps.
#define WALLE_MAX_STEPS             50
#define MANEUVER_MAX_MS             8000

// Set by the remote's MODE button / the app: when off, WALL-E never
// drives itself. The cliff sensor still works and still stops it.
#define MANEUVER_SLOW_CAUTION       1   // drop to SENSOR_CAUTION_SPEED on WARN

// ============================================================
//  11. BEHAVIOUR / AUTONOMY TIMINGS
// ============================================================
#define BEHAVIOR_TICK_MS            50      // main state machine resolution
#define IDLE_TO_EXPLORE_MS          15000  // how long WALL-E sits before exploring
#define EXPLORE_MOVE_MS_MIN        700
#define EXPLORE_MOVE_MS_MAX        1800
#define EXPLORE_OBSERVE_MS          1200   // "look around" pause
#define EXPLORE_ACT_CHANCE_PCT      35     // chance to do a random fun action
#define CONVO_IDLE_GAP_MS_MIN       45000  // min gap between self-initiated chats
#define CONVO_IDLE_GAP_MS_MAX       120000
#define JOKE_CHANCE_PCT             30     // when self-initiating, tell a joke instead
#define DANCE_CHANCE_PCT            25

// (the VAD wake-word thresholds that used to live here were part of
//  the microphone/STT path and were removed with it)

// ============================================================
//  12. DANCE TIMING
// ============================================================
#define DANCE_STEP_MS           300    // one beat
#define DANCE_PAUSE_MS          500    // the freeze-frame between phrases
#define DANCE_LOOPS             2      // 0 = loop until told to stop
#define DANCE_WAVE_MS           120    // per-frame wiggle while "dancing in place"

// ============================================================
//  13. OLED
// ============================================================
#define OLED_FRAME_MS           60     // ~16 fps, easy on the S3
#define OLED_BLINK_INTERVAL_MS  2600
#define OLED_SPLASH_MS          2000

// ============================================================
//  14. Boot safety
// ============================================================
// Motors are always stopped before anything else happens.
#define BOOT_MOTOR_STOP_MS      600
// Never let the robot drive itself further than this without a state change.
#define MAX_DRIVE_MS            4000

//  15. APP LINK  (the phone talks to the robot)
//  ============================================================
//  The app is the ONLY controller. It speaks the EXACT same
//  WallePacket as the serial console, over TCP, and every command lands
//  in the same command_dispatch.cpp. There is no app-specific behaviour
//  anywhere in the firmware, so the app can never drift from the
//  robot's rules.
//
//  Why TCP: a phone cannot speak a raw peer-to-peer radio protocol, and
//  the robot is on that same Wi-Fi network anyway. A TCP server is one
//  small object, needs no libraries, and is the only transport that can
//  carry the variable-length text frames used by ask, speak and
//  set_persona.
//
//  Find the robot at its IP, printed on the serial log at boot. This is a
//  raw TCP socket, not HTTP - see docs/APP_INTEGRATION.md for the full
//  contract, which is also written as a prompt to hand to the CLI building
//  the app.
//
#define WALLE_ENABLE_APP_LINK   1
#define APP_TCP_PORT            8080

// One connected app at a time. A second connection replaces the
// first, which is what you want when an app reloads mid-session.
#define APP_ALLOW_REPLACE_CLIENT 1

// How long a connected app may be silent before the robot stops
// driving on its behalf. Same idea as a remote's link timeout, which
// the radio controller used to need.
#define APP_TIMEOUT_MS          700

// How long the remote keeps priority after the app last drove.
#define APP_CONTROL_HOLD_MS     600

// Push robot state / sensor readings at this floor (ms). The robot
// also pushes on EVENTS (state change, cliff, ack), so this is only
// a keepalive, not a stream.
#define APP_STATUS_INTERVAL_MS  1000

// Turn this off to make the robot completely unreachable from the
// network while still allowing the radio remote.
#define APP_LINK_LOG_PACKETS    0

// ============================================================
//  16. SAFETY GUARD
//  ============================================================
//  One authority decides whether the wheels may move. Everything
//  that could move the robot asks it first: the dispatcher (remote,
//  app, serial), the maneuver sequencer, the dance and the
//  autonomous behaviour.
//
//  Two things block the wheels, in priority order:
//    1. THE CLIFF SENSOR says there is a drop, or it has stopped
//       reporting at all. Nothing overrides this.
//    2. THE ROBOT IS TALKING. When WALL-E is thinking (Gemini) or
//       speaking (TTS) it must be standing still: a robot that
//       rolls around while it talks is a robot nobody can hear.
//
//  The guard is deliberately small and sits in front of the motors
//  rather than inside them, so "who is allowed to move" is answerable
//  by reading one file.
#define SAFETY_BLOCK_ON_CLIFF   1   // sensor blocks motion
#define SAFETY_BLOCK_ON_VOICE   1   // Gemini/TTS blocks motion

// Set after a cliff stop: back away, then carry on. This is what
// makes WALL-E recover instead of just freezing forever.
#define SAFETY_BACKAWAY_STEPS   3
#define SAFETY_BACKAWAY_MS      700
#define SAFETY_BACKOFF_DELAY_MS 400   // pause before trying to move again
// How long after a cliff stop the robot refuses to drive itself
// again, so it does not immediately roll off the same edge.
#define SAFETY_COOLDOWN_MS      2500
