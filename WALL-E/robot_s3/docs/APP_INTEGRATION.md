# WALL-E App Integration

**The contract between the WALL-E robot firmware and the companion app.**

This is written from the firmware's side and matches what the shipped app
(`innovationday.walle.app`) already sends. If either side changes, change
both here and in `shared/walle_protocol.h`.

The single most important thing to understand:

> **The app is the only controller.** The ESP32-WROOM handheld remote has
> been removed, so there is exactly one way in and exactly one set of rules.
> The app speaks the same 10-byte binary packets, over TCP, into the same
> `command_dispatch.cpp` the serial console uses.

---

## 1. Connecting

| | |
|---|---|
| Transport | raw **TCP** — not HTTP, not WebSocket, not JSON |
| Port | **8080** (`APP_TCP_PORT`) |
| Addressing | the robot's IP, printed on its serial log at boot |
| Handshake | none |
| Secret | none |

### How the app opens the socket

A browser cannot open a raw TCP socket. The app is a Capacitor app, so it
uses a small native plugin for exactly that:

```
React UI  →  WallETcpPlugin (Java)  →  TCP socket  →  robot
              connect / send / close      base64 bridge
```

The plugin is deliberately dumb: it opens a socket and moves bytes. All
framing, the watchdog and reconnect logic live in TypeScript on the JS
side, because that is the only place there is one copy of it.

### No discovery

The firmware contains no mDNS and does not advertise itself, so
`wall-e.local` will not resolve. The app asks for the IP once and
remembers it. `/api/scan` is a placeholder that echoes the configured
host, not a working scanner.

---

## 2. The 700 ms watchdog — the thing that will bite you

While the app is **driving**, it must send **something at least every 700 ms**
(`APP_TIMEOUT_MS`) or the robot stops and releases the wheels.

This is deliberate. A phone whose screen locks, or an app whose tab is
backgrounded, must never leave the robot driving.

| Situation | What the app sends |
|---|---|
| Direction held | re-sends the held command every **250 ms** with `FLAG_HELD` |
| Connected, not driving | `PING` every 300 ms |
| Finger lifts | `STOP` immediately, then disarms the keepalive |

The app also holds a **screen wake lock** while visible, because Android
locks an idle screen after a few seconds — and a locked screen means a
silent app means a stopped robot mid-manoeuvre.

When the robot stops you for a timeout it sends `ERROR` / `LINK_TIMEOUT`.
Treat that as a *notice*, not a disconnect: the link is still up, it just
needs the keepalive running again.

---

## 3. Frames

### 3.1 Fixed packet — 10 bytes, little endian

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | magic | `0xA5` always. Used to resynchronise. |
| 1 | 1 | version | `0x01` |
| 2 | 1 | type | `0x01` COMMAND, `0x02` STATUS, `0x03` TEXT |
| 3 | 1 | cmd | command or status id |
| 4 | 1 | value | small argument: state, error, expression |
| 5 | 1 | seq | rolling counter 0–255, wraps |
| 6 | 1 | flags | bit 0 = `WALLE_FLAG_HELD` (0x01) |
| 7 | 1 | reserved | 0 |
| 8 | 2 | arg | `uint16` LE — step count, degrees, millivolts |

Encode with `DataView`, never a packed struct: C struct padding rules differ
between platforms and the firmware asserts the type is exactly 10 bytes.

### 3.2 Text frame — 8-byte header + UTF-8 payload

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | version `0x01` |
| 2 | 1 | type `0x03` TEXT |
| 3 | 1 | op — `0x01` ask, `0x02` speak, `0x03` reply, `0x04` persona |
| 4 | 1 | flags |
| 5 | 2 | len `uint16` LE, 1–240 |
| 6–7 | | padding to 8 |
| 8… | len | UTF-8, no terminator, no escaping |

`ASK`, `SPEAK` and `SET_PERSONA` are announced as a fixed packet, then the
words follow **immediately** in a text frame.

Truncate at **240 bytes on a character boundary**, not a byte boundary:
slicing encoded bytes at 240 can cut a 3-byte character in half and hand the
robot invalid UTF-8.

### 3.3 Stream framing — read the type byte, not the length

TCP has no message boundaries: one read can return half a packet or three
packets. The naive approach — "a frame is fixed-size unless it looks like a
text header at offset 8" — **cannot work**, because a text frame whose total
length happens to be 10 bytes (an 8-byte header plus 2 bytes of text) is
indistinguishable from a fixed packet by length alone.

