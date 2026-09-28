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
#define WALLE_STT_API_KEY    ""
#define WALLE_TTS_API_KEY    ""
#endif

// ------------------------------------------------------------
// 1. Feature switches
// ------------------------------------------------------------
#ifndef WALLE_TEST_MODE
#define WALLE_TEST_MODE 0          // 1 = interactive hardware test menu
#endif

#define WALLE_ENABLE_MOTORS     1
#define WALLE_ENABLE_OLED       1
#define WALLE_ENABLE_AUDIO_IN   1   // microphone (I2S)
#define WALLE_ENABLE_AUDIO_OUT  1   // speaker via amplifier (I2S)
#define WALLE_ENABLE_WIFI       1

// The camera is DISABLED by default.
// Most ESP32-C3 boards have no camera peripheral; the camera modules that
// do exist need a specific SoC (e.g. ESP32-P4) and a vendor camera driver.
// Set to 1 only after you have confirmed your board actually has one and you
// have wired the camera pins in section 5 below.
#define WALLE_ENABLE_CAMERA     0

// ------------------------------------------------------------
// 2. Sentinel for pins that are not known yet
// ------------------------------------------------------------
#define TODO_CONFIGURE_GPIO  (-1)
#define PIN_IS_UNSET(p)      ((p) < 0)

// ============================================================
//  HARDWARE PINS
//  >>> FILL THESE IN FOR YOUR BOARD. <<<
//  Rule of thumb: never use the strapping pins GPIO 2, 8 and 9
//  for anything that pulls at boot time (motor drivers, I2C pull-ups).
//  Also avoid the flash pins GPIO 11-17 on most C3 modules.
// ============================================================

// ------------------------------------------------------------
// 3. OLED - SSD1306 128x64, I2C
//    Default I2C bus on C3: SDA=GPIO8 / SCL=GPIO9  (many devkits)
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
// 6. Microphone - I2S in (digital mic: INMP441 / ICS-43434 / MSM261)
//    -> STT needs 16 kHz / 16-bit / mono PCM.
// ------------------------------------------------------------
#define MIC_I2S_PORT          0
#define MIC_I2S_BCLK_PIN      TODO_CONFIGURE_GPIO
#define MIC_I2S_WS_PIN        TODO_CONFIGURE_GPIO
#define MIC_I2S_DIN_PIN       TODO_CONFIGURE_GPIO
#define MIC_I2S_DATA_BITS     32   // most mems output 24 bit data in a 32 bit slot

// Audio sample format shared by mic and speaker (must match the STT/TTS API)
#define AUDIO_SAMPLE_RATE     16000
#define AUDIO_SAMPLE_BITS     16
#define AUDIO_CHANNELS        1
// Longest clip we are willing to hold in RAM.
//
// Peak heap use for one STT request at 2 s:
//   64 kB raw PCM  +  85 kB base64  +  85 kB JSON body  ~= 234 kB
// which fits the ~290 kB of free heap on a C3. Raising this much above 2 s
// will fail the malloc in SttClient and log an out-of-memory error.
#define AUDIO_MAX_RECORD_MS   2000

// ------------------------------------------------------------
// 7. Speaker / amplifier - I2S out (MAX98357A is a common choice)
// ------------------------------------------------------------
#define SPK_I2S_PORT          1
#define SPK_I2S_BCLK_PIN      TODO_CONFIGURE_GPIO
#define SPK_I2S_LRCK_PIN      TODO_CONFIGURE_GPIO
#define SPK_I2S_DOUT_PIN      TODO_CONFIGURE_GPIO
#define SPK_I2S_DATA_BITS     16
#define SPK_VOLUME            7   // 0..10  (software gain, clamped)

// ------------------------------------------------------------
// 8. Wi-Fi
// ------------------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_MS   20000
#define WIFI_RETRY_INTERVAL_MS   10000
#define WIFI_OFFLINE_CHECK_MS     5000
#define WIFI_DHCP_TIMEOUT_MS     10000

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

// ---- Speech-to-Text (Google Cloud STT v1, synchronous) ----
#define STT_ENABLED            1
#define STT_HOST               "speech.googleapis.com"
#define STT_PATH               "/v1/speech:recognize"
#define STT_TIMEOUT_MS         20000
#define STT_LANGUAGE_CODE      "en-US"

// ---- Text-to-Speech (Google Cloud TTS v1, LINEAR16 -> no decoder needed) ----
#define TTS_ENABLED            1
#define TTS_HOST               "tts.googleapis.com"
#define TTS_PATH               "/v1/text:synthesize"
#define TTS_TIMEOUT_MS         20000
#define TTS_VOICE_LANGUAGE     "en-US"
#define TTS_VOICE_NAME         "en-US-Standard-C"   // en-US-Neural2-* if you have access
#define TTS_MAX_CHARS          180                 // keep requests small, chunk in code

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

// Reaction time to a recognised keyword (simple VAD wake word)
#define VAD_THRESHOLD_DB            -45    // dBFS threshold
#define VAD_SILENCE_MS              900    // stop recording after this much silence

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
#define OLED_FRAME_MS           60     // ~16 fps, easy on the C3
#define OLED_BLINK_INTERVAL_MS  2600
#define OLED_SPLASH_MS          2000

// ============================================================
//  14. Boot safety
// ============================================================
// Motors are always driven to STOP before anything else happens.
#define BOOT_MOTOR_STOP_MS      600
// Never let the robot drive itself further than this without a state change.
#define MAX_DRIVE_MS            4000
