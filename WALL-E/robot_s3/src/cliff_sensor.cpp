// ============================================================
//  Cliff sensor - HC-SR04 implementation
//  See cliff_sensor.h for the reasoning and the mount geometry.
// ============================================================
#include "cliff_sensor.h"
#include "log.h"

static const char* TAG = "SENSOR";

CliffSensor cliffSensor;

namespace {

// Speed of sound in cm per microsecond: 343 m/s = 0.0343 cm/us, so
// the round trip is 2 x 0.0343 and one full 58/us is the classic
// constant. 58 is the datasheet number and is accurate enough here.
constexpr uint16_t kUsPerCm = 58;

// The HC-SR04 wants 10 us low, 10 us high, then goes back low.
constexpr uint8_t kTrigPulseUs = 10;

}  // namespace

// ------------------------------------------------------------
bool CliffSensor::begin() {
#if SENSOR_ENABLE
    if (PIN_IS_UNSET(SENSOR_TRIG_PIN) || PIN_IS_UNSET(SENSOR_ECHO_PIN)) {
        _configured = false;
        LOGW(TAG, "Pins not set in include/config.h - cliff detection DISABLED");
        LOGW(TAG, "Set SENSOR_TRIG_PIN and SENSOR_ECHO_PIN (ECHO via a 1k/2k divider!)");
        return false;
    }

    pinMode(SENSOR_TRIG_PIN, OUTPUT);
    digitalWrite(SENSOR_TRIG_PIN, LOW);

    // ECHO is an INPUT driven by the sensor. No internal pull: an
    // ultrasonic echo is a clean 5 V (or 3.3 V after the divider) push
    // pull-up signal, and a pull resistor would only soften the edge.
    pinMode(SENSOR_ECHO_PIN, INPUT);

    // 45 deg -> cos(45 deg) = 0.7071. Precomputed so the hot path
    // never calls cosf().
    _cosAngle = cosf((float)SENSOR_MOUNT_ANGLE_DEG * 0.0174532925f);

    _configured = true;
    _armed = true;
    _state = WALLE_CLIFF_UNKNOWN;
    _nextPollMs = millis();        // first reading straight away
    _stateChangedMs = millis();

    LOGI(TAG, "HC-SR04 ready (TRIG %d, ECHO %d, mount %d deg)",
         SENSOR_TRIG_PIN, SENSOR_ECHO_PIN, SENSOR_MOUNT_ANGLE_DEG);
    LOGI(TAG, "Expecting %.1f cm of floor, trips at +%.1f cm, clears at -%.1f cm",
         (double)SENSOR_NOMINAL_GROUND_CM, (double)SENSOR_TRIP_DROP_CM,
         (double)SENSOR_WARN_CLEAR_CM);
    LOGW(TAG, "If WALL-E will not move, SENSOR_NOMINAL_GROUND_CM is wrong - measure it");
    return true;
#else
    _configured = false;
    LOGW(TAG, "Disabled in config.h (SENSOR_ENABLE=0)");
    return false;
#endif
}

// ------------------------------------------------------------
//  One echo measurement. Bounded by SENSOR_ECHO_TIMEOUT_US so a
//  disconnected ECHO can never hang the loop.
// ------------------------------------------------------------
bool CliffSensor::sampleOnce(uint16_t* slantCmOut) {
    if (!_armed) return false;

    digitalWrite(SENSOR_TRIG_PIN, LOW);
    delayMicroseconds(4);
    digitalWrite(SENSOR_TRIG_PIN, HIGH);
    delayMicroseconds(kTrigPulseUs);
    digitalWrite(SENSOR_TRIG_PIN, LOW);

    // Wait for the echo to go high. Bounded.
    uint32_t t0 = micros();
    while (digitalRead(SENSOR_ECHO_PIN) == LOW) {
        if ((uint32_t)(micros() - t0) > SENSOR_ECHO_TIMEOUT_US) return false;
    }

    // Measure how long it stays high: that is the round trip.
    t0 = micros();
    while (digitalRead(SENSOR_ECHO_PIN) == HIGH) {
        if ((uint32_t)(micros() - t0) > SENSOR_ECHO_TIMEOUT_US) {
            // A stuck-high ECHO. Some cheap modules sit high when
            // they see nothing, so this is NOT treated as a distance.
            return false;
        }
    }

    const uint32_t durationUs = (uint32_t)(micros() - t0);
    if (durationUs < kUsPerCm) return false;               // sub-centimetre noise

    const uint16_t cm = (uint16_t)(durationUs / kUsPerCm);
    if (cm < SENSOR_MIN_VALID_CM) return false;
    if (cm > SENSOR_CLAMP_MAX_CM) return false;

    *slantCmOut = cm;
    return true;
}

void CliffSensor::resetHistory() {
    _sampleCount = 0;
    _sampleNext  = 0;
    _missStreak  = 0;
    for (uint8_t i = 0; i < SENSOR_SAMPLES; i++) _samples[i] = 0;
}

