// ============================================================
//  Speaker output implementation - lazy load, ring buffer, I2S
//  See audio_output.h for the design.
// ============================================================
#include "audio_output.h"
#include "log.h"
#include <driver/i2s.h>

static const char* TAG = "SPK";

AudioOutput speaker;

// How much we hand I2S per call. 32 bit frames, mono slot: 512 samples
// is ~21 ms of audio at 24 kHz, so the loop stays responsive.
static const size_t kWriteSamples = 512;

namespace {
inline uint8_t enableLevel(bool on) {
#if SPK_ENABLE_ACTIVE_HIGH
    return on ? HIGH : LOW;
#else
    return on ? LOW : HIGH;
#endif
}
}  // namespace

// ------------------------------------------------------------
//  bring() - validation only. No RAM, no I2S, no side effects.
// ------------------------------------------------------------
bool AudioOutput::begin() {
#if !WALLE_ENABLE_AUDIO_OUT
    LOGW(TAG, "Disabled in config.h (WALLE_ENABLE_AUDIO_OUT=0)");
    return false;
#else
    if (PIN_IS_UNSET(SPK_I2S_BCLK_PIN) || PIN_IS_UNSET(SPK_I2S_LRCK_PIN) ||
        PIN_IS_UNSET(SPK_I2S_DOUT_PIN)) {
        LOGW(TAG, "I2S pins not configured in include/config.h - speaker disabled");
        LOGW(TAG, "Set SPK_I2S_BCLK_PIN / SPK_I2S_LRCK_PIN / SPK_I2S_DOUT_PIN");
        return false;
    }

    _rate = TTS_SAMPLE_RATE;
    _vol  = SPK_VOLUME > 10 ? 10 : SPK_VOLUME;
    _ready = true;

    // Deliberately nothing else happens here. With SPK_LAZY_LOAD the
    // driver and the ring buffer come up on the first real sentence.
    LOGI(TAG, "Pins OK (%d/%d/%d, %u Hz, volume %u/10) - audio stack loads on demand",
         SPK_I2S_BCLK_PIN, SPK_I2S_LRCK_PIN, SPK_I2S_DOUT_PIN, _rate, _vol);
    return true;
#endif
}

// ------------------------------------------------------------
//  ensureHardware() - the only place I2S and the ring come up.
// ------------------------------------------------------------
bool AudioOutput::ensureHardware() {
    if (!_ready) return false;
    if (_loaded) return true;

#if SPK_LAZY_LOAD
    LOGI(TAG, "Loading audio stack...");
#else
    LOGI(TAG, "Starting I2S...");
#endif

    if (!PIN_IS_UNSET(SPK_ENABLE_PIN)) {
        pinMode(SPK_ENABLE_PIN, OUTPUT);
        digitalWrite(SPK_ENABLE_PIN, enableLevel(true));
    }

    const i2s_port_t port = (i2s_port_t)SPK_I2S_PORT;

    i2s_config_t cfg = {};
    cfg.mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.channel_format      = I2S_CHANNEL_FMT_ALL_RIGHT;   // mono, right aligned
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags    = 0;
    cfg.dma_buf_count       = 6;
    cfg.dma_buf_len         = 320;      // ~10 ms per DMA buffer
    cfg.tx_desc_auto_clear  = true;     // silence instead of noise on underrun
    cfg.fixed_mclk          = 0;
    cfg.bits_per_chan       = (i2s_bits_per_chan_t)SPK_I2S_DATA_BITS;

    if (i2s_driver_install(port, &cfg, 0, NULL) != ESP_OK) {
        LOGE(TAG, "i2s_driver_install failed");
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = SPK_I2S_BCLK_PIN;
    pins.ws_io_num    = SPK_I2S_LRCK_PIN;
    pins.data_out_num = SPK_I2S_DOUT_PIN;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(port, &pins) != ESP_OK) {
        LOGE(TAG, "i2s_set_pin failed");
        i2s_driver_uninstall(port);
        return false;
    }

    i2s_set_sample_rates(port, _rate);
    i2s_zero_dma_buffer(port);

    _ring = (uint8_t*)malloc(SPK_RING_BYTES);
    if (!_ring) {
        LOGE(TAG, "Cannot allocate %d byte ring buffer", SPK_RING_BYTES);
        i2s_driver_uninstall(port);
        return false;
    }
    _cap = SPK_RING_BYTES;
    _loaded = true;
    _lastActiveMs = millis();

    LOGI(TAG, "Audio ready (I2S%d, %u Hz, %d kB ring)",
         SPK_I2S_PORT, _rate, (int)(_cap / 1024));
    return true;
}

// ------------------------------------------------------------
//  releaseHardware() - give the RAM and the DMA buffers back.
// ------------------------------------------------------------
void AudioOutput::releaseHardware() {
    if (!_loaded) return;

    // Silence before tearing down: an uninstalled I2S driver that was
    // mid-frame would click.
    i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
    i2s_driver_uninstall((i2s_port_t)SPK_I2S_PORT);

    if (_ring) { free(_ring); _ring = nullptr; }
    _cap = 0;
    resetRing();
    _loaded = false;

    // Drop the amplifier too: not just muted, unpowered.
    if (!PIN_IS_UNSET(SPK_ENABLE_PIN)) digitalWrite(SPK_ENABLE_PIN, enableLevel(false));

    LOGI(TAG, "Audio stack released (idle)");
}

void AudioOutput::resetRing() {
    _head = _tail = 0;
    _total = _played = 0;
}

