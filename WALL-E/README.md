# WALL-E — Innovation Day AI Robot 🤖

A small mobile AI toy robot built around an **ESP32-C3**.

WALL-E listens to you, sends what you said to **Gemini**, gets an answer back,
speaks it through a speaker, shows a face on an OLED while it does all of that,
and wanders around dancing and telling jokes when nobody is talking to it.

```
Microphone → ESP32-C3 → STT API → text → Gemini API → text → TTS API → PCM audio
                                                                             ↓
                                                          amplifier → 8 Ω speaker
```

The ESP32-C3 is the **physical brain** only. All the intelligence lives in the
cloud services, so the firmware stays small enough for a C3.

---

## 1. Hardware list

| # | Part | Notes |
|---|------|-------|
| 1 | **ESP32-C3 dev board** | With camera. ⚠️ See the camera warning in §11 |
| 2 | **0.96" OLED** | SSD1306, 128×64, I²C, usually address `0x3C` |
| 3 | **Speaker 8 Ω** | Any small passive speaker |
| 4 | **Audio amplifier** | e.g. MAX98357A (I²S, 3.3 V) or PAM8403 (analog) |
| 5 | **Microphone** | Digital I²S mic (INMP441 / ICS-43434 / MSM261) |
| 6 | **4× DC motors** | TT gear motors with wheels |
| 7 | **Motor driver** | 4-channel PWM + direction, e.g. TB6612FNG or DRV8833 |
| 8 | **Camera** | ⚠️ depends on the exact board — see §11 |
| 9 | **Power** | LiPo + appropriate regulator, plus a **separate 5 V rail for the motors** |
| 10 | **Wiring** | Jumper wires, common ground, breadboard / perfboard |

> ⚠️ **Motor power**: never power TT motors from the ESP32 3V3 pin. Use a
> separate battery/regulator and tie the grounds together. Add a 100 µF
> capacitor across the motor supply — brownouts will crash the C3.

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
| **4. Motors** | `MOTOR_ENABLE_PIN`, `MOTOR_L1_PIN`…`MOTOR_R2_PIN`, and each motor's two `IN1`/`IN2` direction pins |
| **5. Camera** | all `CAMERA_*_PIN` (only if you enable the camera) |
| **6. Microphone** | `MIC_I2S_BCLK_PIN`, `MIC_I2S_WS_PIN`, `MIC_I2S_DIN_PIN` |
| **7. Speaker** | `SPK_I2S_BCLK_PIN`, `SPK_I2S_LRCK_PIN`, `SPK_I2S_DOUT_PIN` |

Motor index order used everywhere in the code:

```
0 = front-left    1 = back-left    2 = front-right    3 = back-right
```

### 2.2 Pin safety rules for the ESP32-C3

* **Do not use GPIO 2, 8, 9** for motors, I²C pull-ups, or anything that loads
  them at boot — they are strapping pins. (GPIO 8/9 is only OK as the I²C bus
  *if* your board already has pull-ups fitted.)
* **Do not use GPIO 11–17** — they are connected to the on-board SPI flash.
* USB-serial chips (CP2102, CH340) often claim **GPIO 18/19**. Check your board.
* A common C3 devkit I²C default is **SDA = GPIO 8, SCL = GPIO 9**.
* If your motor driver is **active-low**, flip the `digitalWrite` logic in
  `src/motor_controller.cpp::writePins()` (one place, clearly marked).

### 2.3 Verify your pins

Run the hardware test build (§10). It tests each motor on its own, so if motor 3
turns backwards you just swap its two direction pins in `config.h`.

---

## 3. Project structure

