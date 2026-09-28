#include "dance.h"
#include "motor_controller.h"
#include "oled_display.h"
#include "log.h"

static const char* TAG = "ROBOT";

Dance dance;

// ---- the choreography -------------------------------------
// 0..255 = speed, negative = reverse. Faces change per step so the
// expression follows the music-less beat.
static const Dance::Step kRoutine[] = {
    //  l1    l2    r1    r2   dur    face
    {  90,   90,  -90,  -90,  DANCE_STEP_MS, EXPR_DANCING },  // lean / spin left
    { -90,  -90,   90,   90,  DANCE_STEP_MS, EXPR_DANCING },  // spin right
    { 150,  150,  150,  150,  DANCE_STEP_MS, EXPR_SPEAKING  },  // forward
    {   0,    0,    0,    0,  DANCE_PAUSE_MS, EXPR_HAPPY     },  // freeze + happy face
    { -120, -120,  120,  120,  DANCE_STEP_MS, EXPR_DANCING },  // turn
    {  120,  120, -120, -120,  DANCE_STEP_MS, EXPR_DANCING },  // turn back
    {   0,    0,    0,    0,  DANCE_PAUSE_MS, EXPR_SURPRISED },  // pose
    { 180,   60,  180,   60,  DANCE_STEP_MS, EXPR_ANGRY     },  // wobble
    {  60,  180,   60,  180,  DANCE_STEP_MS, EXPR_HAPPY     },  // wobble back
    {   0,    0,    0,    0,  DANCE_PAUSE_MS, EXPR_HAPPY     },  // bow
};
static const uint8_t kStepCount = sizeof(kRoutine) / sizeof(kRoutine[0]);
// ----------------------------------------------------------

void Dance::begin() {
    _running = false;
    _index = 0;
    _loop = 0;
    _paused = false;
}

void Dance::start(uint8_t loops) {
    if (!motors.motorsConfigured()) {
        LOGW(TAG, "Dancing - but motor pins are not configured");
    }
    _loops = loops;
    _index = 0;
    _loop = 0;
    _running = true;
    _paused = false;
    LOGI(TAG, "Dancing");
}

void Dance::stop() {
    _running = false;
    _paused = false;
    motors.stop();
    LOGI(TAG, "Dance stopped");
}

void Dance::setPause(bool on) {
    if (on == _paused) return;
    _paused = on;
    if (on) motors.stop();
}

void Dance::runStep(uint32_t now) {
    const Step& s = kRoutine[_index];
    motors.setMotor(0, s.l1);
    motors.setMotor(1, s.l2);
    motors.setMotor(2, s.r1);
    motors.setMotor(3, s.r2);
    oled.setExpression(s.face);
    _stepEndMs = now + (s.durationMs ? s.durationMs : DANCE_STEP_MS);
}

void Dance::update(uint32_t now) {
    if (!_running) return;
    if (now < _stepEndMs) return;   // step still in progress

    _index++;
    if (_index >= kStepCount) {
        _index = 0;
        _loop++;
        if (_loops != 0 && _loop >= _loops) {
            stop();
            oled.setExpression(EXPR_HAPPY);
            return;
        }
        // short breath between loops
        motors.stop();
        _stepEndMs = now + DANCE_PAUSE_MS;
        oled.setExpression(EXPR_HAPPY);
        return;
    }
    runStep(now);
}