// Gain is applied in place as the bytes are copied into the ring, so
// the samples the driver eventually sees are the scaled ones.
void AudioOutput::applyGain(uint8_t* dst, const uint8_t* src, size_t bytes) {
    if (_vol >= 10) { if (dst != src) memcpy(dst, src, bytes); return; }

    const size_t n = bytes / 2;
    const int16_t gain = (int16_t)((int32_t)127 * (_vol + 1) / 11);
    int16_t* d = (int16_t*)(uintptr_t)dst;
    const int16_t* s = (const int16_t*)(const uintptr_t)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = (int16_t)((int32_t)s[i] * gain >> 7);
    }
    // Any odd trailing byte is copied untouched so nothing is lost.
    if (bytes & 1) dst[bytes - 1] = src[bytes - 1];
}

// Pulls up to kWriteSamples out of the ring and into I2S.
// Returns false only on a driver error.
bool AudioOutput::writeToI2s() {
    if (!_loaded) return true;

    const size_t avail = ringUsed();
    if (avail < 2) return true;

    // Never play past what has actually been fed, and never more than
    // SPK_PREFETCH_MS ahead of real time. The second limit is what
    // stops feed() from dumping the entire download into I2S and
    // exhausting the DMA descriptors.
    if (_played >= _total) return true;

    const size_t bytesPerMs = (_rate * 2) / 1000;
    const size_t elapsedMs  = millis() - _startMs;
    const size_t allowed    = (size_t)(elapsedMs + SPK_PREFETCH_MS) * bytesPerMs;
    if (_played >= allowed) return true;

    size_t want = kWriteSamples * 2;
    if (want > avail) want = avail;
    if (want > _total - _played) want = _total - _played;
    want &= ~1u;
    if (want < 2) return true;

    // The ring is not guaranteed contiguous, so feed I2S in up to two
    // slices rather than copying through a scratch buffer.
    const size_t first  = min(want, _cap - _head);
    const size_t second = want - first;

    size_t done = 0;
    esp_err_t err = i2s_write((i2s_port_t)SPK_I2S_PORT, _ring + _head, first, &done, 0);
    if (err != ESP_OK) { LOGE(TAG, "i2s_write error %d", err); return false; }
    _head = (_head + done) % _cap;
    _played += done;

    if (second) {
        done = 0;
        err = i2s_write((i2s_port_t)SPK_I2S_PORT, _ring + _head, second, &done, 0);
        if (err != ESP_OK) { LOGE(TAG, "i2s_write error %d", err); return false; }
        _head = (_head + done) % _cap;
        _played += done;
    }
    return true;
}

bool AudioOutput::beginStream(uint32_t sampleRate) {
    if (!_ready) return false;

    // This is the "load on demand" moment.
    if (!ensureHardware()) return false;

    if (_rate != sampleRate) {
        _rate = sampleRate;
        i2s_set_sample_rates((i2s_port_t)SPK_I2S_PORT, _rate);
    }
    i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
    resetRing();
    _streaming = true;
    _playing = true;
    _startMs = millis();
    _lastActiveMs = _startMs;
    return true;
}

void AudioOutput::feed(const uint8_t* pcm, size_t bytes) {
    if (!_loaded || !_streaming || !pcm || bytes == 0) return;

    size_t done = 0;
    while (done < bytes) {
        // Make room: drain I2S while it is ahead of real time.
        if (ringFree() == 0) {
            if (!writeToI2s()) { stop(); return; }
            if (ringFree() == 0) { delay(1); continue; }   // ring full, wait
        }

        const size_t room  = ringFree();
        const size_t first = min(room, _cap - _tail);
        const size_t chunk = min(first, bytes - done);

        applyGain(_ring + _tail, pcm + done, chunk);
        _tail = (_tail + chunk) % _cap;
        done  += chunk;
        _total += chunk;         // grows as the download streams in

        // Opportunistically push along so the ring never grows stale.
        writeToI2s();
    }
}

void AudioOutput::endStream() {
    if (!_streaming) return;
    _streaming = false;
    _lastActiveMs = millis();
    LOGI(TAG, "Stream closed, %u bytes queued", (unsigned)_total);
}

void AudioOutput::pump() {
    if (!_loaded) return;

    if (_playing) {
        writeToI2s();

        // Finished once everything fed has been handed to the driver.
        // The DMA still has ~60 ms buffered, which is what the delay
        // covers.
        if (!_streaming && _played >= _total) {
            delay(80);
            _playing = false;
            resetRing();
            _lastActiveMs = millis();
            LOGI(TAG, "Playback complete");
        }
        return;
    }

    // Idle: this is where "loaded only when needed" actually pays off.
#if SPK_LAZY_LOAD
    if (millis() - _lastActiveMs >= SPK_RELEASE_IDLE_MS) releaseHardware();
#endif
}

bool AudioOutput::queue(uint8_t* pcm, size_t bytes) {
    if (!_ready || !pcm || bytes == 0) { if (pcm) free(pcm); return false; }
    if (_playing) { LOGW(TAG, "Still busy, dropping queued audio"); free(pcm); return false; }

    if (!beginStream(TTS_SAMPLE_RATE)) { free(pcm); return false; }
    feed(pcm, bytes);     // accumulates _total for us
    endStream();
    free(pcm);            // queue() owns the caller's buffer
    return true;
}

void AudioOutput::stop() {
    _streaming = false;
    _playing = false;
    if (_loaded) {
        i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
    }
    resetRing();
    _lastActiveMs = millis();
}

void AudioOutput::setVolume(uint8_t v) { _vol = v > 10 ? 10 : v; }