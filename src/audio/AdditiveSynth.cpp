#include "audio/AdditiveSynth.h"

#include <cmath>

namespace ot {

void AdditiveSynth::configure(const AdditiveConfig& cfg, uint32_t sampleRate) {
    cfg_ = cfg;
    if (cfg_.harmonicCount == 0) cfg_.harmonicCount = 1;
    if (cfg_.harmonicCount > kMaxHarmonics) cfg_.harmonicCount = kMaxHarmonics;
    sampleRate_ = sampleRate ? sampleRate : 48000;
    initSineTable();
    dirty_ = true;
}

void AdditiveSynth::resetPhase() {
    for (uint8_t i = 0; i < kMaxHarmonics; ++i) phase_[i] = 0;
}

void AdditiveSynth::setFrequency(float hz) {
    if (hz == frequency_) return;
    frequency_ = hz;
    dirty_ = true;
}

void AdditiveSynth::updateGains() {
    dirty_ = false;
    active_ = 0;
    normalisation_ = 0.0f;

    if (frequency_ <= 0.0f) return;

    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
    // A single "blow" parameter drives the harmonic tilt.  0 -> soft, dark;
    // 1 -> loud, brilliant.
    const float blow = clampValue(brightness_ +
                                      cfg_.pitchBrightness * (pitchNorm_ - 0.5f) * 0.5f,
                                  0.0f, 1.0f);
    // Roll-off exponent: strong tilt when soft, nearly flat when loud.
    const float tilt = 2.6f - 2.0f * blow;

    for (uint8_t i = 0; i < cfg_.harmonicCount; ++i) {
        const float harmonicNumber = static_cast<float>(i + 1);
        const float f = frequency_ * harmonicNumber;
        if (f >= nyquist * 0.95f) {
            gain_[i] = 0.0f;
            increment_[i] = 0;
            continue;
        }
        float g = cfg_.harmonicGain[i] * std::pow(harmonicNumber, -tilt + 1.0f);
        // Gentle fade of the very last audible partials to avoid a hard edge
        // when a harmonic crosses Nyquist while the note glides.
        const float headroom = (nyquist * 0.95f - f) / (nyquist * 0.15f);
        if (headroom < 1.0f) g *= clampValue(headroom, 0.0f, 1.0f);

        gain_[i] = g;
        normalisation_ += g;
        increment_[i] = static_cast<uint32_t>((f / static_cast<float>(sampleRate_)) *
                                              4294967296.0f);
        active_ = static_cast<uint8_t>(i + 1);
    }

    normalisation_ = normalisation_ > 0.0001f ? 1.0f / normalisation_ : 0.0f;
}

float AdditiveSynth::process() {
    if (dirty_) updateGains();
    float sum = 0.0f;
    for (uint8_t i = 0; i < active_; ++i) {
        if (increment_[i] == 0) continue;
        sum += gain_[i] * sineFromPhase(phase_[i]);
        phase_[i] += increment_[i];
    }
    return sum * normalisation_;
}

}  // namespace ot
