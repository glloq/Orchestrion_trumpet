// ============================================================================
//  Limiter.h - loudspeaker protection.
//
//  Two independent layers, as required by docs/AUDIO.md:
//    1. a soft limiter with attack/release, working on the configured
//       threshold, which is what normally shapes the signal;
//    2. a hard ceiling that clamps every single sample, so even a bug in the
//       synthesis stage cannot send a full scale square wave to the amplifier.
//
//  The maximum safe level itself is computed by SpeakerProtection from the
//  speaker and amplifier profiles, never from a magic constant.
// ============================================================================
#pragma once

#include "audio/Profiles.h"
#include "config/ConfigTypes.h"

namespace ot {

// Pure maths, unit tested on the host.
class SpeakerProtection {
public:
    // Peak scale factor (0..1) that keeps the speaker inside its power limit
    // for the given amplifier.  1.0 means the amplifier cannot overdrive it.
    static float safePeakScale(const ISpeakerProfile& speaker, const IAmplifierProfile& amp);
    static float safePeakScale(const SpeakerConfig& speaker, const AmplifierConfig& amp);

    // Effective high pass corner: the strictest of the speaker's own
    // recommendation, the acoustic coupling and the user setting.
    static float effectiveHighPassHz(float configured, float speakerRecommended,
                                     float acoustic);
};

class Limiter {
public:
    void configure(const LimiterConfig& cfg, uint32_t sampleRate);
    // Ceiling handed over by the protection stage, 0..1.
    void setPeakScale(float scale) { peakScale_ = clampValue(scale, 0.02f, 1.0f); }
    float peakScale() const { return peakScale_; }
    void reset();

    float process(float x);

    float gainReduction() const { return gain_; }
    uint32_t clipEvents() const { return clipEvents_; }
    void resetStats() { clipEvents_ = 0; }

private:
    LimiterConfig cfg_;
    float threshold_ = 0.7f;
    float ceiling_ = 0.985f;
    float peakScale_ = 1.0f;
    float attackCoef_ = 0.0f;
    float releaseCoef_ = 0.0f;
    float envelope_ = 0.0f;
    float gain_ = 1.0f;
    uint32_t clipEvents_ = 0;
};

}  // namespace ot
