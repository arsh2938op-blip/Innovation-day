// ============================================================
//  WALL-E shared control protocol
//  ------------------------------------------------------------
//  ONE source of truth for the command/status vocabulary shared by
//  the companion app and the ESP32-S3 robot.
//
//  This header is deliberately LOGIC-FREE: constants and a fixed
//  size POD packet only.
//
//  The ESP32-WROOM radio remote has been REMOVED. The app is now
//  the only controller, which means:
//
//    * the header no longer has to stay byte-compatible with a
//      second firmware, so it can change whenever the app and the
//      robot change together;
//    * ESP-NOW is gone from the robot: one radio, one controller,
//      one set of rules;
//    * everything the remote used to carry (buttons) and everything
//      it could never carry (a keyboard, a microphone, speech
//      recognition) now lives in the app.
//
//  Wire format: 10 bytes, little endian, no padding.
//  Transport:   raw TCP on APP_TCP_PORT - see robot_s3/src/app_link.cpp.
// ============================================================
#pragma once

#include <stdint.h>
#include <stddef.h>   // offsetof, for the static_asserts

#define WALLE_PROTO_MAGIC        0xA5
#define WALLE_PROTO_VERSION      0x01
#define WALLE_PROTO_PACKET_SIZE  10

// ------------------------------------------------------------
//  Message types
// ------------------------------------------------------------
enum WalleMsgType : uint8_t {
    WALLE_MSG_COMMAND = 0x01,   // controller -> robot
    WALLE_MSG_STATUS  = 0x02,   // robot -> controller
    WALLE_MSG_TEXT    = 0x03,   // variable length, see WALLE_PROTO_TEXT_HEADER
};

// ------------------------------------------------------------
//  Commands (controller -> robot).
//
//  The robot owns ALL behaviour. A controller only ever names an
//  intent; it never sends motor speeds, durations or expressions
//  as raw values. That is what lets a second remote, the phone
//  app or a web page reuse this exact list unchanged.
// ------------------------------------------------------------
enum WalleCommand : uint8_t {
    WALLE_CMD_NONE = 0x00,

    // ---- locomotion (held buttons keep these alive) ----
    WALLE_CMD_MOVE_FORWARD  = 0x01,
    WALLE_CMD_MOVE_BACKWARD = 0x02,
    WALLE_CMD_TURN_LEFT     = 0x03,
    WALLE_CMD_TURN_RIGHT    = 0x04,
    WALLE_CMD_ROTATE_LEFT   = 0x05,
    WALLE_CMD_ROTATE_RIGHT  = 0x06,

    // ---- the single "everything off" command ----
    // Highest priority from every source. Never ignored.
    WALLE_CMD_STOP          = 0x07,

    // ---- modes ----
    WALLE_CMD_DANCE         = 0x10,
    WALLE_CMD_EXPLORE       = 0x11,
    WALLE_CMD_IDLE          = 0x12,
    WALLE_CMD_AUTONOMOUS_ON = 0x13,
    WALLE_CMD_AUTONOMOUS_OFF= 0x14,

    // ---- voice ----
    // The robot owns the speaker and the Gemini TTS key; a controller
    // only ever asks for an ACTION, never for audio.
    //   TALK = "think of something to say, then say it out loud"
    //   JOKE = "tell me a joke, out loud"
    // Both are single fixed-size commands, so no new wire format and
    // no variable-length frame is introduced.
    //
    // Speech-to-Text deliberately stays on the controller/app side:
    // the robot has no microphone.
    WALLE_CMD_TALK          = 0x15,
    WALLE_CMD_JOKE          = 0x16,

    // ---- measured motion (OPEN LOOP - there are no wheel encoders) ----
    // "move some steps" and "turn around" as a controller would ask
    // for them. The robot converts a distance into a duration using
    // its own geometry (WHEEL_CIRCUMFERENCE_CM / STEP_DISTANCE_CM /
    // TURN_360_MS in include/config.h) and then times it, so the
    // distance is an ESTIMATE, not a measurement.
    WALLE_CMD_MOVE_STEPS    = 0x17,   // arg = step count, 1..WALLE_MAX_STEPS
    WALLE_CMD_TURN_AROUND   = 0x18,   // 180 degrees on the spot
    WALLE_CMD_TURN_DEGREES  = 0x1C,   // arg = degrees, 1..360

    // ---- sensors ----
    // The cliff sensor is sampled continuously on the robot; this is
    // only "tell me what you see right now".
    WALLE_CMD_READ_SENSOR   = 0x19,   // answered with WALLE_ST_SENSOR

