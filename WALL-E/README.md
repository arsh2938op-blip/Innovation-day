# WALL-E 🤖🎮

An AI robot on an **ESP32-S3**, driven by a handheld **ESP32-WROOM** remote
or by a companion app. It will not walk off the edge of the table.

```
┌──────────────────────┐      ESP-NOW       ┌──────────────────────────────┐
│  ESP32-WROOM remote  │ ──────────────────▶ │  ESP32-S3 robot              │
│  (remote_wroom/)     │ ◀────────────────── │  (robot_s3/)                 │
│  buttons only        │  ack / status      │  motors via a driver module  │
└──────────────────────┘                    │  OLED face, behaviour       │
                                            │  HC-SR04 cliff sensor       │
┌──────────────────────┐      TCP :8080     │  Gemini chat + TTS voice    │
│  companion app       │ ──────────────────▶ │                              │
│  (to be built)       │ ◀────────────────── └──────────────┬───────────────┘
└──────────────────────┘   same packets, same dispatcher    │
                              as the radio remote           │
                                            ┌───────────────┴───────────────┐
                                            │  safety guard: one authority │
                                            │  on "may the wheels move?"    │
                                            └───────────────────────────────┘
```

## Layout

| Folder | Target | What it is |
|--------|--------|------------|
| `robot_s3/` | **ESP32-S3** | The WALL-E brain: motors via a driver module, OLED face, state machine, autonomy, dance, the HC-SR04 cliff sensor, Gemini text AI, TTS voice, the app link and the remote link |
| `remote_wroom/` | **ESP32-WROOM** | The handheld remote: buttons in, commands out. No robot logic |
| `shared/` | both | `walle_protocol.h` — the command/status vocabulary. Constants only, no code |

The two firmwares are **separate PlatformIO projects** and share no source
files, so neither can accidentally be compiled for the other. The only thing
in common is a header of `enum`s and a 10-byte packet struct.

## Three things worth knowing before you flash anything

1. **The ESP32-S3 does not drive the motors.** Every motor pin is a logic
   input to a **driver module**, and you need **four independent channels**
   (2 × TB6612FNG is the reference design) or the robot cannot steer. See
   `robot_s3/README.md` §4b.
2. **The HC-SR04 is tilted down at 45°** to watch the floor ahead of the
   wheels, so WALL-E stops at the edge of a table instead of falling off it.
   Its **ECHO pin needs a 1 k / 2 k voltage divider** — 5 V would damage the
   S3. See `robot_s3/README.md` §4c.
3. **`SENSOR_NOMIONAL_GROUND_CM` must be measured** on the real robot. It is
   the distance the sensor should normally read, and a guessed value means
   WALL-E either never moves or drives straight off the table.

## Build

```bash
# the robot
cd robot_s3     && pio run -e walle_s3       # or: pio run -t upload
cd robot_s3     && pio run -e walle_s3_test  # the hardware test menu

# the remote
cd remote_wroom && pio run -e walle_remote   # or: pio run -t upload
```

## Start here

- **[`robot_s3/README.md`](robot_s3/README.md)** — hardware, GPIO config,
  the driver module, the cliff sensor, expressions, behaviour, and **§7 the
  remote protocol, timeout safety and command priority**.
- **[`robot_s3/docs/APP_INTEGRATION.md`](robot_s3/docs/APP_INTEGRATION.md)** —
  **the app contract**, including a ready-to-paste prompt for the CLI that
  will build the app side.
- **[`remote_wroom/README.md`](remote_wroom/README.md)** — how to build and
  flash the remote, and **§2 how to set the button pins**.

## How the pieces fit

* **Three controllers, one dispatcher.** The radio remote (ESP-NOW), the app
  (TCP) and the serial console all send the *same* `WallePacket` into the same
  `command_dispatch.cpp`. There is no app-specific code in the firmware, so
  the app can never drift from the remote's rules. Acks and status are
  fanned out to every controller at once.
* **One authority on movement.** `safety.*` decides whether the wheels may
  move, and everything that could move the robot asks it first. Two things
  block motion: the **cliff sensor** (a drop, or a sensor that stopped
  reporting) and **WALL-E talking** (Gemini thinking or TTS speaking). Neither
  can be overridden by any command.
* **The modules load when they are needed.** The I²S driver, its DMA buffers
  and the audio ring come up on the first sentence and are released 1.5 s
  after the last one, with the amplifier unpowered in between.

## Current state

- **Built and compiling:** `walle_s3`, `walle_s3_test`, `walle_remote` — all
  green, no warnings.
- **Not tested on hardware.** Nothing has ever run on a real board.
- **Pins are still placeholders** (`TODO_CONFIGURE_GPIO` /
  `TODO_CONFIGURE_REMOTE_GPIO`). Both firmwares boot and report honestly which
  hardware is not configured, but neither can drive a motor or read a button
  until you fill those in.