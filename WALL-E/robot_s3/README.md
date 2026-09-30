# WALL-E Robot — ESP32-S3 🤖

The **main WALL-E brain**. A small mobile AI toy robot built around an
**ESP32-S3**: four motors, an OLED face, a state machine, autonomous
behaviour, dance, Gemini text AI, the companion-app link, and a
wireless link to a handheld remote.

> **TTS is back - WALL-E speaks.** Gemini replies are turned into speech
> with the **Gemini TTS API** and played through the I2S amplifier.
> Speech-to-Text is still absent: the robot has no microphone, so you
> talk to it from the remote, the serial console or the app. See §7-§8.
>
> **Gemini, not Groq:** Groq has no text-to-speech endpoint at all (it is
> a text/LLM + Whisper-STT platform), so "Gemini or Groq" resolves to
> **Gemini**. Its TTS models return `audio/L16;codec=pcm;rate=24000` -
> raw signed 16-bit PCM, **no MP3, no WAV header** - so the ESP32 needs
> no audio decoder. It reuses the chat API key, so there is **no new
> secret to configure**.

```
ESP32-WROOM remote ──ESP-NOW──▶ ESP32-S3 (this firmware) ──▶ motors / OLED
                                                             │
  companion app ──WebSocket──▶ ESP32-S3 ◀──Gemini API────────┘ (text only)
```

The ESP32-S3 is the **physical brain** and the only thing that drives the
motors. Every controller — the remote, the serial console, and later the app
— funnels through one dispatcher (`src/command_dispatch.cpp`).

The remote is a **separate firmware** in `../remote_wroom/`, built for a plain
ESP32-WROOM. See [`../remote_wroom/README.md`](../remote_wroom/README.md).

---

## 1. Hardware list

| # | Part | Notes |
|---|------|-------|
| 1 | **ESP32-S3 dev board** | e.g. `esp32-s3-devkitc-1`. ⚠️ See the camera warning in §11 |
| 2 | **0.96" OLED** | SSD1306, 128×64, I²C, usually address `0x3C` |
| 3 | **4× DC motors** | TT gear motors with wheels |
| 4 | **Motor driver module** | **4 independent channels** — 2× TB6612FNG is the reference design (§4b) |
| 5 | **HC-SR04 ultrasonic** | the table-edge sensor, mounted tilted **down at 45°** (§4c) |
| 6 | **I²S amplifier + 8 Ω speaker** | e.g. MAX98357A — WALL-E's voice (§7A) |
| 7 | **ESP32-WROOM board** | the handheld remote — see `../remote_wroom/` |
| 8 | **Buttons** | on the remote only |
| 9 | **Camera** | ⚠️ depends on the exact board — see §11 |
| 10 | **Power** | LiPo + appropriate regulator, plus a **separate 5 V rail for the motors** |
| 11 | **Wiring** | Jumper wires, common ground, breadboard / perfboard |
| 12 | **2× 1 k / 2 k resistors** | the HC-SR04 ECHO voltage divider (§4c) — not optional |

> ⚠️ **Motor power**: never power TT motors from the ESP32 3V3 pin. Use a
> separate battery/regulator and tie the grounds together. Add a 100 µF
> capacitor across the motor supply — brownouts will crash the S3.

> ⚠️ **HC-SR04 power**: the sensor runs on 5 V and drives ECHO to 5 V. The S3
> is 3.3 V only, so **ECHO must go through a 1 k / 2 k divider**, and the
> sensor and the ESP32 must share a ground.

The **amplifier and speaker** are **not yet wired**: see §2 for the
`SPK_I2S_*` placeholders. There is still **no microphone** — Speech-to-Text
stays on the remote or the app.

---

## 2. ⚠️ GPIO configuration — read this first

**The exact pinout of your board is not known yet, so no pins are assigned.**

Every pin in this project is the placeholder:

```c
#define TODO_CONFIGURE_GPIO  (-1)
```

Any subsystem whose pins are still `TODO_CONFIGURE_GPIO` is **automatically
disabled at boot**, logs a clear warning on the serial console, and WALL-E
carries on with the features that do work. Nothing will drive a wrong pin.

**All pins live in one file: [`include/config.h`](include/config.h).**
Change them there and nowhere else.

### 2.1 What you must fill in

Open `include/config.h` and replace each `TODO_CONFIGURE_GPIO`:

| Section | Defines |
|---------|---------|
| **3. OLED (I²C)** | `OLED_I2C_SDA_PIN`, `OLED_I2C_SCL_PIN`, `OLED_I2C_ADDR` |
| **4. Motor driver** | `MOTOR_ENABLE_PIN`, `MOTOR_ENABLE2_PIN`, `MOTOR_L1_PIN`…`MOTOR_R2_PIN`, and each motor's two `IN1`/`IN2` direction pins |
| **4b. Cliff sensor** | `SENSOR_TRIG_PIN`, `SENSOR_ECHO_PIN` — **plus `SENSOR_NOMINAL_GROUND_CM`, which you must measure** |
| **5. Camera** | all `CAMERA_*_PIN` (only if you enable the camera) |
| **6. Speaker** | `SPK_I2S_BCLK_PIN`, `SPK_I2S_LRCK_PIN`, `SPK_I2S_DOUT_PIN`, `SPK_ENABLE_PIN` |

The remote's buttons have their own placeholder list in
`../remote_wroom/include/config.h` (`TODO_CONFIGURE_REMOTE_GPIO`).

Motor index order used everywhere in the code:

```
0 = front-left    1 = back-left    2 = front-right    3 = back-right
```

### 2.1a ⚠️ The three settings that will otherwise waste your afternoon

1. **`SENSOR_NOMINAL_GROUND_CM`** — the cliff sensor is useless until
   this is measured on the real robot. Guess it and WALL-E either never
   moves (thinks it is falling) or walks straight off the table. Run the
   hardware test, option **14**, hold the robot on the table, and copy
   the printed `ground` value into `config.h`. See §4b.
2. **`MOTOR_ENABLE2_PIN`** — with two TB6612 boards you have two STBY
   wires. Leave the second unset and half the robot silently never runs.
3. **The HC-SR04 ECHO divider** — the sensor drives ECHO to 5 V and the
   S3 is **not** 5 V tolerant. It must go through a 1 k / 2 k divider.
   Skipping this damages the ESP32. See §4b.

### 2.2 Pin safety rules for the ESP32-S3

* **Do not use GPIO 26–32** — they are wired to the SPI flash / PSRAM on most
  S3 modules.
* **Do not use GPIO 45 or 46** — strapping pins that select the flash voltage.
  Pulling them at boot can stop the board booting.
