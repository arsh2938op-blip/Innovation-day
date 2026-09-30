# WALL-E App Integration

**This document is the contract between the WALL-E robot firmware and
the companion app.** It is written so that another developer (or another
CLI) can build the app side from this file alone, without reading the
firmware.

The single most important thing to understand before you read the rest:

> **The app is not special.** It is a second controller, exactly like the
> ESP32-WROOM radio remote. It sends the *same* 10-byte binary command
> packets, over TCP instead of ESP-NOW, and the robot funnels both into
> one dispatcher. If the app and the remote ever disagreed about what a
> command means, that would be a firmware bug — not something the app
> has to work around.

---

## 1. Connecting

| | |
|---|---|
| Transport | TCP |
| Default port | `8080` (change with `APP_TCP_PORT` in `include/config.h`) |
| Robot address | printed on the serial log at boot as its local IP |
| Protocol | binary, little-endian, **not** HTTP, **not** JSON, **not** WebSocket |

There is **no handshake and no handshake secret**. Connect, and start
sending frames. Send a `HELLO` if you want the robot to introduce
itself.

```
app / phone  ──TCP:8080──▶  ESP32-S3 robot
                              ├─ parses frames
                              ├─ safety guard: cliff sensor + voice lock
                              └─ command_dispatch.cpp  ← the only place commands live
```

### The watchdog — read this, it will bite you

While the app is **driving** the robot, it must send **something** at
least once every `APP_TIMEOUT_MS` (default **700 ms**), or the robot
stops and releases control. That is deliberate: a phone whose screen
locks, or an app whose tab is backgrounded, must never leave the robot
driving.

Keepalive options, in order of preference:

1. **Re-send the movement command** every ~250 ms while the direction
   button is held (this is what the radio remote does). Use the
   `WALLE_FLAG_HELD` flag.
2. Send `PING` (0x31) every ~300 ms when connected but not driving.

When the robot stops an app for a timeout it sends `WALLE_ST_ERROR` with
`LINK_TIMEOUT`, so you can show "connection lost — robot stopped" and
restart your keepalive instead of reconnecting.

---

## 2. Frame formats

### 2.1 Command / status packet — 10 bytes

```
offset  size  field       notes
------  ----  ----------  ------------------------------------------------
0       1     magic       0xA5, always. Used to resynchronise the stream.
1       1     version     0x01
2       1     type        0x01 = COMMAND (app -> robot)
                          0x02 = STATUS  (robot -> app)
3       1     cmd         see the command / status tables below
4       1     value       small argument: state, error, expression
5       1     seq         rolling counter 0..255, wraps. Echo it back.
6       1     flags       bit0 = WALLE_FLAG_HELD (button physically down)
7       1     reserved    0
8       2     arg         uint16 LE — the step count, ground distance, ...
```

The struct is **not** padded, so it is exactly 10 bytes on the wire in
this order. In JS pack it with `DataView` — do not use `Buffer.write` of
a struct, the padding rules differ.

### 2.2 Text frame — 8-byte header + payload

Used only for `ask` / `speak` / the reply, and only over TCP.

```
offset  size  field       notes
------  ----  ----------  ------------------------------------------------
0       1     magic       0xA5
1       1     version     0x01
2       1     type        0x03 = TEXT
3       1     op          0x01 ask, 0x02 speak, 0x03 reply
4       1     flags       0
5       2     len         uint16 LE, payload byte count, 1..240
6       1     reserved    0
--- payload: `len` bytes of UTF-8, no terminator, no quotes, no escaping ---
```

`len` is capped at **240** on both sides; the robot truncates anything
longer. 240 characters is roughly one short sentence of speech, which
matches the robot's personality prompt ("1–3 short sentences") and keeps
the Gemini TTS cost bounded.

---

## 3. Commands (app → robot)

