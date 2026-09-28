#include "audio_output.h"
#include "log.h"
#include <driver/i2s.h>

static const char* TAG = "SPK";

AudioOutput speaker;

void AudioOutput::freeBuffer() {
    if (_pcm) { free(_pcm); _pcm = nullptr; }
    _total = _prepared = _sent = _chunk = 0;
    _playing = false;
}

bool AudioOutput::begin() {
#if !WALLE_ENABLE_AUDIO_OUT
    LOGW(TAG, "Disabled in config.h");
    return false;
#else
    if (PIN_IS_UNSET(SPK_I2S_BCLK_PIN) || PIN_IS_UNSET(SPK_I2S_LRCK_PIN) ||
        PIN_IS_UNSET(SPK_I2S_DOUT_PIN)) {
        LOGW(TAG, "I2S pins not configured in include/config.h - speaker disabled");
        return false;
    }

    const i2s_port_t port = (i2s_port_t)SPK_I2S_PORT;

    i2s_config_t cfg = {};
    cfg.mode               = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.channel_format     = I2S_CHANNEL_FMT_ALL_RIGHT;   // mono, right aligned
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags   = 0;
    cfg.dma_buf_count      = 6;
    cfg.dma_buf_len        = 320;      // 10 ms per DMA buffer
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk         = 0;
    cfg.bits_per_chan      = (i2s_bits_per_chan_t)SPK_I2S_DATA_BITS;

    if (i2s_driver_install(port, &cfg, 0, NULL) != ESP_OK) {
        LOGE(TAG, "i2s_driver_install failed");
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = SPK_I2S_BCLK_PIN;
    pins.ws_io_num    = SPK_I2S_LRCK_PIN;
    pins.data_out_num = SPK_I2S_DOUT_PIN;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    i2s_set_pin(port, &pins);
    i2s_set_sample_rates(port, AUDIO_SAMPLE_RATE);
    i2s_zero_dma_buffer(port);

    _chunk = 1024;                     // samples written per pump() call
    _vol = SPK_VOLUME > 10 ? 10 : SPK_VOLUME;
    _ready = true;
    LOGI(TAG, "Ready (I2S%d, %d Hz, volume %u/10)", SPK_I2S_PORT, AUDIO_SAMPLE_RATE, _vol);
    return true;
#endif
}

void AudioOutput::setVolume(uint8_t v) { _vol = v > 10 ? 10 : v; }

bool AudioOutput::queue(uint8_t* pcm, size_t bytes) {
    if (!_ready || !pcm || bytes == 0) { if (pcm) free(pcm); return false; }
    if (_playing) { LOGW(TAG, "Still busy, dropping new audio"); free(pcm); return false; }

    _pcm = pcm;
    _total = bytes;
    _prepared = 0;
    _sent = 0;
    _startMs = millis();
    _playing = true;
    LOGI(TAG, "Playing %u bytes (%.1f s)", (unsigned)bytes, bytes / (float)(AUDIO_SAMPLE_RATE * 2));
    return true;
}

void AudioOutput::pump() {
    if (!_playing || !_pcm) return;
    const uint32_t now = millis();

    // ---- self-pacing ----
    // The driver has no "how full is the DMA" getter in this IDF version, so
    // pace against real time instead: never queue more than ~40 ms of audio
    // ahead of the speaker. That keeps the main loop responsive and the audio
    // smooth without ever blocking.
    const size_t bytesPerMs = (AUDIO_SAMPLE_RATE * 2) / 1000;   // 32
    const size_t elapsedMs = now - _startMs;
    const size_t dueSent = (elapsedMs + 40) * bytesPerMs;
    if (dueSent < _sent) return;              // already ahead of real time
    const size_t allowed = (dueSent < _total ? dueSent : _total);

    if (_sent >= allowed) return;

    // ---- scale the next window (only once) ----
    if (_prepared < _sent) {
        size_t want = (_chunk * 2);
        const size_t limit = allowed - _prepared;
        if (want > limit) want = limit;
        want &= ~1u;                          // keep 16-bit samples aligned
        int16_t* w = (int16_t*)(_pcm + _prepared);
        const size_t n = want / 2;
        const int16_t gain = (int16_t)((int32_t)127 * (_vol + 1) / 11);
        for (size_t i = 0; i < n; i++) w[i] = (int16_t)((int32_t)w[i] * gain >> 7);
        _prepared += want;
    }

    const size_t unsent = _prepared - _sent;
    if (unsent == 0) return;

    size_t written = 0;
    esp_err_t err = i2s_write((i2s_port_t)SPK_I2S_PORT, _pcm + _sent, unsent,
                              &written, 0);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_write error %d - stopping playback", err);
        freeBuffer();
        return;
    }
    _sent += written;

    if (_sent >= _total) {
        // let the DMA buffer drain before releasing the memory
        delay(120);
        LOGI(TAG, "Playback complete");
        freeBuffer();
    }
}

void AudioOutput::stop() {
    if (_ready) i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
    freeBuffer();
}
