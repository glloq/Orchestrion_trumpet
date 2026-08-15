// ============================================================================
//  AdditiveSynth.h - the reference trumpet generator.
//
//  A brass instrument is essentially a harmonic series whose upper partials
//  grow with the blowing pressure.  Here the "pressure" is the combination of
//  velocity, CC2 (breath) and CC11 (expression), plus a small contribution
//  from the pitch itself, exactly as described in docs/AUDIO.md.
//
//  Partials above Nyquist are simply not summed, so the engine never aliases
//  when playing the top of the range.
// ============================================================================
#pragma once

#include "audio/Oscillator.h"
#include "config/ConfigTypes.h"

namespace ot {

class AdditiveSynth {
public:
    void configure(const AdditiveConfig& cfg, uint32_t sampleRate);

    void setFrequency(float hz);
    // 0..1, how "hard" the instrument is being blown.  Recomputing the
    // harmonic gains costs a pow() per partial, so it only happens when the
    // value moved enough to be audible - never once per sample.
    void setBrightness(float brightness) {
        const float v = clampValue(brightness, 0.0f, 1.0f);
        if (v > brightness_ + 0.004f || v < brightness_ - 0.004f) dirty_ = true;
        brightness_ = v;
    }
    void setPitchNormalised(float p) {
        const float v = clampValue(p, 0.0f, 1.0f);
        if (v > pitchNorm_ + 0.01f || v < pitchNorm_ - 0.01f) dirty_ = true;
        pitchNorm_ = v;
    }
    void resetPhase();

    float process();

    uint8_t activeHarmonics() const { return active_; }

private:
    void updateGains();

    AdditiveConfig cfg_;
    uint32_t sampleRate_ = 48000;
    float frequency_ = 0.0f;
    float brightness_ = 0.5f;
    float pitchNorm_ = 0.5f;
    uint32_t phase_[kMaxHarmonics] = {0};
    uint32_t increment_[kMaxHarmonics] = {0};
    float gain_[kMaxHarmonics] = {0.0f};
    float normalisation_ = 1.0f;
    uint8_t active_ = 0;
    bool dirty_ = true;
};

}  // namespace ot