| Value | Name | `arg` | What the robot does |
|---|---|---|---|
| `0x01` | `move_forward` | – | drives forward **while held**; re-send or it stops |
| `0x02` | `move_backward` | – | as above |
| `0x03` | `turn_left` | – | arc left, held |
| `0x04` | `turn_right` | – | arc right, held |
| `0x05` | `rotate_left` | – | pivot on the spot, held |
| `0x06` | `rotate_right` | – | pivot on the spot, held |
| `0x07` | `stop` | – | **highest priority from any source.** Stops everything |
| `0x10` | `dance` | – | run the dance routine |
| `0x11` | `explore` | – | start autonomous exploration |
| `0x12` | `idle` | – | stop and park |
| `0x13` | `autonomous_on` | – | let WALL-E decide what to do |
| `0x14` | `autonomous_off` | – | WALL-E only moves when told |
| `0x15` | `talk` | – | ask Gemini for a line, then **say it out loud** |
| `0x16` | `joke` | – | ask Gemini for a joke, then say it |
| `0x17` | `move_steps` | step count, 1–50 | drive N steps and stop by itself |
| `0x18` | `turn_around` | – | pivot 180° and stop by itself |
| `0x1A` | `ask` | – | **followed immediately by a TEXT frame** with the question |
| `0x1B` | `speak` | – | **followed immediately by a TEXT frame** to say verbatim |
| `0x1C` | `turn_degrees` | 1–360 | pivot that many degrees and stop |
| `0x19` | `read_sensor` | – | answer with `WALLE_ST_SENSOR` right now |
| `0x20` | `expression_happy` | – | set the face |
| `0x21` | `expression_thinking` | – | |
| `0x22` | `expression_surprised` | – | |
| `0x23` | `expression_confused` | – | |
| `0x24` | `expression_idle` | – | |
| `0x30` | `hello` | – | robot replies `WALLE_ST_WELCOME` with its state |
| `0x31` | `ping` | – | robot replies `WALLE_ST_PONG` with its state |
| `0x32` | `bye` | – | clean shutdown, robot stops immediately |

### Held vs tap

For `0x01`–`0x06` set `flags |= 0x01` (`WALLE_FLAG_HELD`) while the
finger is down. The robot does not actually need the flag to keep
driving — it stops when the commands stop arriving — but sending it
lets the robot and the logs distinguish a press from a tap.

`0x07` (`stop`) is never a held command. Always send it on release.

### Timed motions do not need a keepalive loop

`move_steps`, `turn_around` and `turn_degrees` run to completion by
themselves. You do **not** need to re-send them and you do **not** need
to send `stop` afterwards. They are still subject to the cliff sensor:
the robot will cut one short if it sees a drop.

---

## 4. Status (robot → app)

| Value | Name | Carries |
|---|---|---|
| `0x80` | `WELCOME` | `value` = robot state (reply to `hello`, and on connect) |
| `0x81` | `ACK` | `cmd` = the command that ran |
| `0x82` | `ERROR` | `value` = error code (see below) |
| `0x83` | `ROBOT_STATE` | `value` = robot state |
| `0x84` | `REMOTE_STATE` | `value` = radio remote state (0–3) |
| `0x86` | `PONG` | `value` = robot state (reply to `ping`) |
| `0x87` | `SENSOR` | `value` = cliff state, `arg` = ground distance in **cm** |
| `0x88` | `CLIFF` | `value` = cliff state (sent on the transition) |

WALL-E's spoken answer is **not** a status packet — it arrives as a `TEXT`
frame with `op = 0x03` (reply), see §5.

The robot also re-sends `ROBOT_STATE` about once a second as a keepalive
floor, and pushes `SENSOR` whenever the app sends `read_sensor` and
afterwards as state changes happen. **Do not render every `ROBOT_STATE`
packet as a screen update** — that is what the status line is for.

### Robot states (`value` of `ROBOT_STATE` / `WELCOME` / `PONG`)

| # | Name | Meaning |
|---|---|---|
| 0 | `BOOT` | starting up |
| 1 | `IDLE` | awake, not doing anything |
| 2 | `THINKING` | waiting on Gemini — **the robot is stopped** |
| 3 | `SPEAKING` | audio playing — **the robot is stopped** |
| 4 | `EXPLORING` | driving itself around |
| 5 | `OBSERVING` | paused while it looks around |
| 6 | `MOVING` | in the middle of a timed maneuver |
| 7 | `DANCING` | dancing |
| 8 | `REMOTE` | a controller owns the wheels |
| 9 | `OFFLINE` | no Wi-Fi, AI features paused |

### Cliff sensor states (`value` of `SENSOR` / `CLIFF`)

