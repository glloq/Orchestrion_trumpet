// ============================================================================
//  Esp32Dac.h - the 8 bit DAC built into the classic ESP32 (GPIO25 / GPIO26).
//
//  Prototype quality only: 8 bits and a noisy analogue supply.  It exists so a
//  bare ESP32-WROOM board can make a sound before any external hardware is
//  wired, and it is explicitly reported as PROTOTYPE to the web UI.
//
//  The ESP32-S3 has no DAC at all, so the backend is compiled out there and
//  BoardCaps hides the option.
// ============================================================================
#pragma once

#include "audio/IAudioBackend.h"

#if !defined(OT_HOST_BUILD)
#include <soc/soc_caps.h>
#if SOC_DAC_SUPPORTED
#define OT_HAS_INTERNAL_DAC 1
#include <driver/dac_continuous.h>
#endif
#endif

namespace ot {

class Esp32DacBackend final : public IAudioBackend {
public:
    ~Esp32DacBackend() override;

    void configure(const AudioConfig& cfg) override;
    bool begin() override;
    void end() override;
    void writeSamples(const int16_t* buffer, size_t count) override;
    void mute(bool state) override { muted_ = state; }
    bool isMuted() const override { return muted_; }

    const char* name() const override { return "ESP32_INTERNAL_DAC"; }
    uint32_t sampleRate() const override { return cfg_.sampleRate; }
    uint8_t bitDepth() const override { return 8; }
    bool isRunning() const override { return running_; }
    uint32_t underruns() const override { return underruns_; }
    void resetUnderruns() override { underruns_ = 0; }
    BackendMaturity maturity() const override { return BackendMaturity::PROTOTYPE; }
    const char* lastError() const override { return error_[0] ? error_ : nullptr; }

private:
    AudioConfig cfg_;
    bool muted_ = true;
    bool running_ = false;
    uint32_t underruns_ = 0;
    char error_[64] = "";
#if defined(OT_HAS_INTERNAL_DAC)
    dac_continuous_handle_t handle_ = nullptr;
#endif
};

}  // namespace ot
