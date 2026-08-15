// ============================================================================
//  NullBackend.h - "no audio output".
//
//  Used by SAFE MODE, by a valve-only instrument and whenever the configured
//  backend refuses to start.  The audio task keeps running at the right pace
//  (it paces itself on the clock) so the rest of the firmware behaves exactly
//  the same, it simply produces no sound.
// ============================================================================
#pragma once

#include "audio/IAudioBackend.h"

namespace ot {

class NullBackend final : public IAudioBackend {
public:
    void configure(const AudioConfig& cfg) override { cfg_ = cfg; }
    bool begin() override {
        running_ = true;
        return true;
    }
    void end() override { running_ = false; }
    void writeSamples(const int16_t* buffer, size_t count) override;
    void mute(bool state) override { muted_ = state; }
    bool isMuted() const override { return muted_; }

    const char* name() const override { return "NONE"; }
    uint32_t sampleRate() const override { return cfg_.sampleRate; }
    uint8_t bitDepth() const override { return 16; }
    bool isRunning() const override { return running_; }
    BackendMaturity maturity() const override { return BackendMaturity::STABLE; }

private:
    AudioConfig cfg_;
    bool muted_ = true;
    bool running_ = false;
    uint32_t lastMicros_ = 0;
};

}  // namespace ot