* **GPIO 0 and 3** are strapping pins too; GPIO 0 also drives the on-board LED.
* **GPIO 19/20** are USB D−/D+ — only free if you do not use USB for flashing.
* If your driver board is **active-low** on STBY, set `MOTOR_ENABLE_ACTIVE_HIGH 0`
  in `config.h` rather than editing code.

### 2.3 Verify your pins

Run the hardware test build (§10). Option **13** checks the driver module
(optionally toggling the enable line so you can hear it click), options
**2–5** test each motor on its own so that if motor 3 turns backwards you
just swap its two direction pins in `config.h`.

---

## 3. Project structure

```
WALL-E/
├── shared/
│   └── walle_protocol.h   # ★ the command/status vocabulary, used by BOTH
│                          #   firmwares. Constants only, no code.
├── robot_s3/              # ← THIS firmware
│   ├── platformio.ini     # build config + the walle_s3_test env
│   ├── include/
│   │   ├── config.h       # ★ ALL hardware/API/timing config lives here
│   │   ├── log.h          # [TAG] message macros
│   │   └── secrets.example.h  # copy to secrets.h, never commit
│   ├── src/
│   │   ├── main.cpp                 # tiny: setup + loop
│   │   ├── command_dispatch.*       # ★ the single command interface + priority
│   │   ├── safety.*                 # ★ the ONE authority on "may the wheels move?"
│   │   ├── cliff_sensor.*           # HC-SR04, tilted down at 45° — table-edge stop
│   │   ├── obstacle_detector.h      # the sensor interface (no behaviour code)
│   │   ├── maneuver.*               # timed motions: N steps, turn around, retreat
│   │   ├── remote_link.*            # ESP-NOW transport + safety watchdog
│   │   ├── app_link.*               # companion app over TCP, same packets
│   │   ├── wifi_manager.*           # connect / reconnect / offline mode
│   │   ├── motor_controller.*       # 4 motors via a driver module, ramping, e-stop
│   │   ├── oled_display.*           # face + expression system
│   │   ├── gemini_client.*          # text → text
│   │   ├── tts_client.*             # text → streamed PCM (Gemini TTS)
│   │   ├── audio_output.*           # I²S ring-buffer speaker, loads only when speaking
│   │   ├── camera_manager.*         # camera (stub — see §11)
│   │   ├── robot_state.*            # the state machine
│   │   ├── behavior.*               # autonomy + Gemini logic
│   │   ├── dance.*                  # the dance routine
│   │   └── hardware_test.*          # test menu + serial console
│   ├── docs/
│   │   └── APP_INTEGRATION.md       # ★ the app contract (also a prompt for a CLI)
│   │   └── hardware_test.*          # test menu + serial console
│   └── README.md
└── remote_wroom/          # ← the separate handheld remote
    ├── platformio.ini
    ├── include/config.h
    ├── src/{main,remote_input,remote_link}.*
    └── README.md
```

---

## 4. Required libraries

Deliberately minimal, all via PlatformIO (`platformio.ini`):

| Library | Why |
|---------|-----|
| `Adafruit SSD1306` + `Adafruit GFX Library` | the OLED face |
| `ArduinoJson` | parsing Gemini JSON |
| `HTTPClient` (built into the ESP32 Arduino core) | HTTPS requests |
| `esp_now.h` (built in) | the wireless remote link |

The `lib_deps` list is **unchanged by adding TTS**: everything audio needs
(`driver/i2s.h`, `esp_http_client.h`, mbedTLS base64) is part of the
ESP-IDF core rather than a PlatformIO library. No audio codec library, no
HTTP client library, no AI SDK — Gemini TTS is deliberately requested in
raw PCM so no decoder is ever needed.

The same is true of the **cliff sensor** and the **app link**: the
HC-SR04 is driven with bare `digitalRead`/`micros()` and the app link is
a `WiFiServer` from the core. Neither adds a single dependency.

---

## 4b. The motor driver module

**The ESP32-S3 never drives a motor.** Every motor pin is a *logic* input
to a driver module; current comes from the battery and the H-bridge does
the switching. `MOTOR_DRIVER_NAME` in `config.h` records what the
firmware believes it is driving.

### You need four independent channels

Differential steering — `turnLeft`, `turnRight`, the arcs — works by
running the left and right wheels at **different speeds**. That requires
one H-bridge per wheel.

| Driver | Channels | Usable here? |
|---|---|---|
| **2 × TB6612FNG** | 2 + 2 = 4 | ✅ reference design |
| 2 × DRV8833 | 2 + 2 = 4 | ✅ |
| 4 × L9110S | 4 | ✅ (weaker; fine for small TT motors) |
| 1 × TB6612FNG | 2 | ❌ locked pairs, no steering |
| 1 × DRV8833 | 2 | ❌ same |
| 1 × L298N | 2 | ❌ same, plus ~2 V dropped across it |

If you only have a 2-channel driver, set `MOTOR_COUNT 2` in `config.h`:
the pin table then uses `MOTOR_L1` (left) and `MOTOR_R1` (right), and the
chassis becomes skid-steer. It drives straight and spins, and cannot
arc. The firmware logs `Fewer than 4 channels: steering is skid-steer only`.

### Wiring (2 × TB6612FNG)

Per wheel: `PWMA/AIN1/AIN2` (or `PWMB/BIN1/BIN2`) to one motor.
Both `STBY` pins go to `MOTOR_ENABLE_PIN` / `MOTOR_ENABLE2_PIN`.
`VM` to the battery, `VCC` to **3.3 V**, and the driver's GND must be tied
to the ESP32's GND.

### The enable lines

`setDriverEnabled(false)` drives every configured enable pin low and then
calls `emergencyStop()`, so putting a driver into standby can never leave
a wheel turning. The firmware raises the enable lines automatically the
first time a motion is requested, and `MOTOR_ENABLE_ACTIVE_HIGH 0` flips
the polarity if your board needs a pull-down to run.

---

## 4c. ⚠️ The cliff sensor — HC-SR04, tilted down at 45°

### The problem

WALL-E lives on a table. Drive it off the edge and it falls over and the
demo is over.

### Why it is tilted at 45°, not flat

Mounted flat, pointing straight down, the sensor only measures *the
height of the robot above the floor* — which never changes while driving.
It would notice the edge far too late.

Tilted forward so the beam lands **ahead of the wheels**, it looks at the
ground the robot is about to drive onto:

```
      *  sensor, 45° from horizontal
       \
        \  d = slant range (what the HC-SR04 reports)
  ------o------------------- floor
```

On a flat floor `d` is constant. Past the table edge the floor simply is
not there, so `d` grows quickly. The firmware converts the slant reading
into the useful number and compares it with what it should be:

