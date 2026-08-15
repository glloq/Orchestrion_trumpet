#include "audio/Envelope.h"

#include <cmath>

namespace ot {

namespace {
// One-pole coefficient reaching ~99.3% of the target after `ms`.
float coefFor(float ms, uint32_t sampleRate) {
    if (ms <= 0.05f) return 0.0f;
    const float samples = (ms * 0.001f) * static_cast<float>(sampleRate);
    return std::exp(-5.0f / samples);
}
}  // namespace

void Envelope::configure(const EnvelopeConfig& cfg, uint32_t sampleRate) {
    cfg_ = cfg;
    sampleRate_ = sampleRate ? sampleRate : 48000;

    const float attackSamples =
        (cfg_.attackMs <= 0.0f ? 1.0f : cfg_.attackMs * 0.001f * static_cast<float>(sampleRate_));
    attackStep_ = 1.0f / (attackSamples < 1.0f ? 1.0f : attackSamples);
    decayCoef_ = coefFor(cfg_.decayMs, sampleRate_);
    releaseCoef_ = coefFor(cfg_.releaseMs, sampleRate_);
    // The chiff lasts roughly as long as the attack plus a short tail.
    transientCoef_ = coefFor(cfg_.attackMs + 25.0f, sampleRate_);
}

void Envelope::noteOn(bool retrigger) {
    if (retrigger || stage_ == Stage::Idle) {
        value_ = retrigger ? 0.0f : value_;
        transient_ = 1.0f;
    }
    stage_ = Stage::Attack;
}

void Envelope::noteOff() {
    if (stage_ != Stage::Idle) stage_ = Stage::Release;
}

void Envelope::reset() {
    stage_ = Stage::Idle;
    value_ = 0.0f;
    transient_ = 0.0f;
}

float Envelope::process() {
    switch (stage_) {
        case Stage::Attack:
            value_ += attackStep_;
            if (value_ >= 1.0f) {
                value_ = 1.0f;
                stage_ = Stage::Decay;
            }
            break;

        case Stage::Decay: {
            const float target = clampValue(cfg_.sustain, 0.0f, 1.0f);
            value_ = target + (value_ - target) * decayCoef_;
            if (value_ - target < 0.001f) {
                value_ = target;
                stage_ = Stage::Sustain;
            }
            break;
        }

        case Stage::Sustain:
            value_ = clampValue(cfg_.sustain, 0.0f, 1.0f);
            break;

        case Stage::Release:
            value_ *= releaseCoef_;
            if (value_ < 0.0002f) {
                value_ = 0.0f;
                stage_ = Stage::Idle;
            }
            break;

        case Stage::Idle:
        default:
            value_ = 0.0f;
            break;
    }

    transient_ *= transientCoef_;
    return value_;
}

}  // namespace ot