Read the **type byte at offset 2** as soon as three bytes have arrived; it is
unambiguous. Also drop a frame whose version byte does not follow the magic,
so a partial frame from a previous connection is never decoded as garbage.

---

## 4. Commands

### Locomotion (held)

| Id | Name | Behaviour |
|---|---|---|
| `0x01` | `move_forward` | drives while held; re-send or it stops |
| `0x02` | `move_backward` | as above |
| `0x03` | `turn_left` | arc left, held |
| `0x04` | `turn_right` | arc right, held |
| `0x05` | `rotate_left` | pivot on the spot, held |
| `0x06` | `rotate_right` | pivot on the spot, held |
| `0x07` | `stop` | **highest priority from any source.** Never `HELD`. |

### Timed motions (finish on their own)

| Id | Name | `arg` |
|---|---|---|
| `0x17` | `move_steps` | 1–50 steps (`STEP_DISTANCE_CM` = 10 cm) |
| `0x18` | `turn_around` | — (180°) |
| `0x1c` | `turn_degrees` | 1–360° |
| `0x1d` | `set_persona` | then a text frame with op `0x04` — see §5 |

No keepalive, and **no `stop` afterwards** — sending one is harmless but
pointless. Still subject to the cliff sensor: a drop truncates the motion.

### Modes

| Id | Name |
|---|---|
| `0x10` | `dance` |
| `0x11` | `explore` |
| `0x12` | `idle` |
| `0x13` | `autonomous_on` |
| `0x14` | `autonomous_off` |

### Voice

| Id | Name | Notes |
|---|---|---|
| `0x15` | `talk` | the robot decides what to say, then says it |
| `0x16` | `joke` | the robot decides the joke |
| `0x1a` | `ask` | then a text frame with op `0x01`, the question |
| `0x1b` | `speak` | then a text frame with op `0x02`, said verbatim |

### Expressions

| Id | Name |
|---|---|
| `0x20` | `expression_happy` |
| `0x21` | `expression_thinking` |
| `0x22` | `expression_surprised` |
| `0x23` | `expression_confused` |
| `0x24` | `expression_idle` |

### Sensors and housekeeping

| Id | Name |
|---|---|
| `0x19` | `read_sensor` |
| `0x30` | `hello` |
| `0x31` | `ping` |
| `0x32` | `bye` |

---

## 5. Talking to the robot

### The whole voice pipeline

```
 phone microphone
        │  Web Speech API (in the app, on the phone)
        ▼
   speech → text
        │  COMMAND ask (0x1a)
        │  TEXT    op=0x01 "what do you see?"      ──TCP──▶ robot
        │                                                │ wheels stop
        │                                                │ Gemini (with the persona's prompt)
        │                                                │ TTS → I²S → amplifier
        │◀── TEXT op=0x03 "I see a table, friend! Friend!" ─┘
        ▼
   transcript in the app        sound from the ROBOT's own speaker
```

**The robot has no microphone and does no speech recognition.** That is a
deliberate division of labour:

| | In the app | In the firmware |
|---|---|---|
| Speech recognition | ✔ browser API, free, on a CPU with a proper OS | ✘ would need an I²S capture buffer competing with the TTS ring for RAM, plus a recogniser |
| Microphone permission | the OS prompt the user understands | — |
| Language selection | a picker | — |
| AI + TTS | ✘ needs the Gemini key on the phone | ✔ key stays in one place |

The robot only ever handles **text**. It answers in text *and* speaks, and
the answer comes back as a `TEXT` frame with `op = 0x03` for the app's
transcript.

### `ask` — a question, answered and spoken

```
app                                              robot
 |-- COMMAND ask (0x1a) -------------------------->|
 |-- TEXT  op=0x01 "what is the capital of..." ->|
 |                                             |  stop the wheels
 |                                             |  Gemini
 |                                             |  TTS → amplifier
 |<-- STATE THINKING --------------------------|
 |<-- TEXT  op=0x03 "I think it's Moscow..." <-|
 |<-- STATE SPEAKING --------------------------|
 |<-- STATE IDLE -----------------------------|
```

### `speak` — say exactly this, no Gemini

Same shape with `op=0x02`. Useful for app-driven messages.

### `talk` / `joke` — no text frame needed