```
ground_cm = slant_cm × cos(45°) = slant_cm × 0.7071
```

and that `ground_cm` is the height of the sensor above the surface —
constant while it is on the table, rising sharply at the edge.

### The wiring — ECHO needs a divider

```
   HC-SR04 ECHO ───[ 1k ]───┬─── ESP32-S3 GPIO   (SENSOR_ECHO_PIN)
                            │
                          [ 2k ]
                            │
                           GND
```

The HC-SR04 runs on 5 V and its ECHO swings to 5 V. **The ESP32-S3 is not
5 V tolerant.** 1 k / 2 k gives 3.0 V. `VCC` → 5 V, `GND` → GND, and the
sensor and the ESP32 **must share a ground** or the echo is meaningless.
`TRIG` is driven by the ESP32 at 3.3 V and can go straight through.

### Calibrate this before anything else

`SENSOR_NOMINAL_GROUND_CM` is the distance the sensor should normally
read, expressed as the vertical drop. **A guessed value is the single
most likely reason WALL-E refuses to move at all.**

1. Flash the test firmware (`-e walle_s3_test`), run option **14**.
2. Hold the robot on the table for the first 8 seconds and watch the
   `ground` column settle.
3. Copy that number into `SENSOR_NOMINAL_GROUND_CM`.
4. Lift the robot so the sensor sees nothing — that is what a table edge
   looks like — and confirm the state goes to `drop` and the robot stops.

The trip logic:

| State | Condition | Result |
|---|---|---|
| `ground` | below `NOMINAL − WARN_CLEAR_CM` | drive freely |
| `warn` | inside the band | drive, but capped at `SENSOR_CAUTION_SPEED` |
| `drop` | above `NOMINAL + TRIP_DROP_CM` | **STOP immediately**, back away, cool down |
| `fault` | `SENSOR_FAULT_LIMIT` consecutive misses | **STOP immediately** |

### Why a missing echo is a fault, not "safe"

An unplugged sensor must never look like a clear road. After
`SENSOR_FAULT_LIMIT` failed reads the state becomes `fault` and the
safety guard stops the robot. The HC-SR04's "nothing there" signal is a
~38 ms burst, not an error, so the driver times it out and calls it a
fault rather than believing it.

### One sample per tick, median of five

Five blocking reads in a row would stall the main loop for up to 60 ms —
exactly the kind of stall that makes a safety system useless. So the
driver takes **one** echo per `SENSOR_INTERVAL_MS` (70 ms, above the
HC-SR04's 60 ms minimum retrigger), keeps the last five, and uses the
**median**. A single wild reading on a soft or angled surface cannot move
the robot.

### What happens on a drop

`SafetyGuard::update()` runs first in `loop()` and reacts to the state
*change* (not every iteration):

1. `motors.emergencyStop()` — no ramp, no debounce, no state transition
   in the way.
2. Cancel the maneuver and the dance, so nothing resumes.
3. Request `IDLE` and show a surprised face.
4. Block the wheels for `SAFETY_COOLDOWN_MS`.
5. When the floor appears again, back away `SAFETY_BACKAWAY_STEPS`
   steps and carry on.

If a remote or the app owns the wheels at the moment of the drop, the
guard does **not** auto-recover — the human is in charge.

---

## 5. Build & flash

Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI), then:

```bash
# from the robot_s3/ folder
pio run                 # build
pio run -t upload       # build + flash over USB
pio device monitor      # serial console at 115200 baud
```

If PlatformIO cannot find your board, edit `board =` in `platformio.ini` to your
exact dev board (e.g. `esp32-s3-devkitc-1`, `adafruit_feather_esp32s3`).

First build downloads the ESP32 toolchain (~250 MB) and can take 10+ minutes.

The remote is built separately from its own folder — see
`../remote_wroom/README.md`. The two projects never share source, so a
mistake in one can never be compiled into the other.

---

## 6. Secrets / credentials

**No API key is ever committed.**

1. Copy the template:

   ```bash
   cp include/secrets.example.h include/secrets.h
   ```

   (on Windows: just copy & rename the file in Explorer)

2. Fill in `include/secrets.h`:

   ```c
   #define WALLE_WIFI_SSID      "MyNetwork"
   #define WALLE_WIFI_PASSWORD  "MyPassword"
   #define WALLE_GEMINI_API_KEY "AIza..."
   ```

3. `include/secrets.h` is already in `.gitignore`. Commit `secrets.example.h`
   only.

`config.h` picks `secrets.h` up automatically with `__has_include()`. If it is
missing, the project still compiles with empty placeholders and WALL-E runs in
offline mode with a warning.

### Where the keys come from

| Key | Where |
|-----|-------|
| Gemini | <https://aistudio.google.com/app/apikey> |

**One key covers both the chat model and TTS.** `WALLE_GEMINI_API_KEY` is sent
as the `x-goog-api-key` header to both `gemini-2.0-flash:generateContent` and
`gemini-2.5-flash-preview-tts:generateContent`, so there is nothing extra to
configure for speech.

**Gemini is now the only API key the robot needs** — it covers both the chat
model and speech.

---

## 7. The wireless remote (ESP-NOW)

The ESP32-WROOM handheld sends commands to the S3 over **ESP-NOW**.

### 7.1 Why ESP-NOW

ESP-NOW is ESP32-to-ESP32 traffic at the Wi-Fi MAC layer. For a local
point-to-point remote it is the right tool:

* **No router, no access point, no internet, no pairing.** Both boards just
  need the same channel.
* **Low latency.** A 10-byte command frame is roughly one management-frame
  slot; measured in the low single-digit milliseconds on a clear channel.
* **Tiny overhead.** No connection, no GATT, no profile — the whole protocol
  is a 10-byte struct, which is why it beats BLE here on complexity.
* **No extra library.** `esp_now.h` ships inside the ESP-IDF core, so the
  remote firmware has **no `lib_deps` at all** and builds in seconds.
* It is the classic ESP32-to-ESP32 link and the one the vendor's own examples
  target.

**The one trade-off to know about:** ESP-NOW shares the Wi-Fi radio *and
channel* with the robot's station connection. While WALL-E is joined to your
router it sits on the router's channel, and the remote must be on that same
channel. With the default `WALLE_REMOTE_PIN_CHANNEL = 0` the robot simply
follows the router and prints the channel it chose at boot, so you set the
remote to match. If the robot is offline it falls back to
`WALLE_REMOTE_FALLBACK_CHANNEL` (6).