```
WALL-E/
├── platformio.ini          # build config + the walle_test env
├── include/
│   ├── config.h            # ★ ALL hardware/API/timing config lives here
│   ├── log.h               # [TAG] message macros
│   └── secrets.example.h   # copy to secrets.h, never commit
├── src/
│   ├── main.cpp            # tiny: setup + loop
│   ├── wifi_manager.*      # connect / reconnect / offline mode
│   ├── motor_controller.*  # 4 motors, ramping, emergency stop
│   ├── oled_display.*      # face + expression system
│   ├── audio_input.*       # I²S mic capture + voice activity detection
│   ├── audio_output.*      # I²S speaker playback (non-blocking)
│   ├── stt_client.*        # speech → text
│   ├── gemini_client.*     # text → text
│   ├── tts_client.*        # text → PCM audio
│   ├── camera_manager.*    # camera (stub — see §11)
│   ├── robot_state.*       # the state machine
│   ├── behavior.*          # autonomy + conversation logic
│   ├── dance.*             # the dance routine
│   ├── hardware_test.*     # test menu + serial console
│   └── log.h
└── README.md
```

---

## 4. Required libraries

Deliberately minimal, all via PlatformIO (`platformio.ini`):

| Library | Why |
|---------|-----|
| `Adafruit SSD1306` + `Adafruit GFX Library` | the OLED face |
| `ArduinoJson` | parsing STT / Gemini / TTS JSON |
| `HTTPClient` (built into the ESP32 Arduino core) | HTTPS requests |
| `driver/i2s.h` (built in) | microphone + speaker |

No audio codec library, no HTTP client library, no AI SDK. That keeps the flash
and RAM footprint small enough for a C3.

---

## 5. Build & flash

Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI), then:

```bash
# from the WALL-E/ folder
pio run                 # build
pio run -t upload       # build + flash over USB
pio device monitor      # serial console at 115200 baud
```

If PlatformIO cannot find your board, edit `board =` in `platformio.ini` to your
exact dev board (e.g. `esp32-c3-devkitm-1`, `seeed_xiao_esp32c3`).

First build downloads the ESP32 toolchain (~250 MB) and can take 10+ minutes.

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
   #define WALLE_STT_API_KEY    "AIza..."   // Google Cloud key
   #define WALLE_TTS_API_KEY    "AIza..."   // Google Cloud key
   ```

3. `include/secrets.h` is already in `.gitignore`. Commit `secrets.example.h`
   only.

`config.h` picks `secrets.h` up automatically with `__has_include()`. If it is
missing, the project still compiles with empty placeholders and WALL-F runs in
offline mode with a warning.

### Where the keys come from

| Key | Where |
|-----|-------|
| Gemini | <https://aistudio.google.com/app/apikey> |
| STT / TTS | Google Cloud console → enable *Cloud Speech-to-Text* and *Cloud Text-to-Speech* → create an API key |

---

## 7. How the voice pipeline works

| Stage | Module | Endpoint (configurable in `config.h`) |
|-------|--------|----------------------------------------|
| Capture | `audio_input` | — I²S, 16 kHz / 16-bit / mono |
| STT | `stt_client` | `https://speech.googleapis.com/v1/speech:recognize` |
| Gemini | `gemini_client` | `https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent` |
| TTS | `tts_client` | `https://tts.googleapis.com/v1/text:synthesize` |
| Play | `audio_output` | — I²S → amplifier → speaker |

Design decisions worth knowing:

* **TTS returns LINEAR16 PCM, not MP3.** An MP3 decoder will not fit comfortably
  in a C3's RAM, so the firmware asks for raw PCM and writes it straight to I²S.
  This also means **no extra library**.
* **STT and TTS audio is capped at 2 seconds** (`AUDIO_MAX_RECORD_MS`). That is
  the largest clip that fits in a C3's heap: 64 kB of raw PCM + 85 kB of base64
  + 85 kB of JSON body. 2 s is enough for a short command. Raising it will fail
  the allocation in `SttClient` and log a clear out-of-memory error.
* **Recording ends automatically** using a simple voice-activity detector:
  as soon as speech is followed by `VAD_SILENCE_MS` of silence.
* **Long answers are split** into ≤`TTS_MAX_CHARS` chunks and spoken one after
  another, so Gemini is never cut off mid-sentence.
* Every request has an explicit timeout (`STT_TIMEOUT_MS`, `GEMINI_TIMEOUT_MS`,
  `TTS_TIMEOUT_MS`). A request that times out **never** hangs the robot.

### Swapping in a different TTS/STT provider