    // ---- ask / speak with your own words ----
    // Sent as a fixed packet, immediately followed by a TEXT frame
    // carrying the words. The robot answers with Gemini and then
    // speaks. See WALLE_PROTO_TEXT_HEADER.
    WALLE_CMD_ASK           = 0x1A,   // text op 0x01 = the question
    WALLE_CMD_SPEAK         = 0x1B,   // text op 0x02 = say exactly this

    // ---- the persona ----
    // The app, not the firmware, decides who the robot is. It sends
    // SET_PERSONA followed by a TEXT frame (op 0x04) containing
    // compact JSON, before the first question. The robot stores the
    // prompt as its Gemini system instruction and appends the suffix
    // to every reply.
    //
    // Why the app owns this: a demo build should be able to change the
    // robot's personality without reflashing, and 240 bytes is far too
    // small a budget to ship every persona the firmware could want.
    //
    // The ACK for this command is sent AFTER the text frame has been
    // parsed, so it is the only command whose acknowledgement can
    // report a text-frame failure (WALLE_ERR_BAD_ARG).
    WALLE_CMD_SET_PERSONA   = 0x1D,

    // ---- expressions ----
    WALLE_CMD_EXPR_HAPPY     = 0x20,
    WALLE_CMD_EXPR_THINKING  = 0x21,
    WALLE_CMD_EXPR_SURPRISED = 0x22,
    WALLE_CMD_EXPR_CONFUSED  = 0x23,
    WALLE_CMD_EXPR_IDLE      = 0x24,

    // ---- link housekeeping ----
    WALLE_CMD_HELLO = 0x30,   // "remote is powered on, who is out there?"
    WALLE_CMD_PING  = 0x31,   // keepalive, also refreshes the safety timer
    WALLE_CMD_BYE   = 0x32,   // clean shutdown, robot stops immediately

    WALLE_CMD_MAX
};

// Is this a command that drives the wheels? These are the ones
// that put the robot into STATE_MANUAL and that the link timeout
// watchdog is allowed to stop.
//
// MOVE_STEPS and the turns are included even though they are not a
// contiguous range: the robot runs them through the same motion
// path, so the SAME timeout, the SAME priority rules and the SAME
// "exactly one source owns the wheels" guarantee must apply to them.
// Leaving them out would be a silent hole in the safety model.
static inline bool walle_cmd_is_motion(uint8_t c) {
    return (c >= WALLE_CMD_MOVE_FORWARD && c <= WALLE_CMD_ROTATE_RIGHT) ||
           c == WALLE_CMD_MOVE_STEPS ||
           c == WALLE_CMD_TURN_AROUND ||
           c == WALLE_CMD_TURN_DEGREES;
}

static inline const char* walle_command_name(uint8_t c) {
    switch (c) {
        case WALLE_CMD_MOVE_FORWARD:  return "move_forward";
        case WALLE_CMD_MOVE_BACKWARD: return "move_backward";
        case WALLE_CMD_TURN_LEFT:     return "turn_left";
        case WALLE_CMD_TURN_RIGHT:    return "turn_right";
        case WALLE_CMD_ROTATE_LEFT:   return "rotate_left";
        case WALLE_CMD_ROTATE_RIGHT:  return "rotate_right";
        case WALLE_CMD_STOP:          return "stop";
        case WALLE_CMD_DANCE:         return "dance";
        case WALLE_CMD_EXPLORE:       return "explore";
        case WALLE_CMD_IDLE:           return "idle";
        case WALLE_CMD_AUTONOMOUS_ON: return "autonomous_on";
        case WALLE_CMD_AUTONOMOUS_OFF:return "autonomous_off";
        case WALLE_CMD_TALK:          return "talk";
        case WALLE_CMD_JOKE:          return "joke";
        case WALLE_CMD_MOVE_STEPS:    return "move_steps";
        case WALLE_CMD_TURN_AROUND:   return "turn_around";
        case WALLE_CMD_TURN_DEGREES:  return "turn_degrees";
        case WALLE_CMD_READ_SENSOR:   return "read_sensor";
        case WALLE_CMD_ASK:           return "ask";
        case WALLE_CMD_SPEAK:         return "speak";
        case WALLE_CMD_SET_PERSONA:   return "set_persona";
        case WALLE_CMD_EXPR_HAPPY:     return "expression_happy";
        case WALLE_CMD_EXPR_THINKING:  return "expression_thinking";
        case WALLE_CMD_EXPR_SURPRISED: return "expression_surprised";
        case WALLE_CMD_EXPR_CONFUSED:  return "expression_confused";
        case WALLE_CMD_EXPR_IDLE:      return "expression_idle";
        case WALLE_CMD_HELLO:          return "hello";
        case WALLE_CMD_PING:           return "ping";
        case WALLE_CMD_BYE:            return "bye";
        default:                       return "?";
    }
}