`0x15` and `0x16` are one frame each: the robot decides *what* to say.

> While the robot is `THINKING` or `SPEAKING` the wheels are blocked for
> **every** source. A `move_forward` sent in that window is refused with
> `BUSY`. This is intentional: a robot that rolls around while it is talking
> cannot be heard and cannot be stopped by the person listening to it.

### The persona — `set_persona` + text op `0x04`

**The app decides who the robot is.** On every connect it sends the persona
before the first question, so a demo can change the robot's personality
without reflashing anything.

```
app                                              robot
 |-- COMMAND set_persona (0x1d) ----------------->|
 |-- TEXT   op=0x04  {"n":"Vulkan",...} --------->|  parses
 |<-- ACK   cmd = 0x1d ----------------------------|  or ERROR / BAD_ARG
```

The payload is compact JSON with single-letter keys, because a text frame is
**240 bytes** and that is the whole budget:

```json
{"n":"Vulkan","s":"Friend!","m":"happy","p":"You are Vulkan, ..."}
```

| Key | Buffer | Required | Meaning |
|---|---|---|---|
| `n` | 24 | **yes** | the robot's name |
| `s` | 24 | no (may be `""`) | suffix appended to every reply before it is spoken |
| `m` | 16 | no (may be `""`) | mood, informational |
| `p` | 216 | **yes** | the Gemini **system instruction** |

Rules the firmware follows:

1. `p` **replaces** the compiled-in system instruction, so every answer is
   generated in that voice.
2. `s` is appended to the answer — but **only if Gemini did not already end
   with it** (case-insensitively). The prompt tells Gemini to add it, so it
   usually does, and the firmware does not want "Friend! Friend!".
3. **All or nothing.** A missing key, or a field longer than its buffer, is
   refused with `BAD_ARG` and the *previous* persona is left completely
   untouched. Partially applying one would give a robot whose name and whose
   voice disagree — impossible to debug from outside.
4. `set_persona` is the **only** command acknowledged *after* its text frame,
   because the fixed packet carries no payload and the ack has to wait until
   the words have been parsed. Do not treat an ack for `0x1d` as proof unless
   it arrives after the frame.

**The size budget is tight and worth respecting.** `{"n":"","s":"","m":"","p":""}`
costs 27 bytes, so the longest prompt the protocol can carry at all is
`240 − 27 − 1 = 212` bytes. The app's current prompt is 156, leaving 51 bytes
of slack. Spelling the keys out (`{"name":"Vulkan",...}`) costs about 20
bytes — on a payload this size that is the difference between a usable
prompt and no persona at all.

If the app never sends a persona, the firmware falls back to the compiled-in
default (`PERSONA_DEFAULT_NAME` and `GEMINI_SYSTEM_PROMPT` in
`robot_s3/include/config.h`), so the robot is never voiceless.

---

## 6. A complete session

```
app                                    robot
CONNECT ─────────────────────────────▶
                              ◀───── WELCOME, value = 1 (IDLE)
                              ◀───── REMOTE_STATE 0 (there is no radio remote)
                              ◀───── SENSOR, ground, e.g. 12 cm
HELLO (0x30) ───────────────────────▶
                              ◀───── ACK, cmd = 0x30
SET_PERSONA (0x1d) ──────────────────▶
TEXT op=0x04 {"n":"Vulkan",...} ────▶
                              ◀───── ACK, cmd = 0x1d        (or ERROR BAD_ARG)
READ_SENSOR (0x19) ─────────────────▶
                              ◀───── SENSOR, value = 1, arg = 12
MOVE_STEPS (0x17, arg = 4) ───────▶
                              ◀───── ACK, cmd = 0x17
                              ◀───── ROBOT_STATE 6 (MOVING)
                              ◀───── ROBOT_STATE 1 (IDLE)     ← it finished
MOVE_FORWARD (0x01, HELD) ───────▶        pressed
MOVE_FORWARD (0x01, HELD) ───────▶        every 250 ms
STOP (0x07) ─────────────────────▶        released
                              ◀───── ACK, cmd = 0x07
                              ◀───── ROBOT_STATE 1
```

---

## 7. Reference implementation (TypeScript)