> BLE would use a genuinely separate radio and therefore be immune to the
> channel question — at the cost of a connection model, a GATT profile and a
> far larger firmware. If you later find yourself fighting channel conflicts
> in a busy 2.4 GHz environment, that is the moment to switch. The command
> protocol in `../shared/walle_protocol.h` is transport-agnostic, so only the
> two `remote_link.*` files would change.

### 7.2 Configuration (robot side, `config.h` §8)

| Define | Meaning |
|--------|---------|
| `WALLE_REMOTE_PIN_CHANNEL` | `0` = follow the router's channel |
| `WALLE_REMOTE_FALLBACK_CHANNEL` | channel to use when offline (6) |
| `REMOTE_TIMEOUT_MS` | **safety stop if nothing is heard for this long** (400 ms) |
| `REMOTE_CONTROL_HOLD_MS` | how long a remote command keeps priority (600 ms) |
| `REMOTE_STATUS_INTERVAL_MS` | floor on the reverse status direction (1000 ms) |

### 7.3 Command protocol

One fixed 10-byte little-endian packet, defined once in
[`../shared/walle_protocol.h`](../shared/walle_protocol.h):

| Byte | Field | Meaning |
|------|-------|---------|
| 0 | `magic` | `0xA5` — sanity check |
| 1 | `version` | `0x01` — protocol revision |
| 2 | `type` | `0x01` command, `0x02` status |
| 3 | `cmd` | command id or status id |
| 4 | `value` | small argument (state, error code) |
| 5 | `seq` | rolling counter |
| 6 | `flags` | bit 0 = button still held |
| 7 | `reserved` | `0` |
| 8–9 | `arg` | `uint16` LE (e.g. millivolts) |

Commands: `move_forward`, `move_backward`, `turn_left`, `turn_right`,
`rotate_left`, `rotate_right`, `stop`, `dance`, `explore`, `idle`,
`autonomous_on`, `autonomous_off`, `expression_happy`,
`expression_thinking`, `expression_surprised`, `expression_confused`,
`expression_idle`, plus `hello`, `ping`, `bye` for the link itself.

Status back to the remote: `WELCOME`, `ACK` (echoes the command that ran),
`ERROR` (with a reason), `ROBOT_STATE`, `REMOTE_STATE`, `BATTY` (unused —
no sensor), `PONG`.

The robot **never sends a status stream**. It answers a command, a state
change, or a ping. A 10-byte ack per command is the entire return traffic.

### 7.4 Discovery

No MAC addresses to configure. The remote broadcasts `HELLO`; the robot
replies unicast to whoever sent it and remembers that MAC; from then on the
remote sends straight to the robot. A re-flashed remote is picked up
automatically because the robot deletes the stale peer and learns the new one.

### 7.5 Low latency and the safety timeout

A held button sends its command on the **press edge** and then re-sends every
`REMOTE_HOLD_REPEAT_MS` (50 ms) while it is held. Releasing every direction
button sends `stop`.

That refresh is what makes the timeout safe rather than twitchy:

* the repeat (50 ms) is far below `REMOTE_TIMEOUT_MS` (400 ms), so a healthy
  link never trips the watchdog;
* a packet lost to interference costs 50 ms of motion, not a full stop;
* if the remote genuinely disappears — battery flat, out of range, crashed —
  the robot stops within `REMOTE_TIMEOUT_MS` using `emergencyStop()`.

Two further guards exist, so a single lost packet can never leave the wheels
turning:

1. `remoteLink::update()` — the link-level watchdog (above).
2. `CommandDispatcher::tick()` — a backstop that stops the wheels if a motion
   command goes stale without a `stop`.

The timeout watchdog lives in the link, **not** the dispatcher, precisely so
that "the remote vanished" is a condition the transport can always answer.

### 7.6 Connection state

`RemoteLinkState` is one of `REMOTE_DISCONNECTED`, `REMOTE_CONNECTING`,
`REMOTE_CONNECTED`, `REMOTE_TIMEOUT`. Transitions are logged
(`[S3] Remote CONNECTED`, `[S3] Remote TIMEOUT`) and pushed to the remote
once, on the transition. The OLED shows `remote ok` / `remote lost` /
`no remote` — but **only when the state changes**, so it cannot flicker or
fight the behaviour layer for the status line.

`status` on the serial console prints the current link state, whether the
remote holds control, and whether autonomy is on.

### 7.7 Command priority

Resolved once, in `command_dispatch.cpp`:

| Priority | Rule |
|----------|------|
| **P0** | `stop` / `bye` from **any** source always runs. It cancels dance, a Gemini call, exploration and remote driving at once. It can never be refused or queued. |
| **P1** | While the remote holds control, movement commands from **other** sources are **refused** and logged. Exactly one source can own the wheels — there is never a contest. |
| **P2** | Otherwise the most recent movement command wins, and WALL-E goes back to driving itself afterwards. |
| **P3** | Mode and expression commands are accepted from any source, except that anything which would *move* the robot is ignored while the remote holds control. |

Two deliberate details:

* A refused command still returns an `ERROR` status, so the remote can tell
  the user *why* nothing happened.
* Only a **remote** stop resumes autonomous behaviour. A stop from the app or
  the console parks the robot, so it cannot drive off by itself immediately
  after somebody hit the emergency stop.

### 7.9 Voice over the remote: `TALK` and `JOKE`

Two voice commands were added to `shared/walle_protocol.h` so the handheld can
make WALL-E speak without any PC in the loop:

| Command | What the robot does |
|---------|--------------------|
| `WALLE_CMD_TALK` (`0x15`) | asks Gemini for a line, then speaks it |
| `WALLE_CMD_JOKE` (`0x16`) | asks Gemini for a joke, then speaks it |

They are plain fixed-size commands, so **no new wire format and no
variable-length frame** was introduced — the 10-byte packet is unchanged and
`sizeof(WallePacket) == WALLE_PROTO_PACKET_SIZE` still holds.

Both go through the normal dispatcher, so they are refused with a reported
error (`NOT_CONFIGURED` when the speaker pins or API key are missing, `BUSY`
while the remote holds the wheels) rather than silently doing nothing.

The **remote never carries the Gemini key and never handles audio** — it only
names an action. The robot asks, speaks, and stays the only brain.

---

## 7A. Text-to-Speech (Gemini)

### The pipeline

```
Gemini chat reply (text)
        │
        ▼
TtsClient::speak()            src/tts_client.cpp
        │  POST /v1beta/models/gemini-2.5-flash-preview-tts:generateContent
        │  generationConfig.responseModalities = ["AUDIO"]
        │  speechConfig.voiceConfig.prebuiltVoiceConfig.voiceName = "Puck"
        ▼
{"inlineData":{"mimeType":"audio/L16;codec=pcm;rate=24000","data":"<base64>"}}
        │  read in 1 kB chunks, base64 decoded on the fly
        ▼
AudioOutput::feed()           src/audio_output.cpp
        │  24 kB ring buffer
        ▼
I²S → MAX98357A → 8 Ω speaker
```

