#include "audio/Limiter.h"

#include <cmath>

namespace ot {

float SpeakerProtection::safePeakScale(const ISpeakerProfile& speaker,
                                       const IAmplifierProfile& amp) {
    const float z = speaker.impedanceOhm() > 0.5f ? speaker.impedanceOhm() : 8.0f;
    float speakerLimitW = speaker.powerLimitW();
    if (speakerLimitW <= 0.05f) speakerLimitW = speaker.ratedPowerW() * 0.5f;
    if (speakerLimitW <= 0.05f) speakerLimitW = 1.0f;

    const float ampMaxW = amp.maxPowerW();
    if (ampMaxW <= 0.05f) {
        // No amplifier declared (line output straight into a powered speaker):
        // nothing to derive, keep unity and rely on the master volume.
        return clampValue(amp.volumeLimit(), 0.02f, 1.0f);
    }

    // Full scale digital -> amplifier maximum voltage.  The safe voltage is the
    // one that dissipates `speakerLimitW` in the same impedance, so the ratio
    // of the two voltages is the peak scale we may use.
    const float vAmp = std::sqrt(ampMaxW * z);
    const float vSafe = std::sqrt(speakerLimitW * z);
    float scale = vAmp > 0.0001f ? vSafe / vAmp : 1.0f;
    scale = clampValue(scale, 0.02f, 1.0f);
    return clampValue(scale * amp.volumeLimit(), 0.02f, 1.0f);
}

float SpeakerProtection::safePeakScale(const SpeakerConfig& speakerCfg,
                                        const AmplifierConfig& ampCfg) {
    SpeakerProfile speaker;
    AmplifierProfile amp;
    speaker.configure(speakerCfg);
    amp.configure(ampCfg);
    return safePeakScale(speaker, amp);
}

float SpeakerProtection::effectiveHighPassHz(float configured, float speakerRecommended,
                                              float acoustic) {
    float hz = configured;
    if (speakerRecommended > hz) hz = speakerRecommended;
    if (acoustic > hz) hz = acoustic;
    return hz;
}

void Limiter::configure(const LimiterConfig& cfg, uint32_t sampleRate) {
    cfg_ = cfg;
    if (sampleRate == 0) sampleRate = 48000;
    threshold_ = std::pow(10.0f, clampValue(cfg.thresholdDb, -40.0f, 0.0f) / 20.0f);
    ceiling_ = clampValue(cfg.hardCeiling, 0.05f, 1.0f);

    const float attackSamples =
        clampValue(cfg.attackMs, 0.05f, 100.0f) * 0.001f * static_cast<float>(sampleRate);
    const float releaseSamples =
        clampValue(cfg.releaseMs, 1.0f, 2000.0f) * 0.001f * static_cast<float>(sampleRate);
    attackCoef_ = std::exp(-1.0f / attackSamples);
    releaseCoef_ = std::exp(-1.0f / releaseSamples);
    reset();
}

void Limiter::reset() {
    envelope_ = 0.0f;
    gain_ = 1.0f;
}

float Limiter::process(float x) {
    // The peak scale is applied first: everything downstream then works in the
    // "safe for this speaker" domain.
    x *= peakScale_;

    if (cfg_.enabled) {
        const float level = std::fabs(x);
        // Peak follower: instant attack on the way up, slow release.
        if (level > envelope_) {
            envelope_ = level + (envelope_ - level) * attackCoef_;
        } else {
            envelope_ = level + (envelope_ - level) * releaseCoef_;
        }

        const float limit = threshold_ * peakScale_;
        float targetGain = 1.0f;
        if (envelope_ > limit && envelope_ > 0.0001f) targetGain = limit / envelope_;

        // Gain only ever moves through the same smoothing, so no zipper noise.
        if (targetGain < gain_) {
            gain_ = targetGain + (gain_ - targetGain) * attackCoef_;
        } else {
            gain_ = targetGain + (gain_ - targetGain) * releaseCoef_;
        }
        x *= gain_;
    }

    // Emergency clamp: unconditional, whatever the limiter decided.
    const float hard = ceiling_ * peakScale_;
    if (x > hard) {
        x = hard;
        ++clipEvents_;
    } else if (x < -hard) {
        x = -hard;
        ++clipEvents_;
    }
    return x;
}

}  // namespace ot
