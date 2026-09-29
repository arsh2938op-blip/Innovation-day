# WALL-E Robot — ESP32-S3 🤖

The **main WALL-E brain**. A small mobile AI toy robot built around an
**ESP32-S3**: four motors, an OLED face, a state machine, autonomous
behaviour, dance, Gemini text AI, the companion-app link, and a
wireless link to a handheld remote.

> **STT and TTS have been removed.** WALL-E no longer has a microphone
> and no longer speaks. It still uses Gemini — the reply is shown on the
> OLED and forwarded to the app. See §7.

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
| 4 | **Motor driver** | 4-channel PWM + direction, e.g. TB6612FNG or DRV8833 |
| 5 | **ESP32-WROOM board** | the handheld remote — see `../remote_wroom/` |
| 6 | **Buttons** | on the remote only |
| 7 | **Camera** | ⚠️ depends on the exact board — see §11 |
| 8 | **Power** | LiPo + appropriate regulator, plus a **separate 5 V rail for the motors** |
| 9 | **Wiring** | Jumper wires, common ground, breadboard / perfboard |

> ⚠️ **Motor power**: never power TT motors from the ESP32 3V3 pin. Use a
> separate battery/regulator and tie the grounds together. Add a 100 µF
> capacitor across the motor supply — brownouts will crash the S3.

The microphone, I²S speaker and audio amplifier are **no longer part of the
robot** — they were removed with STT/TTS. The I²S pins are now free.

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

The remote's buttons have their own placeholder list in
`../remote_wroom/include/config.h` (`TODO_CONFIGURE_REMOTE_GPIO`).

Motor index order used everywhere in the code:

```
0 = front-left    1 = back-left    2 = front-right    3 = back-right
```

### 2.2 Pin safety rules for the ESP32-S3

* **Do not use GPIO 26–32** — they are wired to the SPI flash / PSRAM on most
  S3 modules.
* **Do not use GPIO 45 or 46** — strapping pins that select the flash voltage.
  Pulling them at boot can stop the board booting.
* **GPIO 0 and 3** are strapping pins too; GPIO 0 also drives the on-board LED.
* **GPIO 19/20** are USB D−/D+ — only free if you do not use USB for flashing.
* If your motor driver is **active-low**, flip the `digitalWrite` logic in
  `src/motor_controller.cpp::writePins()` (one place, clearly marked).

### 2.3 Verify your pins

Run the hardware test build (§10). It tests each motor on its own, so if motor 3
turns backwards you just swap its two direction pins in `config.h`.

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
│   │   ├── remote_link.*            # ESP-NOW transport + safety watchdog
│   │   ├── wifi_manager.*           # connect / reconnect / offline mode
│   │   ├── motor_controller.*       # 4 motors, ramping, emergency stop
│   │   ├── oled_display.*           # face + expression system
│   │   ├── gemini_client.*          # text → text
│   │   ├── camera_manager.*         # camera (stub — see §11)
│   │   ├── robot_state.*            # the state machine
│   │   ├── behavior.*               # autonomy + Gemini logic
│   │   ├── dance.*                  # the dance routine
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

The `lib_deps` list is **unchanged** by the STT/TTS removal: the audio
libraries that are gone were all part of the ESP-IDF core, not PlatformIO
libraries. No audio codec library, no HTTP client library, no AI SDK.

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

The STT and TTS keys that used to be documented here were removed with the
voice pipeline. **Gemini is now the only API key the robot needs.**

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

## 8. Expressions (OLED)

`oled.setExpression(EXPR_...)` drives everything. Implemented states:

| Expression | Face |
|------------|------|
| `EXPR_BOOT` | plain eyes, blinking |
| `EXPR_IDLE` | plain eyes, blinking |
| `EXPR_THINKING` | one half-closed eye, one wide |
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
 1) OLED expressions        6) Camera
 2) Motor 1                 7) Wi-Fi
 3) Motor 2                 8) Gemini
 4) Motor 3                 9) Wireless remote
 5) Motor 4                 0) Exit (start the robot)
```

Useful details:

* **Motors** are always stopped between tests, and each motor is driven
  `+120 → −120 → stop` so you can see it run forwards and backwards and fix the
  direction pins.
* **Wireless remote** listens for 15 s and prints the link state — the fastest
  way to confirm the two boards see each other.
* Missing hardware prints an explicit reason instead of failing silently.
* Motors are driven to `STOP` between every test, and nothing in the menu except
  tests 2–5 can make a wheel turn.

The microphone, speaker, STT and TTS tests were removed with those features.

### Serial console (normal build)

Open the monitor at 115200 baud and type:

| Command | Effect |
|---------|--------|
| `help` | list the commands |
| `fwd` `back` `left` `right` `rotl` `rotr` | drive |
| `stop` | stop everything NOW (and resume autonomy) |
| `dance` / `explore` / `idle` | modes |
| `auto on` / `auto off` | autonomous behaviour on/off |
| `happy` `thinking` `surprised` `confused` `face idle` | expressions |
| `joke` | ask Gemini for a joke |
| `say <text>` | send text to Gemini; the answer appears on the OLED |
| `status` | Wi-Fi, state, peripherals, remote link, autonomy |

**The console goes through the same dispatcher as the wireless remote**, so
the two can never disagree about priority. That is why `fwd` and
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
| Any critical error | `motors.emergencyStop()` |

Other guarantees: no infinite loops, no `delay()` longer than ~120 ms in the
main loop, every network client has an explicit timeout, and `STATE_MOVING` is
only ever entered by code that also stops the robot again.

---

## 13. Hardware limitations

* **RAM** — removing STT/TTS removed the firmware's peak-memory moment (a 2 s
  audio clip needed ~234 kB of heap). Steady-state use is now small, and the S3
  has far more RAM than the old C3.
* **No voice** — by design. There is no microphone, no speech synthesis and no
  wake word. Commands come from the remote, the serial console or the app.
* **No obstacle detection** — see §9.5.
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
- [ ] Create `include/secrets.h` from the template (§6)
- [ ] Verify motor directions with hardware test 2–5
- [ ] Set the remote's button pins and confirm the channel matches (§7)
- [ ] Measure the actual Wi-Fi signal / confirm the 5 V motor rail

### Nice to have

- [ ] **Obstacle avoidance** — add a ToF/ultrasonic sensor and implement
      `ObstacleDetector` (§9.5)
- [ ] **Camera vision** — capture a frame, send it to Gemini, let WALL-E
      *describe* what it sees (§11)
- [ ] **Battery voltage** monitoring on an ADC pin, feeding `WALL_ST_BATTERY`
- [ ] Persist the Gemini persona / motor speeds in NVS
- [ ] Route the **companion app** through `command_dispatch.cpp` too, so app
      movement commands are subject to the same P1 priority rule as the
      remote's
- [ ] More dance routines

---

## 15. Quick start

```bash
cp include/secrets.example.h include/secrets.h   # 1. add your keys
#    edit include/config.h                        # 2. set the GPIO pins
pio run -e walle_s3_test -t upload               # 3. test each part
pio run -t upload                                 # 4. build the robot
pio device monitor                                # 5. type: help

# 6. flash the remote (separate project, separate folder)
cd ../remote_wroom && pio run -t upload
```

Have fun. 🤖