// ------------------------------------------------------------
//  Status messages (robot -> controller).
//  Small and event driven: the robot NEVER sends a status stream,
//  it only answers a command, a state change or a ping.
// ------------------------------------------------------------
enum WalleStatus : uint8_t {
    WALLE_ST_NONE = 0x00,
    WALLE_ST_WELCOME = 0x80,   // robot saw a HELLO; arg = robot state
    WALLE_ST_ACK,              // command executed, cmd = what ran
    WALLE_ST_ERROR,            // arg = WalleError
    WALLE_ST_ROBOT_STATE,      // arg = RobotState
    WALLE_ST_REMOTE_STATE,     // value = WalleRemoteState. There is no
    // radio remote any more, so only DISCONNECTED is ever
    // sent. It stays in the vocabulary so the app's remote
    // panel reads "no remote" instead of hanging on
    // "searching" for ever.
    WALLE_ST_BATTERY,          // arg = millivolts, 0 = unknown/no sensor
    WALLE_ST_PONG,             // reply to PING, arg = robot state
    WALLE_ST_SENSOR,           // value = WalleCliffState, arg = ground mm
    WALLE_ST_CLIFF,            // value = WalleCliffState (transition only)
};

enum WalleError : uint8_t {
    WALLE_ERR_NONE = 0,
    WALLE_ERR_BAD_PACKET,      // magic/version/len wrong
    WALLE_ERR_UNKNOWN_CMD,     // not in WalleCommand
    WALLE_ERR_NOT_CONFIGURED,  // e.g. motors have no pins
    WALLE_ERR_BUSY,            // another source owns the motors
    WALLE_ERR_CLIFF,           // refused: the sensor says there is a drop
    WALLE_ERR_SENSOR_FAULT,    // refused: the sensor is not reporting at all
    WALLE_ERR_BAD_ARG,         // argument out of range (e.g. 0 steps)
    WALLE_ERR_LINK_TIMEOUT,    // the controller went quiet while driving; the
                               // robot stopped itself. Send something every
                               // APP_TIMEOUT_MS while driving.
};

// ------------------------------------------------------------
//  Cliff sensor state
//  ------------------------------------------------------------
//  WALL-E's HC-SR04 points DOWN at SENSOR_MOUNT_ANGLE_DEG and watches
//  the floor in front of the wheels. While there is floor the ground
//  distance is SENSOR_NOMINAL_GROUND_CM; at the edge of a table the
//  floor disappears, the reading grows, and the robot must stop.
//
//  This mirrors robot_s3/src/cliff_sensor.h so a controller can draw
//  the same picture without including the robot firmware.
enum WalleCliffState : uint8_t {
    WALLE_CLIFF_UNKNOWN = 0,   // pins unset, or no reading yet
    WALLE_CLIFF_GROUND  = 1,   // floor found, safe to drive
    WALLE_CLIFF_WARN    = 2,   // closer to the edge than the warn band
    WALLE_CLIFF_DROP    = 3,   // no floor -> STOP
    WALLE_CLIFF_FAULT   = 4,   // sensor not responding -> STOP (fail safe)
};

static inline const char* walle_cliff_state_name(uint8_t s) {
    switch (s) {
        case WALLE_CLIFF_UNKNOWN: return "unknown";
        case WALLE_CLIFF_GROUND:  return "ground";
        case WALLE_CLIFF_WARN:    return "warn";
        case WALLE_CLIFF_DROP:    return "drop";
        case WALLE_CLIFF_FAULT:   return "fault";
        default:                  return "?";
    }
}

// The radio remote has been removed. This enum survives only so the
// app can render "no remote" honestly in one line instead of waiting
// for a status that will never come; DISCONNECTED is the only value
// the robot ever sends.
enum WalleRemoteState : uint8_t {
    WALLE_REMOTE_DISCONNECTED = 0,
    WALLE_REMOTE_CONNECTING   = 1,
    WALLE_REMOTE_CONNECTED    = 2,
    WALLE_REMOTE_TIMEOUT      = 3,
};

