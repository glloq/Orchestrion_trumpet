#include "audio/Profiles.h"

#include <cmath>

namespace ot {

namespace {

struct SpeakerRow {
    SpeakerProfileId id;
    const char* name;
    float impedance;
    float ratedPower;
    float fMin;
    float fMax;
    float hpf;
    float gainCorrectionDb;
    float powerLimit;
    EqBand eq[kMaxEqBands];
};

// Datasheet figures, rounded to what actually matters for the protection and
// voicing stages.  `powerLimit` is deliberately well below the RMS rating:
// the driver sits in a sealed chamber feeding a trumpet leadpipe and never
// needs its full excursion.
const SpeakerRow kSpeakers[] = {
    {SpeakerProfileId::VISATON_FRS5_XTS,
     "Visaton FRS 5 XTS", 8.0f, 8.0f, 150.0f, 20000.0f, 200.0f, 0.0f, 5.0f,
     {{300.0f, -1.5f, 0.8f, true}, {3000.0f, 2.0f, 0.9f, true}, {7000.0f, 1.0f, 0.8f, false}}},

    {SpeakerProfileId::DAYTON_CE70PR4,
     "Dayton CE70P-4", 4.0f, 15.0f, 130.0f, 15000.0f, 170.0f, -1.0f, 8.0f,
     {{250.0f, -2.0f, 0.8f, true}, {2500.0f, 2.5f, 0.9f, true}, {6000.0f, 1.5f, 0.8f, false}}},

    {SpeakerProfileId::VISATON_FRS8M,
     "Visaton FRS 8 M", 8.0f, 30.0f, 120.0f, 20000.0f, 160.0f, 0.0f, 20.0f,
     {{220.0f, -1.0f, 0.8f, true}, {2200.0f, 2.0f, 0.9f, true}, {5000.0f, 1.5f, 0.8f, true}}},

    {SpeakerProfileId::MONACOR_SPX30M,
     "Monacor SPX-30M", 8.0f, 30.0f, 110.0f, 20000.0f, 150.0f, 0.0f, 22.0f,
     {{200.0f, -0.5f, 0.8f, true}, {2000.0f, 1.5f, 0.9f, true}, {5500.0f, 1.0f, 0.8f, true}}},

    {SpeakerProfileId::CUSTOM,
     "Custom speaker", 8.0f, 10.0f, 150.0f, 18000.0f, 200.0f, 0.0f, 6.0f,
     {{250.0f, 0.0f, 0.8f, false}, {2500.0f, 0.0f, 0.9f, false}, {6000.0f, 0.0f, 0.8f, false}}},
};

const SpeakerRow& rowFor(SpeakerProfileId id) {
    for (const auto& r : kSpeakers) {
        if (r.id == id) return r;
    }
    return kSpeakers[0];
}

}  // namespace

void applySpeakerProfileDefaults(SpeakerProfileId id, SpeakerConfig& out) {
    const SpeakerRow& r = rowFor(id);
    out.profile = id;
    if (id != SpeakerProfileId::CUSTOM) {
        copyString(out.name, sizeof(out.name), r.name);
        out.impedanceOhm = r.impedance;
        out.powerRmsW = r.ratedPower;
        out.minFrequencyHz = r.fMin;
        out.maxFrequencyHz = r.fMax;
        out.recommendedHighPassHz = r.hpf;
        out.gainCorrectionDb = r.gainCorrectionDb;
        out.powerLimitW = r.powerLimit;
    }
}

void applyAmplifierDefaults(AmplifierType type, AmplifierConfig& out) {
    out.type = type;
    switch (type) {
        case AmplifierType::NONE:
            out.maxPowerW = 0.0f;
            out.gainDb = 0.0f;
            break;
        case AmplifierType::MAX98357_INTERNAL:
            // 3.2 W into 4 ohm at 5 V, fixed 9 dB gain by default.
            out.maxPowerW = 3.2f;
            out.gainDb = 9.0f;
            out.speakerImpedanceOhm = 4.0f;
            break;
        case AmplifierType::TPA3118D2:
            out.maxPowerW = 25.0f;
            out.gainDb = 26.0f;
            out.speakerImpedanceOhm = 8.0f;
            break;
        case AmplifierType::TAS5760_INTERNAL:
            out.maxPowerW = 20.0f;
            out.gainDb = 22.0f;
            out.speakerImpedanceOhm = 8.0f;
            break;
        case AmplifierType::CUSTOM:
            break;
    }
}

void SpeakerProfile::configure(const SpeakerConfig& cfg) {
    const SpeakerRow& r = rowFor(cfg.profile);
    copyString(name_, sizeof(name_), cfg.name[0] ? cfg.name : r.name);
    // Only a value of zero means "not configured": any positive number the
    // user typed is theirs to keep, including a deliberately tiny power limit.
    // Falling back to the catalogue value above a threshold would silently
    // hand a small driver far more power than it was told to accept.
    impedance_ = cfg.impedanceOhm > 0.0f ? cfg.impedanceOhm : r.impedance;
    ratedPower_ = cfg.powerRmsW > 0.0f ? cfg.powerRmsW : r.ratedPower;
    fMin_ = cfg.minFrequencyHz;
    fMax_ = cfg.maxFrequencyHz;
    hpf_ = cfg.recommendedHighPassHz > 0.0f ? cfg.recommendedHighPassHz : r.hpf;
    gainCorrection_ = cfg.gainCorrectionDb;
    powerLimit_ = cfg.powerLimitW > 0.0f ? cfg.powerLimitW : r.powerLimit;

    eqCount_ = 0;
    for (uint8_t i = 0; i < kMaxEqBands; ++i) {
        eq_[i] = r.eq[i];
        if (eq_[i].enabled) eqCount_ = static_cast<uint8_t>(i + 1);
    }
    if (eqCount_ == 0) eqCount_ = kMaxEqBands;
}

void AmplifierProfile::configure(const AmplifierConfig& cfg) {
    name_ = toString(cfg.type);
    maxPower_ = cfg.maxPowerW;
    gain_ = cfg.gainDb;
    volumeLimit_ = clampValue(cfg.volumeLimit, 0.0f, 1.0f);
    integrated_ = cfg.type == AmplifierType::MAX98357_INTERNAL ||
                  cfg.type == AmplifierType::TAS5760_INTERNAL;
}

float acousticHighPassHz(const AcousticConfig& cfg) {
    switch (cfg.coupling) {
        case AcousticCouplingType::OPEN_AIR:
            // Nothing loads the driver: rely on the speaker's own corner.
            return 0.0f;
        case AcousticCouplingType::SEALED_CHAMBER:
        case AcousticCouplingType::CUSTOM_CHAMBER:
        default:
            // A sealed front chamber raises the effective corner: the cone has
            // to work against the trapped air, so everything below is wasted
            // excursion and is removed before the amplifier.
            return cfg.highPassHz;
    }
}

float acousticGainDb(const AcousticConfig& cfg) {
    return cfg.coupling == AcousticCouplingType::OPEN_AIR ? 0.0f : cfg.eqGainDb;
}

}  // namespace ot