This is the table edge detector. Showing it in the app is genuinely
useful — it is the difference between "the robot is stuck" and "the
robot correctly refused to walk off the table".

| # | Name | Meaning |
|---|---|---|
| 0 | `unknown` | sensor not configured, or no reading yet |
| 1 | `ground` | floor found — safe |
| 2 | `warn` | close to the edge — driving slowly |
| 3 | `drop` | **no floor: the robot has stopped** |
| 4 | `fault` | the sensor is not responding — the robot has stopped |

`arg` is the vertical distance from the sensor to the floor in cm. On a
table it sits near the configured nominal value; over an edge it climbs
fast.

### Errors (`value` of `ERROR`)

| # | Name | What the app should show |
|---|---|---|
| 0 | — | no error |
| 1 | `BAD_PACKET` | framing bug — usually a wrong length or a missing magic byte |
| 2 | `UNKNOWN_CMD` | you sent a command id that does not exist |
| 3 | `NOT_CONFIGURED` | the robot's pins for that feature are not set yet |
| 4 | `BUSY` | the radio remote currently owns the wheels |
| 5 | `CLIFF` | **refused: the sensor saw a drop** |
| 6 | `SENSOR_FAULT` | **refused: the cliff sensor is not reporting** |
| 7 | `BAD_ARG` | argument out of range (0 steps, 400 degrees, …) |
| 8 | `LINK_TIMEOUT` | **the robot stopped itself** — you went quiet while driving |

When you get `CLIFF` or `SENSOR_FAULT`, do not retry in a loop. Tell the
user to pick the robot up, or that the sensor needs attention.

`LINK_TIMEOUT` is the one error the robot raises about *you*: you stopped
sending while it was driving, so it stopped and released the wheels. Start
the keepalive again rather than reconnecting.

---

## 5. Talking to WALL-E

The robot has **no microphone**. It answers questions in text and speaks
the answer out loud, but you type the question — the app, or the radio
remote, or the USB serial console.

### `ask` — a question, answered by Gemini and spoken

```
app                                            robot
 |-- COMMAND ask (0x1A) ---------------------->|
 |-- TEXT  op=0x01 "what is the capital of..." ->|
 |                                             |  stop the wheels
 |                                             |  Gemini
 |                                             |  TTS -> amplifier
 |<-- STATE THINKING --------------------------|
 |<-- TEXT  op=0x03 "I think it's Moscow..." <-|
 |<-- STATE SPEAKING --------------------------|
 |<-- STATE IDLE -----------------------------|
```

### `speak` — say exactly this, no Gemini

Same shape, but `op=0x02` and the robot uses TTS only. Useful for
testing the audio path, and for app-driven messages like "battery low".

### `talk` / `joke` — no text frame needed

`0x15` and `0x16` are one frame each: the robot decides *what* to say.
`talk` gives a general line, `joke` gives a joke. Both are still spoken
out loud.

> While the robot is `THINKING` or `SPEAKING` the wheels are blocked for
> **every** controller. A `move_forward` sent during that window is
> refused with `BUSY`. This is intentional: a robot that rolls around
> while it is talking cannot be heard and cannot be stopped.

---

## 6. A complete session

```
app                                    robot
CONNECT ─────────────────────────────▶
HELLO (0x30) ───────────────────────▶
                              ◀───── WELCOME, value = 1 (IDLE)
SENSOR (0x19) ───────────────────▶
                              ◀───── SENSOR, value = 1 (ground), arg = 12
MOVE_STEPS (0x17, arg = 4) ───────▶
                              ◀───── ACK, cmd = 0x17
                              ◀───── ROBOT_STATE, value = 6 (MOVING)
                              ◀───── ROBOT_STATE, value = 1 (IDLE)     <- it finished
MOVE_FORWARD (0x01, HELD) ───────▶        pressed
MOVE_FORWARD (0x01, HELD) ───────▶        every 250 ms
MOVE_FORWARD (0x01, HELD) ───────▶
STOP (0x07) ─────────────────────▶        released
                              ◀───── ACK, cmd = 0x07
                              ◀───── ROBOT_STATE, value = 1
```

---

## 7. Reference implementation (TypeScript)