### Why it streams instead of buffering

This is the single most important design decision in the TTS code.

Gemini returns **24 kHz signed 16-bit PCM = 48 kB per second of speech**. The
same audio inside the JSON response is base64, so **64 kB per second**. A 15
second answer is roughly **720 kB of PCM / 960 kB of base64** — and the ESP32
Arduino `HTTPClient` in this IDF version has **no streaming-body callback**, so
the "obvious" `http.POST()` + `deserializeJson()` implementation would need
over a megabyte of heap at once. It cannot work on a microcontroller.

So `speakFromGemini()` opens the connection with `esp_http_client`, reads the
body in 1 kB slices, scans each slice for the `"data":"` marker, and
base64-decodes characters into a 3-byte group as it goes — pushing each decoded
triple straight into the speaker's ring buffer. **Peak extra RAM is one HTTP
chunk plus the 24 kB ring, regardless of how long the sentence is.**

The ring buffer is sized by `SPK_RING_BYTES` (default 24 kB = 0.5 s at 24 kHz).
Bigger is smoother on a weak link, at the cost of RAM.

### Why raw PCM and not MP3

Gemini's TTS models emit `audio/L16`, which is raw PCM. That is a gift on a
microcontroller: **no MP3 decoder, no WAV header parsing, no extra library.**
The I²S driver is handed samples directly.

### Configuration (all in `include/config.h`, section 9)

| Setting | Default | Notes |
|---------|---------|-------|
| `TTS_PATH` | `.../gemini-2.5-flash-preview-tts:generateContent` | the TTS model |
| `TTS_VOICE` | `Puck` | prebuilt voice; some need a paid tier |
| `TTS_STYLE_PREFIX` | "Say this in a dry, slightly grumpy…" | Gemini is steered by prefixing an instruction — there is no separate style parameter |
| `TTS_MAX_CHARS` | 240 | caps audio tokens (and cost) per request |
| `TTS_SAMPLE_RATE` | 24000 | what Gemini returns; do not change |
| `TTS_SPEAK_REPLIES` | 1 | speak every Gemini reply automatically |
| `SPK_RING_BYTES` | 24576 | streaming ring buffer size |
| `SPK_VOLUME` | 7 | software gain, 0–10 |

### Behaviour

