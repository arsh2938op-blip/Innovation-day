# WALL-E 🤖🎮

An AI robot on an **ESP32-S3**, driven by a phone app. It will not walk off
the edge of the table.

```
┌──────────────────────┐   TCP :8080   ┌──────────────────────────────┐
│  companion app       │ ─────────────▶│  ESP32-S3 robot              │
│  (Capacitor/Android) │◀───────────── │  (robot_s3/)                 │
│                      │  same packets │  motors via a driver module  │
│  · speech recognition│  as the serial│  OLED face, behaviour       │
│  · joystick + buttons│    console    │  HC-SR04 cliff sensor       │
│  · the personality   │               │  Gemini chat + TTS voice     │
└──────────────────────┘               └──────────────┬───────────────┘
                                                       │
                                        ┌──────────────┴───────────────┐
                                        │  safety guard: one authority │
                                        │  on "may the wheels move?"    │
                                        └───────────────────────────────┘
```

## Layout

| Folder | What it is |
|--------|------------|
| `robot_s3/` | The firmware. The only project. Motors, driver module, OLED, the cliff sensor, the state machine, autonomy, dance, Gemini chat, TTS, and the app link |
| `shared/walle_protocol.h` | The wire format. Constants only, no code — included by the firmware with `-I`, so it cannot drift from the app |

There is **no second project**. The ESP32-WROOM handheld remote has been
removed, and with it ESP-NOW: one radio, one controller, one set of rules,
and no radio channel to keep in sync with a router.

## The app is the only controller

Every command — drive, dance, explore, expression, ask, persona — arrives at
`robot_s3/src/command_dispatch.cpp`. The app and the serial console are two
callers of the same class, which is why the app can never drift from the
robot's rules.

### Speech recognition moved into the app

The robot has **no microphone** and does no STT. The phone listens, the
robot thinks:

```
 phone mic → app speech recognition → TEXT over TCP → robot
             → Gemini → TTS → I²S → amplifier → the robot's own speaker
```

This is a deliberate division of labour, and the only part that could not
have been done on the firmware: an I²S capture buffer would compete with the
TTS ring for RAM, and a recogniser has no business being in an embedded
image. In the app it is a browser API. The robot only ever handles text.

### The personality comes from the app too

On every connect the app sends a compact-JSON *persona* — name, reply suffix
and the Gemini system instruction — so a demo can change who the robot is
without reflashing anything. The firmware keeps a compiled-in default so a
robot driven from the serial console is never voiceless.

## Build

```bash
cd robot_s3
pio run -e walle_s3_test -t upload   # the hardware test menu
pio run -e walle_s3       -t upload   # the robot
```

Then find the robot's IP on its serial log at boot and type it into the
app's Connect tab.

## Three things to do before it moves

1. **Measure `SENSOR_NOMINAL_GROUND_CM`.** The cliff sensor is useless until
   this is right on the real robot. Run hardware test **14** and copy the
   printed ground distance into `robot_s3/include/config.h`. A guessed value
   means WALL-E either never moves or drives straight off the table.
2. **Fit the 1 k / 2 k divider on the HC-SR04 ECHO pin.** The sensor drives
   ECHO to 5 V and the S3 is 3.3 V only. Skipping this damages the ESP32.
3. **Use a 4-channel driver module.** Two × TB6612FNG is the reference
   design. A 2-channel driver locks the wheel pairs and the robot cannot
   steer.

## Current state

- **Builds clean:** `walle_s3` and `walle_s3_test`, zero warnings.
- **Not tested on hardware.** Nothing has ever run on a real board.
- **Pins are still placeholders** (`TODO_CONFIGURE_GPIO`). The firmware
  boots, reports honestly which hardware is not configured, and refuses
  motion commands that need pins it does not have.

## Documentation

- **[`robot_s3/README.md`](robot_s3/README.md)** — hardware, GPIO config, the
  driver module, the cliff sensor and its geometry, expressions, the state
  machine, safety, and how to build and flash.
- **[`robot_s3/docs/APP_INTEGRATION.md`](robot_s3/docs/APP_INTEGRATION.md)** —
  **the app contract**: byte layouts, every command and status, the persona,
  the 700 ms watchdog, a TypeScript reference client, a Web Speech API
  example, a Python smoke test, and **a ready-to-paste prompt for the CLI
  that will modify the app.**