```ts
const MAGIC = 0xa5, VERSION = 0x01;
const PKT = 10, TEXT_HEADER = 8, TEXT_MAX = 240;

export const TYPE = { COMMAND: 1, STATUS: 2, TEXT: 3 } as const;

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
  ASK: 0x1a, SPEAK: 0x1b, TURN_DEGREES: 0x1c,
  SET_PERSONA: 0x1d,
  EXPR_HAPPY: 0x20, EXPR_THINKING: 0x21, EXPR_SURPRISED: 0x22,
  EXPR_CONFUSED: 0x23, EXPR_IDLE: 0x24,
  HELLO: 0x30, PING: 0x31, BYE: 0x32,
} as const;

export const ST = {
  WELCOME: 0x80, ACK: 0x81, ERROR: 0x82, ROBOT_STATE: 0x83,
  REMOTE_STATE: 0x84, BATTERY: 0x85, PONG: 0x86,
  SENSOR: 0x87, CLIFF: 0x88,
} as const;

export const ERR = {
  NONE: 0, BAD_PACKET: 1, UNKNOWN_CMD: 2, NOT_CONFIGURED: 3,
  BUSY: 4, CLIFF: 5, SENSOR_FAULT: 6, BAD_ARG: 7, LINK_TIMEOUT: 8,
} as const;

export const CLIFF = {
  UNKNOWN: 0, GROUND: 1, WARN: 2, DROP: 3, FAULT: 4,
} as const;

export const ROBOT_STATE = [
  "boot", "idle", "thinking", "speaking", "exploring",
  "observing", "moving", "dancing", "manual", "offline",
] as const;                    // note: 8 is "manual", not "remote"

const FLAG_HELD = 0x01;
const PING_MS = 300;
const HELD_REPEAT_MS = 250;

export class WalleClient {
  private buf = new Uint8Array(TEXT_HEADER + TEXT_MAX);
  private rxLen = 0;
  private rxWant = PKT;
  private expectingText = false;
  private seq = 0;
  private keepalive?: ReturnType<typeof setInterval>;
  private held: number | null = null;

  constructor(
    private io: { write(b: Uint8Array): void },   // WallETcp plugin or a socket
    private ev: {
      onStatus?(cmd: number, value: number, arg: number): void;
      onState?(state: number, name: string): void;
      onCliff?(state: number, groundCm: number): void;
      onError?(code: number): void;
      onText?(kind: "ask" | "speak" | "reply", text: string): void;
    } = {},
  ) {}

  // ---- opening ----
  open(): void {
    this.hello();
    this.setPersona({ n: "Vulkan", s: "Friend!", m: "happy",
                      p: "You are Vulkan, ... End every reply with Friend!." });
    this.readSensor();
    this.keepalive = setInterval(() => {
      // Ping only when NOT driving: a held command is its own keepalive.
      if (this.held === null) this.ping();
    }, PING_MS);
  }

  close(): void {
    if (this.keepalive) clearInterval(this.keepalive);
    this.bye();
  }

  // ---- outbound ----
  send(cmd: number, arg = 0, held = false, value = 0): void {
    const b = new Uint8Array(PKT);
    const v = new DataView(b.buffer);
    v.setUint8(0, MAGIC);        v.setUint8(1, VERSION);
    v.setUint8(2, TYPE.COMMAND); v.setUint8(3, cmd);
    v.setUint8(4, value);        v.setUint8(5, (this.seq = (this.seq + 1) & 0xff));
    v.setUint8(6, held ? FLAG_HELD : 0);
    v.setUint8(7, 0);
    v.setUint16(8, arg, true);   // little endian
    this.io.write(b);
  }

  private sendText(op: number, text: string): void {
    // Truncate on a CHARACTER boundary, never a byte boundary.
    const enc = new TextEncoder();
    let bytes = enc.encode(text);
    if (bytes.length > TEXT_MAX) {
      let s = text;
      while (enc.encode(s).length > TEXT_MAX) s = s.slice(0, -1);
      bytes = enc.encode(s);
    }
    const b = new Uint8Array(TEXT_HEADER + bytes.length);
    const v = new DataView(b.buffer);
    v.setUint8(0, MAGIC);       v.setUint8(1, VERSION);
    v.setUint8(2, TYPE.TEXT);   v.setUint8(3, op);
    v.setUint8(4, 0);
    v.setUint16(5, bytes.length, true);
    b.set(bytes, TEXT_HEADER);
    this.io.write(b);
  }

  hello()        { this.send(CMD.HELLO); }
  ping()         { this.send(CMD.PING); }
  bye()          { this.send(CMD.BYE); }
  readSensor()   { this.send(CMD.READ_SENSOR); }
  moveSteps(n: number)       { this.send(CMD.MOVE_STEPS, n); }
  turnDegrees(d: number)     { this.send(CMD.TURN_DEGREES, d); }
  turnAround()              { this.send(CMD.TURN_AROUND); }
  stop() { this.held = null; this.send(CMD.STOP); }

  drive(cmd: number, down: boolean): void {
    if (down) {
      this.held = cmd;
      this.send(cmd, 0, true);
      this.heldTimer = setInterval(() => this.send(cmd, 0, true), HELD_REPEAT_MS);
    } else {
      if (this.heldTimer) clearInterval(this.heldTimer);
      this.heldTimer = undefined;
      this.held = null;
      this.send(CMD.STOP);
    }
  }
  private heldTimer?: ReturnType<typeof setInterval>;

  ask(text: string) {
    this.send(CMD.ASK);
    this.sendText(0x01, text);
  }
  speakExactly(text: string) {
    this.send(CMD.SPEAK);
    this.sendText(0x02, text);
  }
  setPersona(p: { n: string; s?: string; m?: string; p: string }) {
    const payload = JSON.stringify({
      n: p.n, s: p.s ?? "", m: p.m ?? "", p: p.p,
    });                      // short keys: the budget is 240 bytes TOTAL
    this.send(CMD.SET_PERSONA);
    this.sendText(0x04, payload);
  }

  // ---- inbound ----
  feed(chunk: Uint8Array): void {
    for (const byte of chunk) {
      if (this.rxLen === 0) {
        if (byte !== MAGIC) continue;                 // resync
        this.buf[this.rxLen++] = byte;
        this.rxWant = PKT;
        continue;
      }
      if (this.rxLen === 1 && byte !== VERSION) { this.reset(); continue; }
      if (this.rxLen >= this.buf.length) { this.reset(); continue; }

      this.buf[this.rxLen++] = byte;

      // The TYPE byte decides fixed vs text. Never the length.
      if (this.rxLen === 3) {
        this.expectingText = this.buf[2] === TYPE.TEXT;
        this.rxWant = this.expectingText ? TEXT_HEADER : PKT;
      }
      if (this.expectingText && this.rxLen === TEXT_HEADER) {
        const len = new DataView(this.buf.buffer).getUint16(5, true);
        if (len === 0 || len > TEXT_MAX) { this.reset(); continue; }
        this.rxWant = TEXT_HEADER + len;
      }
      if (this.rxLen < this.rxWant) continue;

      if (this.expectingText) this.onText(); else this.onPacket();
      this.reset();
    }
  }

  private reset(): void {
    this.rxLen = 0;
    this.rxWant = PKT;
    this.expectingText = false;
  }

  private onPacket(): void {
    const v = new DataView(this.buf.buffer);
    if (v.getUint8(0) !== MAGIC || v.getUint8(1) !== VERSION) return;
    if (v.getUint8(2) !== TYPE.STATUS) return;

    const cmd = v.getUint8(3), value = v.getUint8(4);
    const arg = v.getUint16(8, true);
    this.ev.onStatus?.(cmd, value, arg);

    switch (cmd) {
      case ST.WELCOME:
      case ST.ROBOT_STATE:
      case ST.PONG:
        this.ev.onState?.(value, ROBOT_STATE[value] ?? "?"); break;
      case ST.ERROR:
        this.ev.onError?.(value); break;
      case ST.SENSOR:
      case ST.CLIFF:
        this.ev.onCliff?.(value, arg); break;
    }
  }

  private onText(): void {
    const op = this.buf[3];
    const text = new TextDecoder().decode(
      this.buf.slice(TEXT_HEADER, this.rxWant));
    const kind = op === 0x03 ? "reply" : op === 0x02 ? "speak" : "ask";
    this.ev.onText?.(kind, text);
  }
}
```

