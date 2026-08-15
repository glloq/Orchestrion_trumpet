#include "audio/backends/Esp32Dac.h"

#include "diagnostics/Logger.h"

namespace ot {

Esp32DacBackend::~Esp32DacBackend() { Esp32DacBackend::end(); }

void Esp32DacBackend::configure(const AudioConfig& cfg) { cfg_ = cfg; }

bool Esp32DacBackend::begin() {
#if defined(OT_HAS_INTERNAL_DAC)
    dac_continuous_config_t dacCfg = {
        .chan_mask = cfg_.internalDacChannel == 2 ? DAC_CHANNEL_MASK_CH1 : DAC_CHANNEL_MASK_CH0,
        .desc_num = cfg_.dmaBuffers ? cfg_.dmaBuffers : 6,
        .buf_size = static_cast<size_t>(cfg_.blockSize ? cfg_.blockSize : 128),
        .freq_hz = cfg_.sampleRate,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    if (dac_continuous_new_channels(&dacCfg, &handle_) != ESP_OK) {
        copyString(error_, sizeof(error_), "dac_continuous_new_channels failed");
        OT_LOGE("audio", "%s", error_);
        return false;
    }
    if (dac_continuous_enable(handle_) != ESP_OK) {
        copyString(error_, sizeof(error_), "dac_continuous_enable failed");
        return false;
    }
    running_ = true;
    muted_ = true;
    return true;
#else
    copyString(error_, sizeof(error_), "this chip has no internal DAC");
    return false;
#endif
}

void Esp32DacBackend::end() {
#if defined(OT_HAS_INTERNAL_DAC)
    if (handle_) {
        dac_continuous_disable(handle_);
        dac_continuous_del_channels(handle_);
        handle_ = nullptr;
    }
#endif
    running_ = false;
}

void Esp32DacBackend::writeSamples(const int16_t* buffer, size_t count) {
    if (!running_ || !buffer || count == 0) return;
#if defined(OT_HAS_INTERNAL_DAC)
    // The DAC is unsigned 8 bit: shift the signed samples into 0..255.
    uint8_t tmp[128];
    size_t offset = 0;
    while (offset < count) {
        const size_t chunk = (count - offset) > sizeof(tmp) ? sizeof(tmp) : (count - offset);
        for (size_t i = 0; i < chunk; ++i) {
            const int16_t s = muted_ ? 0 : buffer[offset + i];
            tmp[i] = static_cast<uint8_t>((s >> 8) + 128);
        }
        size_t written = 0;
        if (dac_continuous_write(handle_, tmp, chunk, &written, 40) != ESP_OK ||
            written != chunk) {
            ++underruns_;
        }
        offset += chunk;
    }
#else
    (void)buffer;
    (void)count;
#endif
}

}  // namespace ot
