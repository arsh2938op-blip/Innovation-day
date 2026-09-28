#include "oled_display.h"
#include "log.h"

static const char* TAG = "OLED";

OledDisplay oled;

const char* OledDisplay::nameOf(Expression e) {
    switch (e) {
        case EXPR_BOOT:       return "boot";
        case EXPR_IDLE:       return "idle";
        case EXPR_LISTENING:  return "listening";
        case EXPR_THINKING:   return "thinking";
        case EXPR_SPEAKING:   return "speaking";
        case EXPR_HAPPY:      return "happy";
        case EXPR_CONFUSED:   return "confused";
        case EXPR_SURPRISED:  return "surprised";
        case EXPR_ANGRY:      return "angry";
        case EXPR_DANCING:    return "dancing";
        case EXPR_EXPLORING:  return "exploring";
        case EXPR_OFFLINE:    return "offline";
        case EXPR_ERROR:      return "error";
        default:              return "sleeping";
    }
}

bool OledDisplay::begin() {
#if !WALLE_ENABLE_OLED
    LOGW(TAG, "Disabled in config.h");
    return false;
#else
    if (PIN_IS_UNSET(OLED_I2C_SDA_PIN) || PIN_IS_UNSET(OLED_I2C_SCL_PIN)) {
        LOGW(TAG, "I2C pins not configured in include/config.h - display disabled");
        return false;
    }
    Wire.begin(OLED_I2C_SDA_PIN, OLED_I2C_SCL_PIN, 100000);

    _dpy = new Adafruit_SSD1306(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
    if (!_dpy->begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        LOGE(TAG, "SSD1306 not found at 0x%02X (check wiring / address)", OLED_I2C_ADDR);
        delete _dpy;
        _dpy = nullptr;
        return false;
    }
    _dpy->clearDisplay();
    _dpy->display();
    _ready = true;
    LOGI(TAG, "Ready (%dx%d @ 0x%02X)", OLED_WIDTH, OLED_HEIGHT, OLED_I2C_ADDR);
    return true;
#endif
}

void OledDisplay::setExpression(Expression e) {
    if (e == _expr) return;
    _expr = e;
    _animPhaseMs = 0;
    _lastFrameMs = 0;   // force a redraw next update()
}

void OledDisplay::setStatus(const char* text) {
    if (!text) { _status[0] = 0; return; }
    strncpy(_status, text, sizeof(_status) - 1);
    _status[sizeof(_status) - 1] = 0;
}

void OledDisplay::setCaption(const char* text) {
    if (!text) { _caption[0] = 0; return; }
    strncpy(_caption, text, sizeof(_caption) - 1);
    _caption[sizeof(_caption) - 1] = 0;
}

void OledDisplay::splash(const char* line1, const char* line2) {
    if (!_ready) return;
    _dpy->clearDisplay();
    _dpy->setTextColor(SSD1306_WHITE);
    _dpy->setTextSize(1);
    _dpy->setCursor(0, 4);
    _dpy->println("  WALL-E");
    _dpy->setCursor(0, 24);
    _dpy->println(line1 ? line1 : "");
    _dpy->setCursor(0, 40);
    _dpy->println(line2 ? line2 : "");
    _dpy->display();
}

void OledDisplay::update(uint32_t now) {
    if (!_ready) return;
    if (now - _lastFrameMs < OLED_FRAME_MS) return;
    _lastFrameMs = now;
    _animPhaseMs += OLED_FRAME_MS;

    // blink state shared by several expressions
    if (now - _lastBlinkMs > OLED_BLINK_INTERVAL_MS) {
        _lastBlinkMs = now;
        _blink = !_blink;
    }

    drawFace(_expr, now);
    _dpy->display();
}

// ---------------- face drawing ----------------
void OledDisplay::drawEyes(Expression e, int cx, int cy, bool blink) {
    _dpy->setTextColor(SSD1306_WHITE);
    const int dx = 22, ew = 14, eh = 16;   // eye offset / size
    const int ph = (_animPhaseMs / 120) % 4;  // 0..3 animation phase

    switch (e) {
        case EXPR_LISTENING:
            // tall wide eyes + bouncing pupil
            _dpy->fillRoundRect(cx - dx - ew/2, cy - eh, ew, eh * 2, 4, SSD1306_WHITE);
            _dpy->fillRoundRect(cx + dx - ew/2, cy - eh, ew, eh * 2, 4, SSD1306_WHITE);
            _dpy->fillRect(cx - dx - 2, cy - 4 + ph * 2, 4, 4, SSD1306_BLACK);
            _dpy->fillRect(cx + dx - 2, cy - 4 + ph * 2, 4, 4, SSD1306_BLACK);
            break;

        case EXPR_SPEAKING: {
            // squash/stretch while "talking"
            int h = (ph % 2) ? 10 : 16;
            _dpy->drawRoundRect(cx - dx - ew/2, cy - h, ew, h * 2, 3, SSD1306_WHITE);
            _dpy->drawRoundRect(cx + dx - ew/2, cy - h, ew, h * 2, 3, SSD1306_WHITE);
            _dpy->fillRect(cx - dx - 2, cy - 3, 4, 5, SSD1306_WHITE);
            _dpy->fillRect(cx + dx - 2, cy - 3, 4, 5, SSD1306_WHITE);
            break;
        }

        case EXPR_THINKING:
            // half closed + one raised
            _dpy->drawLine(cx - dx - 7, cy, cx - dx + 7, cy, SSD1306_WHITE);
            _dpy->drawRoundRect(cx + dx - 6, cy - 14, 12, 28, 4, SSD1306_WHITE);
            _dpy->fillRect(cx + dx - 2, cy - 4 + ph * 3, 4, 4, SSD1306_BLACK);
            break;

        case EXPR_HAPPY:
            // upward arcs (^ ^)
            for (int i = -7; i <= 7; i++) {
                int y = cy - 3 + (7 - abs(i)) * 6 / 7;
                _dpy->drawPixel(cx - dx + i, y, SSD1306_WHITE);
                _dpy->drawPixel(cx + dx + i, y, SSD1306_WHITE);
            }
            break;

        case EXPR_DANCING: {
            // eyes swinging left/right
            int o = (ph % 2) ? 4 : -4;
            _dpy->drawRoundRect(cx - dx - 6 + o, cy - 10, 12, 20, 4, SSD1306_WHITE);
            _dpy->drawRoundRect(cx + dx - 6 - o, cy - 10, 12, 20, 4, SSD1306_WHITE);
            _dpy->fillRect(cx - dx - 3 + o, cy - 4, 4, 4, SSD1306_WHITE);
            _dpy->fillRect(cx + dx - 3 - o, cy - 4, 4, 4, SSD1306_WHITE);
            break;
        }

        case EXPR_SURPRISED:
            _dpy->drawCircle(cx - dx, cy, 10, SSD1306_WHITE);
            _dpy->drawCircle(cx + dx, cy, 10, SSD1306_WHITE);
            _dpy->fillRect(cx - dx - 2, cy - 2, 4, 4, SSD1306_WHITE);
            _dpy->fillRect(cx + dx - 2, cy - 2, 4, 4, SSD1306_WHITE);
            break;

        case EXPR_ANGRY:
            // angled brows + narrowed eyes
            _dpy->drawLine(cx - dx - 9, cy - 11, cx - dx + 8, cy - 7, SSD1306_WHITE);
            _dpy->drawLine(cx + dx + 9, cy - 11, cx + dx - 8, cy - 7, SSD1306_WHITE);
            _dpy->drawLine(cx - dx - 7, cy + 1, cx - dx + 7, cy + 1, SSD1306_WHITE);
            _dpy->drawLine(cx + dx - 7, cy + 1, cx + dx + 7, cy + 1, SSD1306_WHITE);
            break;

        case EXPR_CONFUSED:
            // one eye open, one squinted
            _dpy->drawRoundRect(cx - dx - 6, cy - 12, 12, 24, 4, SSD1306_WHITE);
            _dpy->fillRect(cx - dx - 2, cy - 3, 4, 5, SSD1306_WHITE);
            _dpy->drawLine(cx + dx - 8, cy, cx + dx + 8, cy, SSD1306_WHITE);
            _dpy->drawLine(cx, cy - 26, cx + 14, cy - 32, SSD1306_WHITE);  // "?"
            break;

        case EXPR_EXPLORING: {
            // eyes looking to the side, like scanning
            _dpy->drawRoundRect(cx - dx - 7, cy - 10, 14, 20, 4, SSD1306_WHITE);
            _dpy->drawRoundRect(cx + dx - 7, cy - 10, 14, 20, 4, SSD1306_WHITE);
            int look = ((_animPhaseMs / 250) % 2) ? 3 : -3;
            _dpy->fillRect(cx - dx - 3 + look, cy - 3, 4, 5, SSD1306_WHITE);
            _dpy->fillRect(cx + dx - 3 + look, cy - 3, 4, 5, SSD1306_WHITE);
            break;
        }

        case EXPR_OFFLINE:
            // x x
            for (int s = -1; s <= 1; s += 2) {
                _dpy->drawLine(cx - dx - 5, cy - 5 * s, cx - dx + 5, cy + 5 * s, SSD1306_WHITE);
                _dpy->drawLine(cx + dx - 5, cy - 5 * s, cx + dx + 5, cy + 5 * s, SSD1306_WHITE);
            }
            break;

        case EXPR_ERROR:
            _dpy->drawLine(cx - dx - 7, cy, cx - dx + 7, cy, SSD1306_WHITE);
            _dpy->drawLine(cx + dx - 7, cy, cx + dx + 7, cy, SSD1306_WHITE);
            break;

        case EXPR_SLEEPING:
            // closed lids
            _dpy->drawLine(cx - dx - 7, cy, cx - dx + 7, cy, SSD1306_WHITE);
            _dpy->drawLine(cx + dx - 7, cy, cx + dx + 7, cy, SSD1306_WHITE);
            break;

        case EXPR_BOOT:
        case EXPR_IDLE:
        default:
            if (blink) {
                _dpy->drawLine(cx - dx - 7, cy, cx - dx + 7, cy, SSD1306_WHITE);
                _dpy->drawLine(cx + dx - 7, cy, cx + dx + 7, cy, SSD1306_WHITE);
            } else {
                _dpy->drawRoundRect(cx - dx - 7, cy - 9, 14, 18, 4, SSD1306_WHITE);
                _dpy->drawRoundRect(cx + dx - 7, cy - 9, 14, 18, 4, SSD1306_WHITE);
                _dpy->fillRect(cx - dx - 3, cy - 3, 4, 5, SSD1306_WHITE);
                _dpy->fillRect(cx + dx - 3, cy - 3, 4, 5, SSD1306_WHITE);
            }
            break;
    }
}

void OledDisplay::drawMouth(Expression e, int cx, int cy) {
    const int y = cy + 24;
    switch (e) {
        case EXPR_HAPPY:
        case EXPR_DANCING:
            for (int i = -10; i <= 10; i++) {
                int d = cy + 28 - (10 - abs(i)) * 8 / 10;
                _dpy->drawPixel(cx + i, d, SSD1306_WHITE);
            }
            break;
        case EXPR_SPEAKING: {
            int h = ((_animPhaseMs / 90) % 3) * 3 + 2;
            _dpy->drawRoundRect(cx - 9, y - 1, 18, h + 2, 2, SSD1306_WHITE);
            break;
        }
        case EXPR_ANGRY:
        case EXPR_SURPRISED:
            _dpy->drawCircle(cx, y + 4, 6, SSD1306_WHITE);
            break;
        case EXPR_THINKING:
        case EXPR_CONFUSED:
            _dpy->drawLine(cx - 8, y + 2, cx + 8, y + 2, SSD1306_WHITE);
            _dpy->drawLine(cx + 8, y + 2, cx + 2, y + 7, SSD1306_WHITE);
            break;
        case EXPR_ERROR:
        case EXPR_OFFLINE:
            _dpy->drawLine(cx - 8, y + 4, cx + 8, y + 4, SSD1306_WHITE);
            break;
        case EXPR_LISTENING:
            _dpy->drawRoundRect(cx - 7, y, 14, 8, 3, SSD1306_WHITE);
            break;
        case EXPR_BOOT:
        case EXPR_SLEEPING:
        case EXPR_EXPLORING:
        case EXPR_IDLE:
        default:
            _dpy->drawLine(cx - 8, y + 3, cx + 8, y + 3, SSD1306_WHITE);
            break;
    }
}

void OledDisplay::drawStatus() {
    if (_status[0] == 0) return;
    _dpy->setTextSize(1);
    _dpy->setTextColor(SSD1306_WHITE);
    _dpy->setCursor(2, OLED_HEIGHT - 10);
    _dpy->print(_status);
}

void OledDisplay::drawCaption() {
    if (_caption[0] == 0) return;
    _dpy->setTextSize(1);
    _dpy->setTextColor(SSD1306_WHITE);
    _dpy->setCursor(2, 0);
    _dpy->print(_caption);
}

void OledDisplay::drawFace(Expression e, uint32_t now) {
    (void)now;
    _dpy->clearDisplay();

    const int cx = OLED_WIDTH / 2;
    const int cy = 30;

    drawEyes(e, cx, cy, _blink);
    drawMouth(e, cx, cy);

    // expression name as a tiny label
    _dpy->setTextSize(1);
    _dpy->setTextColor(SSD1306_WHITE);
    _dpy->setCursor(2, OLED_HEIGHT - 20);
    _dpy->print(nameOf(e));

    drawStatus();
    drawCaption();
}