// ------------------------------------------------------------
//  Median of the sample ring. Rejects the single wild reading the
//  HC-SR04 produces on a soft or angled surface, which is the whole
//  reason WALL-E must not act on one measurement.
// ------------------------------------------------------------
static uint16_t medianOf(uint16_t* v, uint8_t n) {
    if (n == 0) return 0;
    // Insertion sort: n is at most SENSOR_SAMPLES (5), so this is
    // cheaper and clearer than anything fancier.
    for (uint8_t i = 1; i < n; i++) {
        const uint16_t key = v[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return v[n / 2];
}

void CliffSensor::classify(uint32_t now) {
    if (_sampleCount == 0) return;

    _slantCm = medianOf(_samples, _sampleCount);

    // The only maths in the whole sensor: the sensor looks down at
    // SENSOR_MOUNT_ANGLE_DEG, so the slant range it reports is longer
    // than the true vertical drop by 1/cos(angle).
    _groundCm = (uint16_t)(_slantCm * _cosAngle + 0.5f);

    const float ground = (float)_groundCm;
    uint8_t next;

    if (ground >= (float)SENSOR_NOMINAL_GROUND_CM + SENSOR_TRIP_DROP_CM) {
        // No floor in front of the wheels.
        next = WALLE_CLIFF_DROP;
    } else if (ground <= (float)SENSOR_NOMINAL_GROUND_CM - SENSOR_WARN_CLEAR_CM) {
        // Solidly on the floor. Also covers "the robot got lifted up",
        // which reads as a suspiciously SHORT distance.
        next = WALLE_CLIFF_GROUND;
    } else {
        // Inside the band around the nominal reading: close to the
        // edge, or the sensor is reading something odd. Drive, but
        // slowly and keep looking.
        next = WALLE_CLIFF_WARN;
    }

    if (next != _state) {
        const char* from = nameOf(_state);
        _state = next;
        _stateChangedMs = now;

        if (next == WALLE_CLIFF_DROP) {
            LOGW(TAG, "DROP - no floor (%.1f cm, expected %.1f cm)", (double)ground,
                 (double)SENSOR_NOMINAL_GROUND_CM);
        } else {
            LOGI(TAG, "%s -> %s (%.1f cm)", from, nameOf(next), (double)ground);
        }
        _lastReported = next;
    }
}

// ------------------------------------------------------------
void CliffSensor::update(uint32_t now) {
    if (!_configured) {
        _state = WALLE_CLIFF_UNKNOWN;
        return;
    }

    if ((int32_t)(now - _nextPollMs) < 0) return;
    _nextPollMs = now + SENSOR_INTERVAL_MS;

    uint16_t cm = 0;
    if (!sampleOnce(&cm)) {
        if (++_missStreak >= SENSOR_FAULT_LIMIT) {
            // Never believe a silent sensor.
            if (_state != WALLE_CLIFF_FAULT) {
                LOGE(TAG, "No echo %u times - sensor FAULT", (unsigned)_missStreak);
                _state = WALLE_CLIFF_FAULT;
                _stateChangedMs = now;
                _lastReported = WALLE_CLIFF_FAULT;
                resetHistory();
                _slantCm = 0;
                _groundCm = 0;
            }
        }
        return;
    }

    // A valid echo clears the fault immediately - a sensor that was
    // nudged and now answers must not leave WALL-E stuck forever.
    _missStreak = 0;
    if (_state == WALLE_CLIFF_FAULT || _state == WALLE_CLIFF_UNKNOWN) {
        LOGI(TAG, "Sensor recovered");
        resetHistory();
    }

    _samples[_sampleNext] = cm;
    _sampleNext = (uint8_t)((_sampleNext + 1) % SENSOR_SAMPLES);
    if (_sampleCount < SENSOR_SAMPLES) _sampleCount++;

    classify(now);
}

bool CliffSensor::pollNow() {
    if (!_configured) return false;

    // Respect the datasheet: never retrigger inside 60 ms, whatever
    // the caller asks for.
    const uint32_t now = millis();
    if ((int32_t)(now - _nextPollMs) < 0) delay(_nextPollMs - now);

    uint16_t cm = 0;
    if (!sampleOnce(&cm)) {
        if (++_missStreak >= SENSOR_FAULT_LIMIT) {
            _state = WALLE_CLIFF_FAULT;
            _lastReported = _state;
            _stateChangedMs = millis();
        }
        return false;
    }
    _missStreak = 0;

    _samples[_sampleNext] = cm;
    _sampleNext = (uint8_t)((_sampleNext + 1) % SENSOR_SAMPLES);
    if (_sampleCount < SENSOR_SAMPLES) _sampleCount++;

    _nextPollMs = millis() + SENSOR_INTERVAL_MS;
    classify(millis());
    return true;
}

// ------------------------------------------------------------
bool CliffSensor::detect(float* proximityOut) {
    // 0 = clear, 1 = on the threshold. Computed from the live reading
    // so a caller can slow down as it approaches the edge instead of
    // finding out at the last moment.
    float p = 0.0f;
    if (groundCm() > 0) {
        const float span = (float)SENSOR_TRIP_DROP_CM;
        p = ((float)groundCm() - (float)SENSOR_NOMINAL_GROUND_CM) / span;
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
    }
    if (proximityOut) *proximityOut = p;
    return blocked();
}

const char* CliffSensor::nameOf(uint8_t s) {
    return walle_cliff_state_name(s);
}