static inline const char* walle_remote_state_name(uint8_t s) {
    switch (s) {
        case WALLE_REMOTE_DISCONNECTED: return "DISCONNECTED";
        case WALLE_REMOTE_CONNECTING:   return "CONNECTING";
        case WALLE_REMOTE_CONNECTED:    return "CONNECTED";
        case WALLE_REMOTE_TIMEOUT:      return "TIMEOUT";
        default:                        return "?";
    }
}

// ------------------------------------------------------------
//  Packet flags
// ------------------------------------------------------------
#define WALLE_FLAG_HELD  0x01   // button is physically down (vs. a tap)

// ------------------------------------------------------------
//  The packet
// ------------------------------------------------------------
//  Wire layout (10 bytes, little endian):
//    [0] magic    0xA5
//    [1] version  0x01
//    [2] type     WalleMsgType
//    [3] cmd      WalleCommand / WalleStatus
//    [4] value    small argument (speed bucket, state, error, ...)
//    [5] seq      rolling counter, wraps at 255
//    [6] flags    WALLE_FLAG_*
//    [7] reserved 0
//    [8..9] arg   uint16 LE, optional (e.g. millivolts)
struct WallePacket {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  cmd;
    uint8_t  value;
    uint8_t  seq;
    uint8_t  flags;
    uint8_t  reserved;
    uint16_t arg;
};

static_assert(sizeof(WallePacket) == WALLE_PROTO_PACKET_SIZE,
              "WallePacket must stay exactly 10 bytes on the wire");

// Build a packet with every field initialised. Using an
// aggregate initialiser also guarantees `reserved` is zeroed,
// which keeps the bytes deterministic and cheap to compare.
static inline WallePacket walle_make_packet(uint8_t type, uint8_t cmd,
                                            uint8_t value = 0, uint8_t flags = 0,
                                            uint16_t arg = 0, uint8_t seq = 0) {
    WallePacket p;
    p.magic    = WALLE_PROTO_MAGIC;
    p.version  = WALLE_PROTO_VERSION;
    p.type     = type;
    p.cmd      = cmd;
    p.value    = value;
    p.seq      = seq;
    p.flags    = flags;
    p.reserved = 0;
    p.arg      = arg;
    return p;
}

// Reject anything that is not exactly our protocol. Also guards
// against a peer that is running an older/newer revision.
static inline bool walle_packet_valid(const WallePacket& p, uint8_t len) {
    return len == WALLE_PROTO_PACKET_SIZE &&
           p.magic   == WALLE_PROTO_MAGIC &&
           p.version == WALLE_PROTO_VERSION;
}

// ============================================================
//  TEXT FRAMES (WALLE_MSG_TEXT)
//  ------------------------------------------------------------
//  The 10-byte packet has no room for words, so text uses a second
//  frame: a fixed 8-byte header followed by `len` UTF-8 bytes.
//
//    [0] magic    0xA5
//    [1] version  0x01
//    [2] type     0x03 (WALLE_MSG_TEXT)
//    [3] op       WalleTextOp
//    [4] flags    WALLE_FLAG_*
//    [5..6] len   uint16 LE, payload byte count (0..WALLE_TEXT_MAX)
//    [7] reserved 0
//    [8 .. 8+len-1]  UTF-8 payload, no terminator, no quotes
//
//  WHY A SEPARATE FRAME AND NOT A BIGGER PACKET
//  -------------------------------------------
//  WallePacket is fixed at 10 bytes on purpose: that is comfortably
//  inside a single TCP read, and a fixed size means a receiver can
//  validate and dispatch a frame with no buffering. A variable
//  "say this exact sentence" field would force every controller to
//  buffer, reassemble and length-check, and would slow down every frame
//  path for the sake of a feature only a phone can actually use.
//
//  So: fixed packets on the radio, fixed-packet framing on TCP, and
//  this length-prefixed frame only on the app link. WALLE_CMD_ASK and
//  WALLE_CMD_SPEAK announce that a text frame follows; the text frame
//  carries the actual words.
// ============================================================
#define WALLE_PROTO_TEXT_HEADER 8
#define WALLE_TEXT_MAX           240   // matches TTS_MAX_CHARS; keeps RAM small