### Speech recognition in the app

The app uses the Web Speech API, which works inside the Android WebView:

```ts
const SR = globalThis.SpeechRecognition ?? (globalThis as any).webkitSpeechRecognition;

async function listen(timeoutMs = 7000): Promise<string | null> {
  const SRc = SR;
  if (!SRc || !navigator.mediaDevices?.getUserMedia) return null;   // no mic

  return new Promise((resolve) => {
    const rec = new SRc();
    rec.continuous = false;
    rec.interimResults = true;      // show the user it is hearing them
    rec.lang = "en-US";
    rec.maxAlternatives = 1;

    let settled = false;
    const done = (v: string | null) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      resolve(v);
    };

    rec.onresult = (e) => {
      for (let i = e.resultIndex; i < e.results.length; i++) {
        const r = e.results[i];
        if (r.isFinal) return done((r[0] as SpeechRecognitionResult).transcript.trim());
      }
    };
    rec.onerror = () => done(null);
    rec.onend   = () => done(null);

    const timer = setTimeout(() => { try { rec.stop(); } catch {} done(null); },
                            timeoutMs);
    rec.start();
  });
}

// …then send it. The robot does the thinking.
const text = await listen();
if (text) client.ask(text);
```

Three details worth keeping:

* **Always send a timeout.** A recognition session that never fires leaves
  the button stuck "listening" for ever.