* Every Gemini reply is spoken automatically when `TTS_SPEAK_REPLIES` is on.
* `behavior.requestSpeak(text)` speaks arbitrary text **without** asking Gemini
  (the serial console's `speak X` command).
* `STATE_SPEAKING` holds the animated speaking face until the audio has
  actually finished playing, not merely finished downloading.
* `stop` / `halt` / `behavior.halt()` calls `speaker.stop()`, so an emergency
  stop also cuts off speech mid-word.
* Any TTS failure is logged and swallowed: the reply stays on the OLED and the
  robot carries on. **A speech failure is never a robot failure.**

### "Loaded only when needed"

For most of its life WALL-E is silent, and the I²S driver, its DMA buffers
and a 24 kB ring buffer are the second largest RAM cost in this firmware.

* `AudioOutput::begin()` **only validates the pins.** It allocates nothing and
  never touches I²S, so it is safe to call from `setup()` and forget about.
* `ensureHardware()` brings up the I²S driver and the ring the first time
  something is actually spoken.
* `releaseHardware()` frees both again `SPK_RELEASE_IDLE_MS` (1.5 s) after the
  last sentence, and drops `SPK_ENABLE_PIN` so the **amplifier is unpowered**,
  not merely muted.

The console `status` line shows `audio=loaded` / `audio=unloaded (idle)` so you
can watch it happen.

### The wheels are locked while WALL-E talks

Every conversation entry point (`requestChat`, `requestJoke`,
`requestSpeak`) calls `Behavior::parkForVoice()` **before** any network
call. That stops every wheel, cancels the dance and any maneuver, and sets
the safety guard's voice lock. Motion commands from *any* controller are
refused with `BUSY` until `conversationFinished()` releases it.

This is not decoration: a robot that rolls around while it is talking
cannot be heard and cannot be stopped by the person listening to it.

### 7.8 App and remote coexistence

Both are clients of the S3; the S3 stays the brain and the only motor driver.

```
app    ──WebSocket──▶ ┐
                     ├──▶ command_dispatch ──▶ motors / OLED / behaviour
remote ──ESP-NOW───▶ ┘
```

They can be used at the same time. Whichever source last issued a *movement*
command holds the wheels until it stops or times out, and §7.7 makes that
rule explicit rather than emergent. The remote never replaces the S3 and never
touches the app protocol: the firmware's Robot API v1 surface is unchanged,
so the existing companion app keeps working exactly as before.

---

## 7B. The companion app (TCP)

### The app is just another remote

There is **no app-specific code anywhere in the firmware.** `AppLink`
moves the *same* `WallePacket` as the radio remote, over TCP instead of
ESP-NOW, and hands every frame to the same `command_dispatch.cpp`. That is
the whole design: the app cannot drift from the remote's rules, because
there is only one set of rules.

```
phone / browser app ──TCP:8080──┐
                                ├──▶ command_dispatch.cpp ──▶ safety guard ──▶ motors
ESP32-WROOM remote ──ESP-NOW────┘
serial console ────UART─────────┘
```

`CommandDispatcher::notify()` fans every ack, error and state change out to
**both** links at once, so the two are structurally incapable of
desynchronising.

### Why TCP and not a second radio link

A phone cannot speak ESP-NOW, and the robot is already on the Wi-Fi network
the phone is on. A `WiFiServer` is one small object, needs no libraries, and
— unlike ESP-NOW — can carry the variable-length text frames that `ask` and
`speak` need.

### Stream framing

TCP has no message boundaries: one read can return half a packet or three
packets. So bytes are buffered and only acted on when a whole frame is
present — 10 bytes for a fixed packet, 8 + `len` for a text frame. Every
frame must start with the magic byte `0xA5`, and anything else is skipped,
which gives free resynchronisation if the app reconnects mid-frame.

### The watchdog

While the app is driving it must send something every `APP_TIMEOUT_MS`
(700 ms) or the robot stops and releases control. A phone whose screen
locks must never leave the robot driving. The app should re-send the
movement command every ~250 ms with `WALLE_FLAG_HELD` set, exactly as the
radio remote does.

### Full contract

**[`docs/APP_INTEGRATION.md`](docs/APP_INTEGRATION.md)** — byte layouts, the
full command/status/error/cliff tables, a session walkthrough, a TypeScript
reference client, a Python smoke test, and **a ready-to-paste prompt for
the CLI that will build the app side.**

---

## 9A. Motion primitives — "move N steps", "turn around"

`MotorController` says *which way* and keeps going until somebody stops it.
That is right for a held button and wrong for "drive 4 steps and stop",
which is what an app, a controller or an avoidance routine needs.

`Maneuver` adds a deadline:

| Method | Protocol command | Console |
|---|---|---|
| `startForwardSteps(n)` | `move_steps` (0x17), `arg = n` | `steps 4` |
| `startRetreatSteps(n)` | — (used by the cliff guard) | — |
| `startTurnAround()` | `turn_around` (0x18) | `turn around` |
| `startTurnDegrees(d)` | `turn_degrees` (0x1C), `arg = d` | `turn 90` |
| `startForward(ms)` etc. | — | — |

One step is `STEP_DISTANCE_CM` (10 cm). Every command goes through the
dispatcher, so the cliff guard gets to veto it exactly as it would a remote
command — and a cliff detected half way through "move 8 steps" **truncates
the maneuver** rather than being ignored until it finishes.

### These are open loop, and that is a real limitation

There are **no wheel encoders**, so a distance cannot be measured, only
estimated. The firmware converts a distance into a duration using
`MANEUVER_CM_PER_SECOND` / `MANEUVER_MS_PER_TURN_360` and then times it.
That is fine for "get over there" and fine for a dance. It drifts with
battery level and floor surface.

Calibrate once with hardware-test option **15**: drive 10 steps, measure the
real distance, then

```
new_turn = old_turn × measured ÷ intended
```

and repeat until both are within about 10 %. `MANEUVER_SPEED_MULTIPLIER` is
the fine trim left at the end.

Real odometry would need encoders on the motors — a different hardware and a
different driver.

---

## 8. Expressions (OLED)

`oled.setExpression(EXPR_...)` drives everything. Implemented states:

| Expression | Face |
|------------|------|
| `EXPR_BOOT` | plain eyes, blinking |
| `EXPR_IDLE` | plain eyes, blinking |
| `EXPR_THINKING` | one half-closed eye, one wide |
| `EXPR_SPEAKING` | eyes squash/stretch + mouth opens in time with the audio |
| `EXPR_HAPPY` | `^ ^` eyes, big smile |
| `EXPR_CONFUSED` | one open, one squinted, a `?` |
| `EXPR_SURPRISED` | round eyes, `O` mouth |
| `EXPR_ANGRY` | angled brows (also used for funny reactions) |
| `EXPR_DANCING` | eyes swinging side to side |
| `EXPR_EXPLORING` | eyes scanning left/right |
| `EXPR_OFFLINE` | `X X` eyes |
| `EXPR_ERROR` | flat line eyes |
| `EXPR_SLEEPING` | closed lids |

`EXPR_LISTENING` and `EXPR_SPEAKING` were removed with STT/TTS — they only
existed to accompany audio.

All drawing is primitive-based (`fillRoundRect`, `drawPixel`, `drawCircle`) in
`src/oled_display.cpp`. **To add an expression:** add an `EXPR_*` to the enum in
`oled_display.h`, add a `case` to `drawEyes()` and `drawMouth()`, and add one
line to `RobotStateMachine::enter()`. The redraw rate is capped at
`OLED_FRAME_MS` (60 ms) so the CPU stays free.

`oled.setStatus("offline")` shows a small label, `oled.setCaption("...")` shows
a line of text at the top — used for Gemini's answer, which is no longer
spoken.

---

## 9. Movement, autonomy and dancing

### 9.1 Motor control (`motor_controller.*`)

```cpp
motors.forward(SPEED_WALK);
motors.backward(SPEED_SLOW);
motors.turnLeft(SPEED_TURN);
motors.turnRight(SPEED_TURN);
motors.left(SPEED_SLOW);
motors.right(SPEED_SLOW);
motors.stop();
motors.emergencyStop();      // instant, no ramp - used on every error path
motors.setMotor(0, 200);     // raw per-motor control
```

All pins and speeds come from `config.h`. Speeds are 0–255
(`SPEED_SLOW` / `SPEED_WALK` / `SPEED_FAST`). Every movement ramps up over
`MOTOR_RAMP_STEPS` steps so the robot does not jerk at start-up.

### 9.2 State machine (`robot_state.*`)

```
        BOOT
          │  (motors stopped, peripherals up, Wi-Fi started)
          ▼
        IDLE ──────────────┐
          │               │ time or trigger
          ▼               │
  ┌► EXPLORING ──► MOVING ─┘
  │       │
  │       └──► OBSERVING
  │
  ├► THINKING ─────────────────────────────► IDLE
  ├► DANCING ─────────────────────────────────► IDLE
  ├► REMOTE_MANUAL ──(remote owns the wheels)──► IDLE
  └► OFFLINE (no Wi-Fi) ─────────────────────► IDLE
```

Each state has a **minimum dwell time** so it cannot flicker, and
`RobotStateMachine::enter()` **stops the motors for every state that does not
explicitly drive them**. That is the main runaway-motor guard.
`REMOTE_MANUAL` is the one state the guard deliberately skips, because
`remote_link.cpp` owns the wheels there and stops them itself on release,
timeout or link loss.

### 9.3 Autonomous behaviour (`behavior.*`)

`Behavior::update()` is a non-blocking state machine:

* **IDLE** — after a random 45–120 s gap, WALL-E picks something to do: tell a
  joke, dance, or start exploring. Only when autonomy is **on**
  (`WALLE_AUTONOMOUS_DEFAULT`, toggled at runtime by the remote's
  `autonomous_on` / `autonomous_off`).
* **EXPLORING** — random sequence of *move forward / turn left / turn right /
  stop and observe*, each for a random 0.7–1.8 s. Timings in `config.h`
  (`EXPLORE_*`).
* **THINKING** — the Gemini conversation script (§7): one bounded HTTP request
  per step, so the loop keeps running.

A remote movement command calls `behavior.suspendAutonomy()`, which parks
exploration and dance and pushes the next self-initiated action out of the way
so WALL-E does not wander the instant the remote lets go.

No planning happens on the ESP32 — the firmware only *decides when*; Gemini
decides *what to say*.

### 9.4 Dance mode (`dance.*`)

`Dance` is a table of steps, each `{l1, l2, r1, r2, durationMs, face}`:

```
spin left → spin right → forward → FREEZE (happy face) →
turn → turn back → pose (surprised) → wobble → wobble → bow
```

Timing: `DANCE_STEP_MS`, `DANCE_PAUSE_MS`, `DANCE_LOOPS` (`0` = loop forever),
`DANCE_WAVE_MS` — all in `config.h`. The face changes on every step, so the
OLED dances with the wheels. `dance.start()` / `dance.stop()` are public.

### 9.5 Obstacle avoidance — **not implemented, and honestly so**

There is **no distance sensor, no bump switch and no usable camera** in the
parts list, so WALL-E currently cannot see walls.

Rather than fake it, `behavior.h` defines an interface:

```cpp
class ObstacleDetector {
    virtual bool detect(float* proximityOut);
};
```

The default implementation always reports "clear", so WALL-E explores with
**timed turns** instead of real avoidance. To add real avoidance, implement the
interface and pass it in:

```cpp
behavior.setObstacleDetector(new MyToFDetector());
```

`Behavior::updateExploration()` already reacts to a positive detection
(back off and turn away) — that branch is currently unreachable until you supply
a detector.

---

## 10. Hardware test mode

Each piece of hardware can be verified **independently** — you do not need the
AI pipeline working, and motors are disabled by default so nothing moves unless
you ask for it.

```bash
pio run -e walle_s3_test -t upload
pio device monitor
```

The firmware boots into a serial menu:

```
 1) OLED expressions        8) Gemini
 2) Motor 1                 9) Speaker tone (440 Hz)
 3) Motor 2                10) TTS (speak a Gemini line)
 4) Motor 3                11) Wireless remote
 5) Motor 4                12) App link (TCP)
 6) Camera                 13) Driver module (STBY + channels)
 7) Wi-Fi                  14) Cliff sensor (HC-SR04)
                           15) Motion primitives / calibration
 0) Exit (start the robot)
```

Useful details:

* **Motors** are always stopped between tests, and each motor is driven
  `+120 → −120 → stop` so you can see it run forwards and backwards and fix the
  direction pins.
* **Wireless remote** listens for 15 s and prints the link state — the fastest
  way to confirm the two boards see each other.
* **App link** prints the robot's IP and the `nc` command to try it from a laptop.
* Missing hardware prints an explicit reason instead of failing silently.
* Motors are driven to `STOP` between every test, and nothing in the menu except
  tests 2–5 and 15 can make a wheel turn.

There is no microphone test: the robot has no microphone. Speech-to-Text runs
on the radio remote or the app instead (§7B).

### Serial console (normal build)

Open the monitor at 115200 baud and type:

| Command | Effect |
|---------|--------|
| `help` | list the commands |
| `fwd` `back` `left` `right` `rotl` `rotr` | drive |
| `stop` | stop everything NOW (and resume autonomy) |
| `steps <n>` | drive `n` steps and stop by itself |
| `turn <deg>` / `turn around` | pivot N degrees / 180° and stop |
| `dance` / `explore` / `idle` | modes |
| `auto on` / `auto off` | autonomous behaviour on/off |
| `happy` `thinking` `surprised` `confused` `face idle` | expressions |
| `joke` / `talk` | Gemini writes a line; WALL-E says it out loud |
| `speak <text>` | speak exactly that, without Gemini |
| `say <text>` | send text to Gemini; the answer appears on the OLED and is spoken |
| `sensor` | one cliff reading, live |
| `watch` | continuous cliff readings + the safety verdict + free heap |
| `status` | everything: state, driver, sensor, links, safety, audio |

**The console goes through the same dispatcher as the wireless remote and the
app**, so all three can never disagree about priority. That is why `fwd` and
`move_forward` behave identically.

---

## 11. ⚠️ Camera — depends on your exact board

**A plain ESP32-S3 module has no camera peripheral.** Boards sold as "ESP32-S3
with camera" are either one with an OV2640 fitted, or an **ESP32-P4** (which
has a real ISP and camera block), or a vendor-specific module needing its own
driver.

