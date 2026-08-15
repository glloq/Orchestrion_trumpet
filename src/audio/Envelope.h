// ============================================================================
//  Envelope.h - ADSR plus the two noise components that make a synthetic
//  trumpet sound like it is being blown rather than generated.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

class Envelope {
public:
    enum class Stage : uint8_t { Idle = 0, Attack, Decay, Sustain, Release };

    void configure(const EnvelopeConfig& cfg, uint32_t sampleRate);

    void noteOn(bool retrigger);
    void noteOff();
    void reset();

    // One sample of the amplitude envelope, 0..1.
    float process();

    Stage stage() const { return stage_; }
    bool isActive() const { return stage_ != Stage::Idle; }
    float value() const { return value_; }
    // 1.0 at the very start of a note, decaying over the attack: drives the
    // amount of attack "chiff" noise mixed in.
    float attackTransient() const { return transient_; }

private:
    EnvelopeConfig cfg_;
    uint32_t sampleRate_ = 48000;
    Stage stage_ = Stage::Idle;
    float value_ = 0.0f;
    float attackStep_ = 1.0f;
    float decayCoef_ = 0.0f;
    float releaseCoef_ = 0.0f;
    float transient_ = 0.0f;
    float transientCoef_ = 0.0f;
};

}  // namespace ot