* **Always send a `stop()`.** A cancelled session can end without a result.
* **Handle `not-allowed`.** That is the user denying microphone permission,
  and it needs different wording from "I didn't hear anything".

### Python smoke test

Proves the robot is reachable and the framing is right, before you touch the
app.

```python
import socket, struct, time

HOST, PORT = "192.168.1.42", 8080     # the robot's IP from its serial log

def packet(cmd, arg=0, value=0, flags=0, seq=0):
    # magic, version, type, cmd, value, seq, flags, reserved, arg(u16 LE)
    return struct.pack("<BBBBBBBB H", 0xA5, 0x01, 0x01, cmd, value, seq, flags, 0, arg)

def text(op, payload: str):
    b = payload.encode()
    return struct.pack("<BBBBBBH B", 0xA5, 0x01, 0x03, op, 0, len(b), 0, 0) + b

s = socket.create_connection((HOST, PORT), timeout=5)
s.sendall(packet(0x30))                                  # HELLO
time.sleep(0.3)
s.sendall(packet(0x1d) + text(0x04,                      # SET_PERSONA
    '{"n":"Pinocchio","s":"Buon giorno!","m":"happy",'
    '"p":"You are Pinocchio, a cheerful little robot."}'))
time.sleep(0.3)
s.sendall(packet(0x19))                                  # READ_SENSOR
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

Expect a `WELCOME` (`a5 01 02 80 …`), an `ACK` for `0x1d` (`a5 01 02 81 1d …`),
then a `SENSOR` (`a5 01 02 87 <value> … <arg LE>`).

### Driving it from Python

```python
s.sendall(packet(0x17, arg=4))     # 4 steps, stops by itself
time.sleep(1.5)
s.sendall(packet(0x07))            # stop (always safe to send)
```

---

## 8. Status (robot → app)

| Id | Name | Carries |
|---|---|---|
| `0x80` | `WELCOME` | `value` = robot state, on connect and after `hello` |
| `0x81` | `ACK` | `cmd` = the command that ran |
| `0x82` | `ERROR` | `value` = error code |
| `0x83` | `ROBOT_STATE` | `value` = robot state |
| `0x84` | `REMOTE_STATE` | always `0` — the radio remote has been removed |
| `0x85` | `BATTERY` | never sent; no sensor feeds it |
| `0x86` | `PONG` | `value` = robot state |
| `0x87` | `SENSOR` | `value` = cliff state, `arg` = ground distance in **cm** |
| `0x88` | `CLIFF` | `value` = cliff state, on the transition |

WALL-E's spoken answer is **not** a status packet — it arrives as a `TEXT`
frame with `op = 0x03`.

### Robot states

| # | Name | |
|---|---|---|
| 0 | `boot` | starting up |
| 1 | `idle` | awake |
| 2 | `thinking` | **wheels blocked** |
| 3 | `speaking` | **wheels blocked** |
| 4 | `exploring` | driving itself |
| 5 | `observing` | paused, looking around |
| 6 | `moving` | mid-manoeuvre |
| 7 | `dancing` | |
| 8 | `manual` | a controller owns the wheels |
| 9 | `offline` | no Wi-Fi, AI paused |

`8` used to mean `remote`. It now means "the app is driving" — the value did
not change, because it is on the wire.

### Cliff states

| # | Name | |
|---|---|---|
| 0 | `unknown` | not configured, or no reading yet |
| 1 | `ground` | safe |
| 2 | `warn` | near the edge, driving slowly |
| 3 | `drop` | **no floor — stopped** |
| 4 | `fault` | **sensor not responding — stopped** |

`arg` is the vertical distance to the floor in cm. This is the difference
between "the robot is stuck" and "the robot correctly refused to walk off the
table", so show it prominently.

### Errors

| # | Name | What the app should show |
|---|---|---|
| 1 | `BAD_PACKET` | Bad packet from the robot — check the connection |
| 2 | `UNKNOWN_CMD` | WALL-E did not recognise that command |
| 3 | `NOT_CONFIGURED` | That feature is not wired up on this robot |
| 4 | `BUSY` | The wheels are blocked or someone else has control |
| 5 | `CLIFF` | WALL-E stopped at the edge — pick it up or move it back |
| 6 | `SENSOR_FAULT` | WALL-E's distance sensor is not responding |
| 7 | `BAD_ARG` | That value was out of range (or a persona was malformed) |
| 8 | `LINK_TIMEOUT` | WALL-E stopped driving because the app went quiet |

`CLIFF` and `SENSOR_FAULT` are **not retried** — they are stop conditions.

---

## 9. Priority and safety

Above the dispatcher's priority table, **safety wins**: every motion command
is offered to the safety guard first. A cliff, a dead sensor, or a
conversation in progress refuses the command, whatever asked for it.

- `STOP`, `BYE` and `IDLE` from any source always run.
- Exactly one source owns the wheels at a time.
- While `thinking` or `speaking`, movement is refused with `BUSY`.

Surface all of this rather than fighting it: grey out the joystick and say
why.

---

## 10. What the app cannot do, and why

| Missing | Reason |
|---|---|
| Audio playback | the robot streams TTS to its own amplifier; it sends no audio back |
| Camera view | the camera is compiled out on the ESP32-S3 |
| Battery gauge | `BATTERY` exists in the protocol but no sensor feeds it. Hide the row until a real reading arrives. |
| Discovery | no mDNS in the firmware; ask for the IP |
| Real distances | no wheel encoders. "4 steps" is a timed estimate from `STEP_DISTANCE_CM`, not a measurement. |
| Robot-side listening | the robot has no microphone, by design — see §5 |

---

## 11. Prompt for a CLI modifying the app

> Copy everything below this line as your instructions.

You are modifying the **companion app for WALL-E**, a small ESP32-S3 robot.
The robot's firmware is **already written and compiled**; your job is the app
side only. The contract is in `robot_s3/docs/APP_INTEGRATION.md` — read it
first and follow it exactly. Do not invent protocol details.

**Transport.** A raw TCP socket to the robot's IP on port `8080`. Not HTTP,
not WebSocket, not JSON. On Android use the existing `WallETcp` Capacitor
plugin, which sends and receives base64 byte chunks. Keep all framing in
TypeScript, not in the plugin, so there is exactly one copy of it.

**Framing.** Binary, little endian. Use `DataView`. Determine frame type from
the **type byte at offset 2**, never from the length — a 10-byte text frame is
indistinguishable from a fixed packet by size alone.

**Keep these exact behaviours.** They exist for a safety reason, not a
stylistic one:

1. Re-send a held direction every **250 ms** with `FLAG_HELD`; send `STOP` on
   release. The robot stops itself after 700 ms of silence.
2. Send `PING` every 300 ms when connected but idle.
3. Hold a **screen wake lock** while visible. A locked screen means a silent
   app means a stopped robot.
4. On `ERROR` / `LINK_TIMEOUT`, treat it as a notice — the link is up, just
   restart the keepalive. Do not reconnect.
5. Never retry `CLIFF` or `SENSOR_FAULT`. They are stop conditions.
6. Truncate text at 240 bytes **on a character boundary**.
7. Build the persona payload with **single-letter keys**. The budget is 240
   bytes total and the current prompt is already 156.
8. Send the persona on **every** connect, before `hello` completes.

**Speech recognition.** Keep using the Web Speech API. Always set a timeout,
always `stop()` the session, and distinguish "permission denied" from
"didn't hear anything". Send the transcript with `ask` + text op `0x01`. Do
**not** add a text-to-speech of your own — the robot's own speaker is the
thing a live demo is meant to show.

**Deliverables.** Whatever you change, state plainly: (a) which commands you
use, (b) how the 700 ms watchdog is satisfied, (c) how you surface each error
code, and (d) what you did **not** implement.