#include "audio_input.h"
#include "log.h"
#include <math.h>
#include <driver/i2s.h>

static const char* TAG = "MIC";

AudioInput mic;

void AudioInput::freeBuffer() {
    if (_buffer) { free(_buffer); _buffer = nullptr; }
    _cap = _length = 0;
}

bool AudioInput::begin() {
#if !WALLE_ENABLE_AUDIO_IN
    LOGW(TAG, "Disabled in config.h");
    return false;
#else
    if (PIN_IS_UNSET(MIC_I2S_BCLK_PIN) || PIN_IS_UNSET(MIC_I2S_WS_PIN) ||
        PIN_IS_UNSET(MIC_I2S_DIN_PIN)) {
        LOGW(TAG, "I2S pins not configured in include/config.h - microphone disabled");
        return false;
    }

    const i2s_port_t port = (i2s_port_t)MIC_I2S_PORT;

    i2s_config_t cfg = {};
    cfg.mode               = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.channel_format     = I2S_CHANNEL_FMT_ALL_RIGHT;   // mono, right aligned
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags   = 0;
    cfg.dma_buf_count      = 4;
    cfg.dma_buf_len        = 512;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk         = 0;
    cfg.bits_per_chan      = (i2s_bits_per_chan_t)MIC_I2S_DATA_BITS;

    if (i2s_driver_install(port, &cfg, 0, NULL) != ESP_OK) {
        LOGE(TAG, "i2s_driver_install failed");
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = MIC_I2S_BCLK_PIN;
    pins.ws_io_num    = MIC_I2S_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = MIC_I2S_DIN_PIN;

    i2s_set_pin(port, &pins);
    i2s_set_sample_rates(port, AUDIO_SAMPLE_RATE);
    i2s_zero_dma_buffer(port);

    // Clear the ~1s of garbage the DMA buffer contains after a reset
    int16_t junk[256];
    for (int i = 0; i < 8; i++) readSamples(junk, 256);

    _ready = true;
    LOGI(TAG, "Ready (I2S%d, %d Hz, %d bit)", MIC_I2S_PORT, AUDIO_SAMPLE_RATE, AUDIO_SAMPLE_BITS);
    return true;
#endif
}

size_t AudioInput::readSamples(int16_t* dst, size_t maxSamples) {
    size_t got = 0;
    if (!_ready) return 0;
    // small timeouts keep the main loop responsive
    while (got < maxSamples) {
        size_t n = 0;
        esp_err_t err = i2s_read((i2s_port_t)MIC_I2S_PORT, dst + got,
                                 (maxSamples - got) * sizeof(int16_t),
                                 &n, pdMS_TO_TICKS(20));
        if (err != ESP_OK || n == 0) break;
        got += n / sizeof(int16_t);
    }
    return got;
}

bool AudioInput::startRecording(uint16_t maxSeconds) {
    if (!_ready) return false;
    abortRecording();

    _cap = (size_t)AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * 2 * maxSeconds;
    _buffer = (uint8_t*)malloc(_cap);
    if (!_buffer) {
        LOGE(TAG, "Cannot allocate %u bytes for %us of audio (try a shorter clip)",
             (unsigned)_cap, maxSeconds);
        return false;
    }
    _length = 0;
    _recording = true;
    _speechSeen = false;
    _recStartMs = millis();
    _lastVoiceMs = 0;
    LOGI(TAG, "Recording... (max %us)", maxSeconds);
    return true;
}

void AudioInput::abortRecording() {
    _recording = false;
    freeBuffer();
}

void AudioInput::end() {
    abortRecording();
}

bool AudioInput::pollRecording() {
    if (!_recording || !_buffer) return true;

    static int16_t chunk[256];
    size_t got = readSamples(chunk, 256);
    if (got == 0) return false;

    // ---- level / VAD ----
    int32_t peak = 0;
    for (size_t i = 0; i < got; i++) {
        int32_t v = chunk[i] < 0 ? -chunk[i] : chunk[i];
        if (v > peak) peak = v;
    }
    int db = (peak > 0) ? (int)(20.0 * log10((double)peak / 32768.0)) : -90;
    _peakDb = db;

    size_t bytes = got * 2;
    if (_length + bytes > _cap) bytes = _cap - _length;      // clamp at max length
    memcpy(_buffer + _length, chunk, bytes);
    _length += bytes;

    if (db > VAD_THRESHOLD_DB) {
        _speechSeen = true;
        _lastVoiceMs = millis();
    }

    const uint32_t elapsed = millis() - _recStartMs;
    const size_t maxBytes = _cap;
    if (_length >= maxBytes) {                          // max length reached
        LOGW(TAG, "Recording hit the %u s cap and was truncated", _cap / (AUDIO_SAMPLE_RATE * 2));
        return true;
    }
    if (_speechSeen && millis() - _lastVoiceMs > VAD_SILENCE_MS) return true;  // speech + silence
    if (elapsed > 4000 && !_speechSeen) return true;                       // nothing heard
    return false;
}

// ------------------------------------------------------------
void AudioInput::runMeterTest(uint16_t seconds) {
    if (!_ready) return;
    Serial.println("Talking now - the value should rise:");
    uint32_t endMs = millis() + (uint32_t)seconds * 1000;
    while (millis() < endMs) {
        int16_t chunk[256];
        size_t got = readSamples(chunk, 256);
        if (!got) continue;
        int32_t sum = 0;
        for (size_t i = 0; i < got; i++) sum += chunk[i] * chunk[i];
        float rms = sqrt((float)sum / got);
        int db = (rms > 0) ? (int)(20.0 * log10(rms / 32768.0)) : -90;
        int bars = (db + 60) * 15 / 60;      // -60 dB .. 0 dB -> 0..15
        if (bars < 0) bars = 0;
        if (bars > 15) bars = 15;
        Serial.printf("  %6.1f dBFS %s\r", (double)db, "###############" + bars);
    }
}