Because the exact board is unknown, the camera is **disabled**:

```c
#define WALLE_ENABLE_CAMERA 0
```

`src/camera_manager.cpp` compiles to a stub that logs
`No camera on this build` and returns nothing. `Behavior` never assumes vision.

To enable it you must:

1. Confirm your SoC has a camera block,
2. fill in every `CAMERA_*` pin in `include/config.h`,
3. add the vendor camera library to `platformio.ini`,
4. set `WALLE_ENABLE_CAMERA` to `1`,
5. implement `CameraManager::capture()` (it currently returns `nullptr`).

Until then, WALL-E explores using **movement and timing only**.

---

## 12. Robustness

The firmware is built for a physical robot that can crash, drop Wi-Fi, lose a
remote and return garbage from an API.

| Situation | What happens |
|-----------|--------------|
| Boot | Motors driven to `STOP` **before** anything else is initialised |
| Wi-Fi missing / wrong password | `OFFLINE` state, `X X` face, movement + OLED + dance + remote still work, reconnects every 10 s |
| No SSID in `secrets.h` | Starts offline, logs a warning, never crashes |
| Remote not powered on | `REMOTE_CONNECTING`; the robot drives itself as normal |
| **Remote vanishes mid-drive** | `emergencyStop()` within `REMOTE_TIMEOUT_MS`, state → `REMOTE_TIMEOUT`, OLED shows `remote lost` |
| Remote battery flat / out of range | Same as above — the repeat timer is the only thing keeping the wheels turning |
| Remote sends an unknown command | Logged, `ERROR` returned, robot unaffected |
| Remote and app both command movement | Remote holds the wheels; the app's movement command is refused and logged (P1) |
| Remote sends `stop` | Everything halts and autonomy resumes |
| Gemini fails or times out | `EXPR_CONFUSED` + `gemini?` label, back to `IDLE` |
| Gemini blocked / returns empty | Logged with the block reason, no retry loop |
| Motor pins still `TODO_CONFIGURE_GPIO` | Every motion command is refused with `NOT_CONFIGURED`; nothing drives a wrong pin |
| Camera missing | Continues with non-camera behaviour |
| **Cliff sensor sees a drop** | `emergencyStop()` (no ramp), maneuver + dance cancelled, wheels blocked, backs away and cools down |
| **Cliff sensor stops reporting** | Treated as `fault`, not "safe" — same immediate stop |
| Cliff sensor pins unset | Sensor disabled at boot; wall logged that safety is unavailable; the rest of the robot works |
| Any motion command while blocked | Refused with `WALLE_ERR_CLIFF` / `SENSOR_FAULT` / `BUSY`, never silently ignored |
| A maneuver interrupted by a cliff | Truncated at once — it does not run to its deadline |
| Any motion while WALL-E is thinking/speaking | Refused with `BUSY`; the wheels are locked for the whole conversation |
| Second driver board's STBY unset | That side never runs; test **13** reports the channel count |
| Fewer than 4 driver channels | Logged; the chassis is skid-steer and cannot arc |
| Speaker pins unset | TTS reports `Disabled`, replies stay on the OLED, robot keeps working |
| Audio idle for 1.5 s | I²S driver and ring buffer released, amplifier unpowered |
| TTS fails or times out | `EXPR_ERROR` + `tts?`, **carries on silently** — a speech failure is never a robot failure |
| No Gemini key | `tts.ready()` is false, `TALK`/`JOKE` refused with `NOT_CONFIGURED` |
| Remote presses TALK while it holds the wheels | Refused with `BUSY` — the remote always has priority |
| Remote drops out mid-drive | `REMOTE_TIMEOUT_MS` watchdog stops the wheels unconditionally |
| App drops out mid-drive | `APP_TIMEOUT_MS` watchdog stops the wheels and releases control |
| App sends a frame with a bad length | `BAD_PACKET`, and the stream resynchronises on the next `0xA5` |
| Any critical error | `motors.emergencyStop()` and `speaker.stop()` |

