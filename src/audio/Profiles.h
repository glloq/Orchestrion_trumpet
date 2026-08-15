// ============================================================================
//  Profiles.h - speaker / amplifier / acoustic coupling abstractions.
//
//  The DAC, the amplifier and the speaker are three independent choices.  The
//  audio engine never asks "am I driving a MAX98357?"; it asks the profiles
//  for a high-pass corner, a voicing EQ and a safe peak level.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

// ---------------------------------------------------------------------------
// ISpeakerProfile - everything the protection and voicing stages need to know
// about the transducer.
// ---------------------------------------------------------------------------
class ISpeakerProfile {
public:
    virtual ~ISpeakerProfile() = default;
    virtual const char* name() const = 0;
    virtual float impedanceOhm() const = 0;
    virtual float ratedPowerW() const = 0;
    virtual float minimumFrequencyHz() const = 0;
    virtual float maximumFrequencyHz() const = 0;
    virtual float recommendedHighPassHz() const = 0;
    virtual float gainCorrectionDb() const = 0;
    virtual float powerLimitW() const = 0;
    // Voicing curve of the driver, applied before the limiter.
    virtual uint8_t eqBandCount() const = 0;
    virtual const EqBand& eqBand(uint8_t index) const = 0;
};

// ---------------------------------------------------------------------------
// IAmplifierProfile - the electrical envelope the DSP must stay inside.
// ---------------------------------------------------------------------------
class IAmplifierProfile {
public:
    virtual ~IAmplifierProfile() = default;
    virtual const char* name() const = 0;
    virtual float maxPowerW() const = 0;
    virtual float gainDb() const = 0;
    virtual float volumeLimit() const = 0;
    virtual bool hasIntegratedSpeakerOutput() const = 0;
};

// ---------------------------------------------------------------------------
// Concrete, table backed implementations built from the configuration.
// ---------------------------------------------------------------------------
class SpeakerProfile final : public ISpeakerProfile {
public:
    void configure(const SpeakerConfig& cfg);

    const char* name() const override { return name_; }
    float impedanceOhm() const override { return impedance_; }
    float ratedPowerW() const override { return ratedPower_; }
    float minimumFrequencyHz() const override { return fMin_; }
    float maximumFrequencyHz() const override { return fMax_; }
    float recommendedHighPassHz() const override { return hpf_; }
    float gainCorrectionDb() const override { return gainCorrection_; }
    float powerLimitW() const override { return powerLimit_; }
    uint8_t eqBandCount() const override { return eqCount_; }
    const EqBand& eqBand(uint8_t index) const override {
        return eq_[index < eqCount_ ? index : 0];
    }

private:
    char name_[kNameLen] = "";
    float impedance_ = 8.0f;
    float ratedPower_ = 10.0f;
    float fMin_ = 150.0f;
    float fMax_ = 20000.0f;
    float hpf_ = 180.0f;
    float gainCorrection_ = 0.0f;
    float powerLimit_ = 8.0f;
    uint8_t eqCount_ = 0;
    EqBand eq_[kMaxEqBands];
};

class AmplifierProfile final : public IAmplifierProfile {
public:
    void configure(const AmplifierConfig& cfg);

    const char* name() const override { return name_; }
    float maxPowerW() const override { return maxPower_; }
    float gainDb() const override { return gain_; }
    float volumeLimit() const override { return volumeLimit_; }
    bool hasIntegratedSpeakerOutput() const override { return integrated_; }

private:
    const char* name_ = "NONE";
    float maxPower_ = 0.0f;
    float gain_ = 0.0f;
    float volumeLimit_ = 1.0f;
    bool integrated_ = false;
};

// Factory defaults for the catalogued drivers.  Selecting a profile in the web
// UI fills the SpeakerConfig with these numbers; the user may then override
// any of them (CUSTOM keeps whatever is already stored).
void applySpeakerProfileDefaults(SpeakerProfileId id, SpeakerConfig& out);
void applyAmplifierDefaults(AmplifierType type, AmplifierConfig& out);

// Extra high-pass / voicing introduced by the acoustic coupling.
float acousticHighPassHz(const AcousticConfig& cfg);
float acousticGainDb(const AcousticConfig& cfg);

}  // namespace ot
