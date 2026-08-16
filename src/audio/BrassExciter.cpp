#include "audio/BrassExciter.h"

#include <cmath>

namespace ot {

void BrassExciter::configure(const ExciterConfig& cfg, uint32_t sampleRate) {
    cfg_ = cfg;
    sampleRate_ = sampleRate ? sampleRate : 48000;
    osc_.setSampleRate(sampleRate_);
    // One-pole rise towards the target pressure. 3 time constants inside
    // transientMs, so the attack is essentially finished when it says it is.
    const float samples = clampValue(cfg_.transientMs, 0.5f, 500.0f) * 0.001f *
                          static_cast<float>(sampleRate_);
    pressureCoef_ = std::exp(-3.0f / (samples < 1.0f ? 1.0f : samples));
    pressure_ = 0.0f;
    // tanh(x)/tanh(drive) keeps the output near unity whatever the drive, so
    // turning the drive up changes the timbre and not the level. The limiter
    // should never be the thing that decides how brassy the instrument is.
    const float d = clampValue(cfg_.drive, 0.1f, 12.0f);
    normalisation_ = 1.0f / std::tanh(d);
}

void BrassExciter::setFrequency(float hz) { osc_.setFrequency(hz); }

void BrassExciter::noteOn() { pressure_ = 0.0f; }

void BrassExciter::resetPhase() { osc_.resetPhase(); }

float BrassExciter::process(float noise) {
    // Pressure rises towards the static setting plus whatever the player is
    // adding through velocity, breath and expression.
    const float target = clampValue(cfg_.pressure + blow_ * (1.0f - cfg_.pressure), 0.0f, 1.0f);
    pressure_ = target + (pressure_ - target) * pressureCoef_;

    // Air noise goes in BEFORE the shaper: shaped noise reads as breath moving
    // through the instrument, noise added afterwards reads as hiss on top of a
    // synthesiser.
    float x = osc_.next() + noise * cfg_.noiseAmount;

    // Asymmetry: biasing the shaper is what creates even harmonics. A
    // symmetric transfer function only ever gives odd ones, which is a
    // clarinet, not a trumpet.
    x += cfg_.asymmetry * 0.5f * pressure_;

    const float drive = clampValue(cfg_.drive * (1.0f + cfg_.pressureToDrive * pressure_),
                                   0.1f, 12.0f);
    float y = std::tanh(x * drive) * normalisation_;

    // Remove the DC the bias introduced, otherwise every note starts with an
    // offset and the DC blocker downstream has to clean up after us.
    y -= std::tanh(cfg_.asymmetry * 0.5f * pressure_ * drive) * normalisation_;
    return y * pressure_;
}

}  // namespace ot