enum WalleTextOp : uint8_t {
    WALLE_OP_ASK    = 0x01,   // app -> robot: the question / prompt
    WALLE_OP_SPEAK  = 0x02,   // app -> robot: say exactly this, no Gemini
    WALLE_OP_REPLY  = 0x03,   // robot -> app: the answer, as text
    WALLE_OP_PERSONA = 0x04,  // app -> robot: compact JSON persona
};

static inline const char* walle_text_op_name(uint8_t op) {
    switch (op) {
        case WALLE_OP_ASK:    return "ask";
        case WALLE_OP_SPEAK:  return "speak";
        case WALLE_OP_REPLY:  return "reply";
        case WALLE_OP_PERSONA: return "persona";
        default:              return "?";
    }
}

// ------------------------------------------------------------
//  The persona payload
//  ------------------------------------------------------------
//  WALLE_OP_PERSONA carries compact JSON, not prose, because a text
//  frame is 240 bytes TOTAL and that is the entire budget:
//
//    {"n":"Vulkan","s":"Friend!","m":"happy","p":"You are ..."}
//
//    n  the robot's name          (required)
//    s  suffix appended to every reply before it is spoken
//    m  mood, informational       (required)
//    p  the Gemini system instruction                    (required)
//
//  The keys are single letters on purpose. Spelling them out costs
//  about 20 bytes, which on a payload this size is the difference
//  between a usable prompt and no persona at all.
//
//  Every key is required. A clipped or half-parsed payload is
//  refused with WALLE_ERR_BAD_ARG rather than silently half-applied,
//  because a robot with the wrong personality but no error message is
//  much harder to debug than one that says no.
// ============================================================

// ------------------------------------------------------------
//  BYTE-EXACT ON PURPOSE
//
//  DO NOT "simplify" lenLo/lenHi back into a uint16_t.
//
//  A uint16_t has 2-byte alignment, so the compiler inserts a padding
//  byte and places the length at OFFSET 6 instead of 5, making the
//  struct 10 bytes rather than 8. The app reads the length from offset
//  5 and expects an 8-byte header, so EVERY text frame - ask, speak
//  and set_persona alike - would be misparsed and the stream would
//  resynchronise to nothing. This was a real bug; the static_asserts
//  below exist so it cannot come back.
//
//  The wire is an unaligned byte stream, so the header is built from
//  individual bytes.
// ------------------------------------------------------------
struct WalleTextHeader {
    uint8_t magic;      // 0
    uint8_t version;    // 1
    uint8_t type;       // 2
    uint8_t op;         // 3
    uint8_t flags;      // 4
    uint8_t lenLo;      // 5   length, low byte
    uint8_t lenHi;      // 6   length, high byte
    uint8_t reserved;   // 7
};

static_assert(sizeof(WalleTextHeader) == WALLE_PROTO_TEXT_HEADER,
              "WalleTextHeader must be exactly 8 bytes");
static_assert(offsetof(WalleTextHeader, lenLo) == 5,
              "the length must live at offset 5, as the app reads it");
static_assert(offsetof(WalleTextHeader, lenHi) == 6,
              "the length must live at offset 6, as the app reads it");

// Little-endian length access, on the wire's terms.
static inline uint16_t walle_text_len(const WalleTextHeader& h) {
    return (uint16_t)h.lenLo | ((uint16_t)h.lenHi << 8);
}

static inline void walle_text_set_len(WalleTextHeader& h, uint16_t len) {
    h.lenLo = (uint8_t)(len & 0xFF);
    h.lenHi = (uint8_t)(len >> 8);
}

// Builds a header. `len` is NOT clamped here, so an oversized payload
// is reported to the caller rather than silently shortened.
static inline WalleTextHeader walle_make_text_header(uint8_t op, uint16_t len,
                                                      uint8_t flags = 0) {
    WalleTextHeader h;
    h.magic    = WALLE_PROTO_MAGIC;
    h.version  = WALLE_PROTO_VERSION;
    h.type     = WALLE_MSG_TEXT;
    h.op       = op;
    h.flags    = flags;
    walle_text_set_len(h, len);
    h.reserved = 0;
    return h;
}

// Validates only the fixed part; the caller must still check the
// length against WALLE_TEXT_MAX before reading the payload.
static inline bool walle_text_valid(const WalleTextHeader& h, uint8_t op) {
    const uint16_t len = walle_text_len(h);
    return h.magic   == WALLE_PROTO_MAGIC &&
           h.version == WALLE_PROTO_VERSION &&
           h.type    == WALLE_MSG_TEXT &&
           h.op      == op &&
           len > 0 && len <= WALLE_TEXT_MAX;
}
