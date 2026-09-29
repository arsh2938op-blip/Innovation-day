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

// Wireless remote link (ESP-NOW peer: the ESP32-WROOM handheld).
// STT/TTS were removed from this firmware, so voice input is gone;
// the remote is now the way you drive WALL-E by hand.
#define WALLE_ENABLE_REMOTE     1

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
//  The S3 devkit exposes far more usable GPIO than the C3 did, and
//  the I2S microphone + speaker pins are now free because STT/TTS
//  were removed, so there is plenty of room for the motors, the
//  OLED and the ESP-NOW radio.
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
// 4. Motors - 4x DC motors through a motor driver
//    Model assumed: 4 independent PWM channels (e.g. TB6612FNG /
//    DRV8833 in 4-channel mode). Direction pins are "active high".
//    If your driver is active-low, invert the macros below.
//    motor order: 0=front-left 1=front-right 2=back-left 3=back-right
// ------------------------------------------------------------
#define MOTOR_COUNT           4
#define MOTOR_ENABLE_PIN      TODO_CONFIGURE_GPIO   // driver STBY / EN pin, or -1 if none
#define MOTOR_PWM_FREQ        20000                  // 20 kHz = above human hearing
#define MOTOR_PWM_BITS        8

// left side  = index 0,1     right side = index 2,3
static const int8_t MOTOR_L1_PIN = TODO_CONFIGURE_GPIO;  // M1 PWM   (front-left)
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
// 6. Microphone / speaker  -- REMOVED
// ------------------------------------------------------------
//  STT and TTS were removed from this firmware, so there is no I2S
//  audio capture and no I2S audio playback any more. The I2S pins
//  (and the RAM the PCM buffers used) are free for the motors,
//  the OLED and the remote link. See git history for the previous
//  MIC_I2S_* / SPK_I2S_* / AUDIO_* settings.
//
//  WALL-E still talks, it just shows the answer on its face and in
//  the companion app instead of speaking it out loud.

// ------------------------------------------------------------
// 7. Wi-Fi
// ------------------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_MS   20000
#define WIFI_RETRY_INTERVAL_MS   10000
#define WIFI_OFFLINE_CHECK_MS     5000
#define WIFI_DHCP_TIMEOUT_MS     10000

// ============================================================
//  8. WIRELESS REMOTE LINK (ESP-NOW, ESP32-WROOM remote)
// ============================================================
//  Why ESP-NOW: it is ESP32-to-ESP32 at the Wi-Fi MAC layer, needs
//  no router, no access point, no pairing and no internet, and a
//  10-byte command packet costs about as much airtime as a BLE
//  advertisement. Latency is ~1 ms on an open channel.
//
//  IMPORTANT - CHANNEL: ESP-NOW and the Wi-Fi station connection
//  share one radio. While the robot is joined to the app's
//  network it sits on the router's channel, and the remote must
//  therefore use the SAME channel (see remote_wroom's
//  REMOTE_CHANNEL). If the robot is not connected to Wi-Fi it
//  falls back to WALLE_REMOTE_FALLBACK_CHANNEL. Bumping either
//  number to match your router is the only setup the remote needs.

// 0 = follow the Wi-Fi channel the robot is associated with.
// Any other value pins the radio to that channel, which is what
// you want if the robot spends most of its time offline.
#define WALLE_REMOTE_PIN_CHANNEL      0
#define WALLE_REMOTE_FALLBACK_CHANNEL 6

// Hard safety timer. If no valid packet from the remote arrives
// within this window while the remote is driving, the robot stops.
// Held buttons re-send every REMOTE_REPEAT_MS (WROOM side), which
// is far below this value, so a healthy link never trips it.
#define REMOTE_TIMEOUT_MS            400

// How long the remote keeps priority over other command sources
// after it last sent a movement command (see command_dispatch).
#define REMOTE_CONTROL_HOLD_MS       600

// Seconds between status pushes to the remote. The robot only
// sends status on events (ack, state change, error, ping reply),
// so this is purely the keepalive floor.
#define REMOTE_STATUS_INTERVAL_MS   1000

// Log every received packet. Leave at 0; the ack/timeout logging
// is already enough to debug a link.
#define REMOTE_VERBOSE_LOG            0

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

// ---- STT / TTS: REMOVED ----
//  Speech-to-Text and Text-to-Speech are gone from the robot
//  firmware, together with their hosts, paths, timeouts and
//  voice settings. Gemini above is untouched and is still used
//  for WALL-E's personality (jokes, idle chatter, "say ..." from
//  the serial console and the companion app).

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
// Motors are always driven to STOP before anything else happens.
#define BOOT_MOTOR_STOP_MS      600
// Never let the robot drive itself further than this without a state change.
#define MAX_DRIVE_MS            4000