The endpoints and API-key header are constants in `config.h`
(`STT_HOST` / `STT_PATH`, `TTS_HOST` / `TTS_PATH`). Adapt the JSON body in
`src/stt_client.cpp` / `src/tts_client.cpp` and keep the interface
(`transcribe()` / `synthesize()`) unchanged — `behavior.cpp` will not need edits.

---

## 8. Expressions (OLED)

`oled.setExpression(EXPR_...)` drives everything. Implemented states:

| Expression | Face |
|------------|------|
| `EXPR_BOOT` | plain eyes, blinking |
| `EXPR_IDLE` | plain eyes, blinking |
| `EXPR_LISTENING` | tall eyes, bouncing pupil |
| `EXPR_THINKING` | one half-closed eye, one wide |
| `EXPR_SPEAKING` | eyes + mouth animate while audio plays |
| `EXPR_HAPPY` | `^ ^` eyes, big smile |
| `EXPR_CONFUSED` | one open, one squinted, a `?` |
| `EXPR_SURPRISED` | round eyes, `O` mouth |
| `EXPR_ANGRY` | angled brows (also used for funny reactions) |
| `EXPR_DANCING` | eyes swinging side to side |
| `EXPR_EXPLORING` | eyes scanning left/right |
| `EXPR_OFFLINE` | `X X` eyes |
| `EXPR_ERROR` | flat line eyes |
| `EXPR_SLEEPING` | closed lids |

All drawing is primitive-based (`fillRoundRect`, `drawPixel`, `drawCircle`) in
`src/oled_display.cpp`. **To add an expression:** add an `EXPR_*` to the enum in
`oled_display.h`, add a `case` to `drawEyes()` and `drawMouth()`, and add one
line to `RobotStateMachine::enter()`. The redraw rate is capped at
`OLED_FRAME_MS` (60 ms) so the CPU stays free.

