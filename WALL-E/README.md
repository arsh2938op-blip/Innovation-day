# WALL-E 🤖🎮

An AI robot on an **ESP32-S3**, driven by a handheld **ESP32-WROOM** remote.

```
┌──────────────────────┐        ESP-NOW        ┌────────────────────────────┐
│  ESP32-WROOM remote  │ ─────────────────────▶ │  ESP32-S3 robot            │
│  (remote_wroom/)     │ ◀───────────────────── │  (robot_s3/)               │
│  buttons only        │   ack / status / state │  motors, OLED, behaviour   │
└──────────────────────┘                        └────────────────────────────┘
                                                            ▲
                          ┌─────────────────────────────────┘
                          │  WebSocket
                    ┌─────┴──────┐
                    │ companion  │   (separate repository)
                    │    app     │
                    └────────────┘
```

## Layout

| Folder | Target | What it is |
|--------|--------|------------|
| `robot_s3/` | **ESP32-S3** | The WALL-E brain: motors, OLED face, state machine, autonomy, dance, Gemini text AI, app link, remote link |
| `remote_wroom/` | **ESP32-WROOM** | The handheld remote: buttons in, commands out. No robot logic |
| `shared/` | both | `walle_protocol.h` — the command/status vocabulary. Constants only, no code |

The two firmwares are **separate PlatformIO projects** and share no source
files, so neither can accidentally be compiled for the other. The only thing
in common is a header of `enum`s and a 10-byte packet struct.

## Build

```bash
# the robot
cd robot_s3     && pio run -e walle_s3       # or: pio run -t upload

# the remote
cd remote_wroom && pio run -e walle_remote   # or: pio run -t upload
```

## Start here

- **[`robot_s3/README.md`](robot_s3/README.md)** — hardware, GPIO config,
  expressions, behaviour, and **§7 explains the remote protocol, the timeout
  safety and the command priority rules**.
- **[`remote_wroom/README.md`](remote_wroom/README.md)** — how to build and
  flash the remote, and **§2 how to set the button pins**.

## What changed in the S3 rework

- **Board:** ESP32-C3 → **ESP32-S3** (motors, OLED, behaviour, Gemini and the
  app link all moved here).
- **Removed:** STT, TTS, the microphone, the speaker, `STATE_LISTENING` /
  `STATE_SPEAKING` and `EXPR_LISTENING` / `EXPR_SPEAKING`, along with their
  API keys, config, tests and documentation. WALL-E no longer listens or
  speaks.
- **Kept:** motors, OLED, expressions, the state machine, autonomous
  behaviour, dance, Wi-Fi, Gemini, and the Robot API v1 surface the
  companion app depends on.
- **Added:** the wireless remote link (`remote_link.*`) and a single command
  dispatcher (`command_dispatch.*`) that the remote, the serial console and
  the app all share.

> ⚠️ **The button pins and the robot's GPIO pins are still placeholders**
> (`TODO_CONFIGURE_GPIO` / `TODO_CONFIGURE_REMOTE_GPIO`). Both firmwares build
> and boot, and report honestly which hardware is not configured, but neither
> can drive a motor or read a button until you fill those in.