```ts
// ---- constants mirrored from shared/walle_protocol.h -----------------
const MAGIC = 0xa5, VERSION = 0x01;
const PKT = 10, TEXT_HEADER = 8, TEXT_MAX = 240;

export const TYPE_COMMAND = 0x01, TYPE_STATUS = 0x02, TYPE_TEXT = 0x03;

export const CMD = {
  MOVE_FORWARD: 0x01, MOVE_BACKWARD: 0x02,
  TURN_LEFT: 0x03, TURN_RIGHT: 0x04,
  ROTATE_LEFT: 0x05, ROTATE_RIGHT: 0x06,
  STOP: 0x07,
  DANCE: 0x10, EXPLORE: 0x11, IDLE: 0x12,
  AUTONOMOUS_ON: 0x13, AUTONOMOUS_OFF: 0x14,
  TALK: 0x15, JOKE: 0x16,
  MOVE_STEPS: 0x17, TURN_AROUND: 0x18,
  READ_SENSOR: 0x19,
  ASK: 0x1a, SPEAK: 0x1b,
  TURN_DEGREES: 0x1c,
  EXPR_HAPPY: 0x20, EXPR_THINKING: 0x21, EXPR_SURPRISED: 0x22,
  EXPR_CONFUSED: 0x23, EXPR_IDLE: 0x24,
  HELLO: 0x30, PING: 0x31, BYE: 0x32,
} as const;

export const CLIFF = {
  UNKNOWN: 0, GROUND: 1, WARN: 2, DROP: 3, FAULT: 4,
} as const;

export const ERR = {
  NONE: 0, BAD_PACKET: 1, UNKNOWN_CMD: 2, NOT_CONFIGURED: 3,
  BUSY: 4, CLIFF: 5, SENSOR_FAULT: 6, BAD_ARG: 7, LINK_TIMEOUT: 8,
} as const;

export const ROBOT_STATE = [
  "boot", "idle", "thinking", "speaking", "exploring",
  "observing", "moving", "dancing", "remote", "offline",
] as const;

const FLAG_HELD = 0x01;
const PING_INTERVAL_MS = 300;

// ---- client ---------------------------------------------------------
export interface StatusEvents {
  onStatus?: (cmd: number, value: number, arg: number, seq: number) => void;
  onState?: (state: number, name: string) => void;
  onCliff?: (state: number, groundCm: number) => void;
  onError?: (code: number) => void;
  onText?: (text: string) => void;
}

export class WalleClient {
  private buf = new Uint8Array(TEXT_HEADER + TEXT_MAX + PKT);
  private rxLen = 0;
  private rxWant = PKT;
  private seq = 0;
  private ping?: ReturnType<typeof setInterval>;
  /** Set true while a direction button is held, to auto-ping. */
  driving = false;

  constructor(
    private host: string,
    private port = 8080,
    private ev: StatusEvents = {},
  ) {}

  async connect(): Promise<void> {
    await new Promise<void>((resolve, reject) => {
      const sock = new WebSocket(`ws://${this.host}:${this.port}`);
      // NOTE: a browser WebSocket cannot open a raw TCP socket, so a
      // browser app needs a tiny bridge or a native shell. A phone app,
      // a desktop app, Node or Python can use a raw socket directly.
      (sock as any).addEventListener("open", () => resolve());
      (sock as any).addEventListener("error", reject);
      (sock as any).addEventListener("message", (m: any) =>
        this.feed(new Uint8Array(m.data)));
    });
    this.hello();
    this.ping = setInterval(() => {
      if (this.driving) this.send(CMD.PING);
    }, PING_INTERVAL_MS);
  }

  close() {
    if (this.ping) clearInterval(this.ping);
    this.send(CMD.BYE);
  }

  /** One 10-byte command. */
  send(cmd: number, arg = 0, value = 0, held = false): void {
    const b = new Uint8Array(PKT);
    const v = new DataView(b.buffer);
    v.setUint8(0, MAGIC);      v.setUint8(1, VERSION);
    v.setUint8(2, TYPE_COMMAND); v.setUint8(3, cmd);
    v.setUint8(4, value);      v.setUint8(5, (this.seq = (this.seq + 1) & 0xff));
    v.setUint8(6, held ? FLAG_HELD : 0);
    v.setUint8(7, 0);
    v.setUint16(8, arg, true);   // little endian
    this.raw(b);
  }

  /** A TEXT frame: announce with ask/speak, then send the words. */
  private sendText(op: number, text: string): void {
    const bytes = new TextEncoder().encode(text).slice(0, TEXT_MAX);
    const b = new Uint8Array(TEXT_HEADER + bytes.length);
    const v = new DataView(b.buffer);
    v.setUint8(0, MAGIC);      v.setUint8(1, VERSION);
    v.setUint8(2, TYPE_TEXT); v.setUint8(3, op);
    v.setUint8(4, 0);
    v.setUint16(5, bytes.length, true);
    v.setUint8(7, 0);
    b.set(bytes, TEXT_HEADER);
    this.raw(b);
  }

  ask(question: string)   { this.send(CMD.ASK);   this.sendText(0x01, question); }
  speakExactly(text: string) { this.send(CMD.SPEAK); this.sendText(0x02, text); }

  hello()   { this.send(CMD.HELLO); }
  forward(held: boolean) { this.driving = held; this.send(CMD.MOVE_FORWARD, 0, 0, held); }
  stop()    { this.driving = false; this.send(CMD.STOP); }
  steps(n: number)       { this.send(CMD.MOVE_STEPS, n); }
  turnAround()           { this.send(CMD.TURN_AROUND); }
  turnDegrees(d: number) { this.send(CMD.TURN_DEGREES, d); }
  readSensor()           { this.send(CMD.READ_SENSOR); }

  // ---- inbound ----
  private raw(b: Uint8Array) { /* socket.send(b) */ }

  private feed(chunk: Uint8Array) {
    for (const byte of chunk) {
      if (this.rxLen === 0) {
        if (byte !== MAGIC) continue;         // resync on the magic byte
        this.buf[this.rxLen++] = byte;
        this.rxWant = PKT;
        continue;
      }
      this.buf[this.rxLen++] = byte;

      if (this.rxWant === PKT) {
        if (this.rxLen >= PKT) { this.onPacket(); this.rxLen = 0; }
        continue;
      }
      if (this.rxLen === TEXT_HEADER) {
        const v = new DataView(this.buf.buffer);
        this.rxWant = TEXT_HEADER + v.getUint16(5, true);
      }
      if (this.rxLen >= this.rxWant) { this.onText(); this.rxLen = 0; }
    }
  }

  private onPacket() {
    const v = new DataView(this.buf.buffer);
    if (v.getUint8(0) !== MAGIC || v.getUint8(1) !== VERSION) return;
    const type = v.getUint8(2), cmd = v.getUint8(3);
    const value = v.getUint8(4), seq = v.getUint8(5);
    const arg = v.getUint16(8, true);
    if (type !== TYPE_STATUS) return;

    this.ev.onStatus?.(cmd, value, arg, seq);

    switch (cmd) {
      case 0x80: case 0x83: case 0x86:            // WELCOME / STATE / PONG
        this.ev.onState?.(value, ROBOT_STATE[value] ?? "?"); break;
      case 0x82: this.ev.onError?.(value); break;
      case 0x87: case 0x88:                       // SENSOR / CLIFF
        this.ev.onCliff?.(value, arg); break;
    }
  }

  private onText() {
    const op = this.buf[3];
    const len = this.rxWant - TEXT_HEADER;
    if (op !== 0x03) return;                     // only replies matter
    this.ev.onText?.(new TextDecoder().decode(this.buf.slice(TEXT_HEADER, TEXT_HEADER + len)));
  }
}
```

### Python smoke test (no app required)

This proves the robot is reachable and the framing is right, before you
write a single line of app code.

```python
import socket, struct, time