Other guarantees: no infinite loops, no `delay()` longer than ~120 ms in the
main loop, every network client has an explicit timeout, and `STATE_MOVING` is
only ever entered by code that also stops the robot again.

---

## 13. Hardware limitations

* **RAM — this is why TTS streams.** Gemini returns 24 kHz PCM, which is
  **48 kB per second** of speech (64 kB/sec as base64 in the JSON). A 15-second
  sentence would need over a megabyte if it were buffered. Instead the response
  is read in 1 kB chunks, base64-decoded on the fly and pushed into a
  24 kB ring buffer, so peak RAM is the ring, not the sentence length. The
  trade-off: on a very slow connection the audio can briefly underrun (an
  audible gap) because the speaker runs at real time while data arrives.
* **No speech-to-text** — by design. There is no microphone on the robot, so
  there is no wake word and no voice input. WALL-E is *spoken to* from the
  remote, the serial console or the app; it only *speaks* itself.
* **The speaker is not wired yet** — `SPK_I2S_*` are still
  `TODO_CONFIGURE_GPIO`, so TTS is inert until you set them (§2).
* **No obstacle detection** — the cliff sensor sees the floor, not walls. WALL-E
  will still drive into a chair. A front-mounted ToF module or an IR
  break-beam can be added later: implement `ObstacleDetector`
  (`src/obstacle_detector.h`) and hand it to `Behavior::setObstacleDetector()`.
  Nothing in `behavior.cpp` needs to change.
* **Distances are open loop.** There are no wheel encoders, so "4 steps" and
  "turn around" are *timed estimates* from `MANEUVER_CM_PER_SECOND` and
  `MANEUVER_MS_PER_TURN_360`, not measurements. They drift with battery level
  and floor surface. Calibrate once (§9A); for real odometry you need encoders
  and a different motor driver.
* **The cliff sensor is fail-safe, not fail-certain.** A `fault` means the
  sensor is not reporting, and the robot stops — it does **not** mean there is
  definitely a drop. It also cannot see obstacles, and the HC-SR04 is blind to
  soft surfaces (a desk mat can eat the beam), so `SENSOR_NOMINAL_GROUND_CM` has
  to be measured on the surface WALL-E will actually run on.
* **The cliff sensor must be angled correctly.** 45° is the design point. A
  near-vertical mount only measures height and will not see the edge early; a
  near-horizontal mount looks too far ahead and will stop short of everything.
* **One app client at a time.** A second TCP connection replaces the first. That
  is deliberate — an app reloading mid-session should not leave a dead socket
  holding the control lock.
* **A browser cannot open a raw TCP socket.** The app link needs Node, Python,
  React Native, a desktop app, or a small bridge. See §9 of
  `docs/APP_INTEGRATION.md`.
* **No camera vision** — see §11.
* **Wheels only** — no balance/inversion recovery like the film WALL-E.
* **Battery monitoring is not implemented** — the `WALL-E_ST_BATTERY` status
  message exists in the protocol but no sensor feeds it, so it is never sent.
* **The remote's button pins are not chosen yet** — see
  `../remote_wroom/README.md`.

---

## 14. TODO

### Must do before it runs

- [ ] Fill in every `TODO_CONFIGURE_GPIO` in `include/config.h` (§2)
- [ ] **Measure `SENSOR_NOMINAL_GROUND_CM` on the real surface** — hardware test
      **14**, then write the value into `config.h` (§4c). Without this WALL-E
      either never moves or walks off the table.
- [ ] **Fit the 1 k / 2 k divider on the HC-SR04 ECHO pin** before connecting it
      to the S3. 5 V into a 3.3 V pin damages the ESP32 (§4c)
- [ ] Set `MOTOR_ENABLE2_PIN` if you use two driver boards (§4b)
- [ ] Create `include/secrets.h` from the template (§6)
- [ ] Verify motor directions with hardware test 2–5
- [ ] Set the remote's button pins and confirm the channel matches (§7)
- [ ] Measure the actual Wi-Fi signal / confirm the 5 V motor rail

### Nice to have

- [ ] **Obstacle avoidance in front** — the cliff sensor only watches the floor.
      A front ToF or IR break-beam implementing `ObstacleDetector` plugs in
      without touching `behavior.cpp` (§13)
- [ ] **Encoders** — turn the open-loop maneuvers into real odometry (§9A)
- [ ] **Camera vision** — capture a frame, send it to Gemini, let WALL-E
      *describe* what it sees (§11)
- [ ] **Battery voltage** monitoring on an ADC pin, feeding `WALL_ST_BATTERY`
- [ ] Persist the Gemini persona / motor speeds in NVS
- [ ] More dance routines

---

## 15. Quick start

```bash
cp include/secrets.example.h include/secrets.h   # 1. add your keys
#    edit include/config.h                        # 2. set the GPIO pins
#                                             #    + MEASURE SENSOR_NOMINAL_GROUND_CM
pio run -e walle_s3_test -t upload               # 3. run test 14 FIRST (the sensor)
pio run -t upload                                 # 4. build the robot
pio device monitor                                # 5. type: help

# 6. flash the remote (separate project, separate folder)
cd ../remote_wroom && pio run -t upload
```

If WALL-E refuses to move at all, the first thing to check is
`SENSOR_NOMINAL_GROUND_CM` — the safety guard is doing its job and
believing the floor has gone. The console's `status` line shows the sensor
state and the exact reason the wheels are blocked.

Have fun. 🤖
