// ============================================================
//  REMOTE CONTROLLER CONFIGURATION (ESP32-WROOM)
//  ------------------------------------------------------------
//  EVERY remote-specific value lives here. No other file in this
//  project contains a raw GPIO number, a timing or a channel.
//
//  >>> THE BUTTON PINS BELOW ARE PLACEHOLDERS. <<<
//  The physical remote has not been specified yet, so the defaults
//  are TODO_CONFIGURE_REMOTE_GPIO (-1). With them unset the
//  firmware still boots and still runs: it just logs that no
//  buttons are configured and the safety stop still works.
//
//  HOW TO CONFIGURE ONCE THE REMOTE IS BUILT
//  1. Wire each button between its GPIO and GND (they are pulled up
//    internally, so no external resistor is needed).
//  2. Replace the -1 values below with the real GPIO numbers.
//  3. Confirm the pins against the "Avoid" list further down.
//  4. Change REMOTE_ACTIVE_LOW if your buttons read backwards.
//
//  On the classic ESP32 (WROOM) avoid:
//    * GPIO 6..11  - wired to the SPI flash, unusable
//    * GPIO 34,35,36,39 - input only, no internal pull-up
//    * GPIO 0, 2, 12, 15   - strapping pins: anything that pulls at
//                            boot time can stop the board booting
//    * GPIO 6, 7, 8, 9, 10 - flash
//    * GPIO 1, 3  - UART0, used by the serial monitor
// ============================================================
#pragma once

#include <Arduino.h>
#include "walle_protocol.h"

// ------------------------------------------------------------
//  0. Placeholder sentinel
// ------------------------------------------------------------
#define TODO_CONFIGURE_REMOTE_GPIO  (-1)
#define REMOTE_PIN_IS_UNSET(p)      ((p) < 0)

// Buttons are active LOW (idle HIGH) because every ESP32 GPIO has
// an internal pull-up. Set to 0 if your remote uses active-high.
#define REMOTE_ACTIVE_LOW           1

// ------------------------------------------------------------
//  1. Remote control buttons
//  >>> PLACEHOLDERS - set the real pins before using. <<<
// ------------------------------------------------------------
#define BTN_FORWARD_PIN   TODO_CONFIGURE_REMOTE_GPIO
#define BTN_BACK_PIN      TODO_CONFIGURE_REMOTE_GPIO
#define BTN_LEFT_PIN      TODO_CONFIGURE_REMOTE_GPIO
#define BTN_RIGHT_PIN     TODO_CONFIGURE_REMOTE_GPIO
#define BTN_STOP_PIN      TODO_CONFIGURE_REMOTE_GPIO   // emergency stop
#define BTN_DANCE_PIN     TODO_CONFIGURE_REMOTE_GPIO
#define BTN_MODE_PIN      TODO_CONFIGURE_REMOTE_GPIO   // autonomy on/off

// Optional: two more expression buttons, disabled while unset.
#define BTN_EXPR_PIN      TODO_CONFIGURE_REMOTE_GPIO
#define BTN_SURPRISE_PIN  TODO_CONFIGURE_REMOTE_GPIO

// Optional on-board LED showing link state. -1 disables it.
#define REMOTE_LED_PIN    TODO_CONFIGURE_REMOTE_GPIO

// ------------------------------------------------------------
//  2. Button behaviour
// ------------------------------------------------------------
#define REMOTE_DEBOUNCE_MS     25    // ignore contact chatter
#define REMOTE_HOLD_REPEAT_MS  50    // re-send a held direction this often
#define REMOTE_LONGPRESS_MS    700   // "long press" threshold on MODE

// ------------------------------------------------------------
//  3. Wireless link (ESP-NOW)
// ------------------------------------------------------------
//  MUST match the robot. See robot_s3/include/config.h section 8
//  for why the two boards have to agree on a channel.
//
//  0 = scan for the robot on whatever channel it is on (the robot
//  always transmits a HELLO, so the remote can find it). Set a real
//  channel (1-13) if you want to skip the scan.
//  The scan is the default because it is the only setting that
//  works whether or not the robot is joined to your Wi-Fi.
#define REMOTE_CHANNEL            0
#define REMOTE_FALLBACK_CHANNEL   6

// How often to send a PING keepalive. Well under the robot's
// REMOTE_TIMEOUT_MS so the robot never trips its watchdog while
// the remote is merely idle.
#define REMOTE_PING_INTERVAL_MS   150

// How long the remote waits for ANY packet from the robot before it
// reports the link as lost. Must be comfortably larger than the
// robot's status interval, otherwise the LED flickers.
#define REMOTE_LOST_TIMEOUT_MS    2500

// The robot's MAC, if you want to skip discovery entirely.
// Leave all-zero to let the remote learn it from the first packet.
//  >>> OPTIONAL - discovery is automatic, so this is normally unused. <<<
#define REMOTE_PEER_MAC           {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

// Print every packet and link event. Useful while setting up.
#define REMOTE_VERBOSE_LOG        1

// ------------------------------------------------------------
//  4. Serial
// ------------------------------------------------------------
#define REMOTE_SERIAL_SPEED   115200
