#include "audio/backends/I2sBackendBase.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#endif

namespace ot {

I2sBackendBase::~I2sBackendBase() { I2sBackendBase::end(); }

void I2sBackendBase::setError(const char* text) {
    copyString(error_, sizeof(error_), text);
    if (text) OT_LOGE("audio", "%s", text);
}

void I2sBackendBase::configure(const AudioConfig& cfg) {
    cfg_ = cfg;
    uint8_t requested = cfg.bitDepth;
    if (requested != 16 && requested != 24 && requested != 32) requested = 16;
    const uint8_t maximum = maximumBitDepth();
    bitDepth_ = requested > maximum ? maximum : requested;
}

bool I2sBackendBase::installI2s() {
#if defined(OT_HOST_BUILD)
    return true;
#else
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chanCfg.dma_desc_num = cfg_.dmaBuffers ? cfg_.dmaBuffers : 6;
    chanCfg.dma_frame_num = cfg_.blockSize ? cfg_.blockSize : 128;
    chanCfg.auto_clear = true;   // send silence instead of stale data on underrun

    const bool needRx = supportsCapture() && cfg_.i2s.din >= 0;
    esp_err_t err = i2s_new_channel(&chanCfg, &tx_, needRx ? &rx_ : nullptr);
    if (err != ESP_OK) {
        setError("i2s_new_channel failed");
        return false;
    }

    // A 24 bit request is carried in 32 bit slots with the sample left
    // aligned: every DAC in the catalogue accepts that framing and it avoids
    // the 3-byte alignment rules of the native 24 bit slot mode.
    i2s_data_bit_width_t width =
        bitDepth_ >= 24 ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT;

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg_.sampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = cfg_.i2s.mclk >= 0 ? static_cast<gpio_num_t>(cfg_.i2s.mclk) : I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(cfg_.i2s.bclk),
                .ws = static_cast<gpio_num_t>(cfg_.i2s.ws),
                .dout = static_cast<gpio_num_t>(cfg_.i2s.dout),
                .din = needRx ? static_cast<gpio_num_t>(cfg_.i2s.din) : I2S_GPIO_UNUSED,
                .invert_flags = {false, false, false},
            },
    };
    if (cfg_.i2s.mclk >= 0) {
        stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    }
    // The instrument is mono; feeding the left slot only keeps the DMA traffic
    // and the DSP cost halved.
    stdCfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    err = i2s_channel_init_std_mode(tx_, &stdCfg);
    if (err != ESP_OK) {
        setError("i2s_channel_init_std_mode failed (check the I2S pins)");
        return false;
    }
    if (needRx && rx_) {
        err = i2s_channel_init_std_mode(rx_, &stdCfg);
        if (err != ESP_OK) OT_LOGW("audio", "I2S RX init failed, capture disabled");
    }
    return true;
#endif
}

bool I2sBackendBase::begin() {
    error_[0] = '\0';
    if (!preparePeripheral()) return false;
    if (!installI2s()) return false;

#if !defined(OT_HOST_BUILD)
    if (i2s_channel_enable(tx_) != ESP_OK) {
        setError("i2s_channel_enable failed");
        return false;
    }
    if (rx_) i2s_channel_enable(rx_);
#endif

    running_ = true;
    if (!startCodec()) {
        setError("codec initialisation failed");
        running_ = false;
        return false;
    }
    // The instrument always comes up silent: unmuting is the very last step of
    // the boot sequence, once every peripheral has been validated.
    mute(true);
    return true;
}

void I2sBackendBase::end() {
    if (!running_) return;
    mute(true);
    stopCodec();
#if !defined(OT_HOST_BUILD)
    if (tx_) {
        i2s_channel_disable(tx_);
        i2s_del_channel(tx_);
        tx_ = nullptr;
    }
    if (rx_) {
        i2s_channel_disable(rx_);
        i2s_del_channel(rx_);
        rx_ = nullptr;
    }
#endif
    running_ = false;
}

void I2sBackendBase::writeSamples(const int16_t* buffer, size_t count) {
    if (!running_ || !buffer || count == 0) return;
#if !defined(OT_HOST_BUILD)
    size_t written = 0;
    // 40 ms is far longer than any legitimate DMA wait: if we ever reach it
    // the pipeline is broken and we would rather count the event than hang the
    // audio task forever.
    const esp_err_t err = i2s_channel_write(tx_, buffer, count * sizeof(int16_t), &written,
                                            pdMS_TO_TICKS(40));
    if (err != ESP_OK || written != count * sizeof(int16_t)) ++underruns_;
#else
    (void)buffer;
    (void)count;
#endif
}

void I2sBackendBase::writeSamples32(const int32_t* buffer, size_t count) {
    if (!running_ || !buffer || count == 0) return;
    if (bitDepth_ <= 16) {
        // Narrow in place-free chunks so a 16 bit backend still works when the
        // engine renders at full width.
        int16_t tmp[128];
        size_t offset = 0;
        while (offset < count) {
            const size_t chunk = (count - offset) > 128 ? 128 : (count - offset);
            for (size_t i = 0; i < chunk; ++i) {
                tmp[i] = static_cast<int16_t>(buffer[offset + i] >> 16);
            }
            writeSamples(tmp, chunk);
            offset += chunk;
        }
        return;
    }
#if !defined(OT_HOST_BUILD)
    size_t written = 0;
    const esp_err_t err = i2s_channel_write(tx_, buffer, count * sizeof(int32_t), &written,
                                            pdMS_TO_TICKS(40));
    if (err != ESP_OK || written != count * sizeof(int32_t)) ++underruns_;
#endif
}

void I2sBackendBase::mute(bool state) {
    muted_ = state;
    applyMute(state);
}

}  // namespace ot