HOST, PORT = "192.168.1.42", 8080   # <- the robot's IP from its serial log

def packet(cmd, arg=0, value=0, flags=0, seq=0):
    # magic, version, type, cmd, value, seq, flags, reserved, arg(u16 LE)
    return struct.pack("<BBBBBBBB H", 0xA5, 0x01, 0x01, cmd, value, seq, flags, 0, arg)

s = socket.create_connection((HOST, PORT), timeout=5)
s.sendall(packet(0x30))                      # HELLO
time.sleep(0.3)
s.sendall(packet(0x19))                      # READ_SENSOR
time.sleep(0.5)

s.settimeout(2)
try:
    while True:
        data = s.recv(512)
        if not data:
            break
        print(data.hex(" "))
except socket.timeout:
    pass
```

Expected in the hex dump: a `WELCOME` (`a5 01 02 80 ...`), then
`SENSOR` (`a5 01 02 87 <value> ... <arg LE>`).

### Driving the robot from Python

```python
import time
s.sendall(packet(0x17, arg=4))               # 4 steps, stops by itself
time.sleep(1.5)
s.sendall(packet(0x07))                      # stop (always safe to send)
```

---

## 8. What the app cannot do (yet)

Being explicit about this saves a lot of confusion:

* **The robot cannot listen.** There is no microphone and no
  speech-to-text on the robot. Speech input lives in the radio remote or
  the app itself, never in the firmware.
* **No distance reporting on every change.** The robot pushes `SENSOR`
  on `read_sensor`, on cliff transitions and about once a second. If you
  want a smooth live graph, poll with `read_sensor` at ~5 Hz — that is
  a real ultrasonic echo per poll, so do not poll faster.
* **No battery.** `WALLE_ST_BATTERY` exists in the protocol but no
  voltage sensor feeds it, so it is never sent. Do not build a battery
  gauge against it.
* **No camera stream.** The camera is compiled out on a plain ESP32-S3.
* **No audio to the app.** The robot streams TTS straight to its own
  amplifier; it does not send audio back.

---

## 9. Prompt for the CLI building the app

> Copy everything below this line as your instructions.

---

You are implementing the **companion app for WALL-E**, a small ESP32-S3
robot. The robot's firmware is **already written and compiled**. Your job
is **only** the app side. You must not invent protocol details: the
contract is in `robot_s3/docs/APP_INTEGRATION.md`, read it first and
follow it exactly.

**1. Transport.** A raw TCP socket to the robot's IP on port `8080`. Not
HTTP, not WebSocket, not JSON. If your target platform is a browser,
say so up front and stop — a browser cannot open a raw TCP socket and
you will need a bridge. For Node, Python, React Native, Swift, Kotlin or
a desktop app, use a plain socket.

**2. Wire format.** Binary, little-endian, 10-byte command packets.
Implement `WallePacket` exactly as in the reference TypeScript above.
Use `DataView`; never pack a language struct, because padding differs.

**3. Implement these, in this priority order:**
   a. Connect, send `HELLO`, render the robot state.
   b. Direction buttons. On press send the command with `HELD`; on
      release send `STOP`. **Re-send the held command every 250 ms** or
      the robot stops after 700 ms of silence.
   c. Stop button, always available, never disabled.
   d. Mode buttons: dance, explore, autonomous on/off, expressions.
   e. `read_sensor` and a small live readout of the cliff state and
      ground distance, plus a clear warning when the state is `DROP` or
      `FAULT`.
   f. A text box that sends `ask` + a TEXT frame, and displays the
      `WALLE_OP_REPLY` text frame.
   g. The timed motions: "4 steps" and "turn around". Note that these
      finish on their own — do not add a keepalive loop for them, and do
      not expect a `stop` to be needed.

**4. Error handling is not optional.** The robot answers every command
with `ACK` or `ERROR`, and it refuses commands when it cannot or must
not comply. Map each error to a message a person can act on:
   * `CLIFF` → "Wall-E stopped at the edge — pick it up or move it back"
   * `SENSOR_FAULT` → "Wall-E's distance sensor is not responding"
   * `BUSY` → "The handheld remote has control" (or "WALL-E is talking")
   * `NOT_CONFIGURED` → "That feature isn't wired up on this robot"
   * `BAD_ARG` → a bug in your call; clamp before sending.
   Never retry a refused movement command in a loop.

**5. Do not paper over the safety rules.** The robot will refuse to move
while it is thinking or speaking, and while the cliff sensor is unhappy.
That is intended. Surface it in the UI instead of fighting it.

**6. Deliverables.** A typed client module matching the reference
implementation, a small UI exercising points 3a–3g, and a README saying
how to find the robot's IP address (it is printed on the robot's serial
log at boot) and how to run it.