`oled.setStatus("offline")` shows a small label, `oled.setCaption("...")` shows
a line of text at the top (used for the transcript and Gemini's answer).

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
  ├► LISTENING ─► THINKING ─► SPEAKING ─► IDLE
  ├► DANCING ─────────────────────────────────► IDLE
  └► OFFLINE (no Wi-Fi) ─────────────────────► IDLE
```

Each state has a **minimum dwell time** so it cannot flicker, and
`RobotStateMachine::enter()` **stops the motors for every state that does not
explicitly drive them**. That is the main runaway-motor guard.

### 9.3 Autonomous behaviour (`behavior.*`)

`Behavior::update()` is a non-blocking state machine:

* **IDLE** — after a random 45–120 s gap, WALL-E picks something to do: tell a
  joke, dance, or start exploring.
* **EXPLORING** — random sequence of *move forward / turn left / turn right /
  stop and observe*, each for a random 0.7–1.8 s. Timings in `config.h`
  (`EXPLORE_*`).
* **TALKING** — the conversation script (§7): `listen → transcribe → ask Gemini
  → speak`. It runs as discrete steps so the loop keeps running.

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
pio run -e walle_test -t upload
pio device monitor
```

The firmware boots into a serial menu:

```
 1) OLED expressions        7) Motor 4
 2) Microphone              8) Camera
 3) Speaker                 9) Wi-Fi
 4) Motor 1                10) Gemini
 5) Motor 2                11) STT
 6) Motor 3                12) TTS
 0) Exit (start the robot)
```

Useful details:

* **Motors** are always stopped between tests, and each motor is driven
  `+120 → −120 → stop` so you can see it run forwards and backwards and fix the
  direction pins.
* **Microphone** prints a live dBFS meter — speak and watch the bar.
* **Speaker** plays a 440 Hz tone and the **TTS** test speaks a real sentence.
* Missing hardware prints an explicit reason instead of failing silently.
* Motors are driven to `STOP` between every test, and nothing in the menu except
  tests 4–7 can make a wheel turn.

### Serial console (normal build)

Open the monitor at 115200 baud and type:

| Command | Effect |
|---------|--------|
| `help` | list the commands |
| `talk` | listen and answer |
| `joke` | ask Gemini for a joke |
| `dance` / `stop` | dance / stop everything |
| `explore` / `stop` | explore / stop everything |
| `say <text>` | send text straight to Gemini and print the answer |
| `status` | Wi-Fi, state, and which peripherals are configured |

---

## 11. ⚠️ Camera — depends on your exact board

**A plain ESP32-C3 module has no camera peripheral.** Most "ESP32-C3 + camera"
dev boards people mean are one of:

* an **ESP32-S3** with an OV2640,
* an **ESP32-P4** (has a real ISP and camera block),
* a **Waveshare ESP32-C3 camera board** — these exist and do work, but they use
  a vendor-specific camera module and driver.

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

The firmware is built for a physical robot that can crash, drop Wi-Fi and
return garbage from an API.

| Situation | What happens |
|-----------|--------------|
| Boot | Motors driven to `STOP` **before** anything else is initialised |
| Wi-Fi missing / wrong password | `OFFLINE` state, `X X` face, movement + OLED + dance still work, reconnects every 10 s |
| No SSID in `secrets.h` | Starts offline, logs a warning, never crashes |
| STT fails or times out | `EXPR_CONFUSED`, back to `IDLE`, robot keeps running |
| STT hears silence | Not treated as an error — just returns to `IDLE` |
| Gemini fails or times out | `EXPR_CONFUSED` + `gemini?` label, back to `IDLE` |
| Gemini is blocked / returns empty | Logged with the block reason, no retry loop |
| TTS fails | Error expression, **continues without speech** — the conversation is not lost |
| No speaker wired | Audio is synthesised and discarded, the robot keeps talking on the OLED |
| No microphone wired | Voice input is skipped, jokes and Gemini still work |
| Camera missing | Continues with non-camera behaviour |
| Any critical error | `motors.emergencyStop()` |

Other guarantees: no infinite loops, no `delay()` longer than ~120 ms in the
main loop, all three network clients have explicit timeouts, and
`STATE_MOVING` is only ever entered by code that also stops the robot again.

---

## 13. Hardware limitations

* **RAM** — a single STT request is the peak-memory moment in the firmware
  (~234 kB of heap for a 2 s clip). Longer recordings will fail with a clear
  out-of-memory log. The firmware logs a warning if a recording was truncated
  because it hit the time cap.
* **No MP3 playback** — see §7. Only raw PCM TTS output is supported.
* **No voice wake word** — WALL-E reacts to the serial `talk` command, or you
  can hold-to-talk with a GPIO button (add it in `main.cpp` and call
  `behavior.requestTalk()`).
* **No obstacle detection** — see §9.5.
* **No camera vision** — see §11.
* **Wheels only** — no balance/inversion recovery like the film WALL-E.
* **Battery monitoring is not implemented.**

---

## 14. TODO

### Must do before it runs

- [ ] Fill in every `TODO_CONFIGURE_GPIO` in `include/config.h` (§2)
- [ ] Create `include/secrets.h` from the template (§6)
- [ ] Verify motor directions with hardware test 4–7
- [ ] Measure and set the actual Wi-Fi signal / confirm the 5 V motor rail

### Nice to have

- [ ] **Wake word** ("Hey WALL-E") using a local keyword-spotting model
- [ ] **Obstacle avoidance** — add a ToF/ultrasonic sensor and implement
      `ObstacleDetector` (§9.5)
- [ ] **Camera vision** — capture a frame, send it to Gemini, let WALL-E
      *describe* what it sees (§11)
- [ ] **Battery voltage** monitoring on an ADC pin
- [ ] Persist the Gemini persona / volume / motor speeds in NVS
- [ ] FreeRTOS task for audio so playback never stalls the main loop
- [ ] More dance routines triggered by sound level (WALL-E dances when you clap)

---

## 15. Quick start

```bash
cp include/secrets.example.h include/secrets.h   # 1. add your keys
#    edit include/config.h                        # 2. set the GPIO pins
pio run -e walle_test -t upload                   # 3. test each part
pio run -t upload                                 # 4. build the robot
pio device monitor                                # 5. type: help
```

Have fun. 🤖
