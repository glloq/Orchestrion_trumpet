#include "config/Presets.h"

#include "audio/Profiles.h"

namespace ot {

namespace {

const PresetInfo kPresets[] = {
    {PresetId::LOW_COST, "LOW_COST", "Low cost",
     "ESP32 -> MAX98357A -> Dayton CE70P-4. Cheapest working chain.", 6, false, false, false},
    {PresetId::COMPACT, "COMPACT", "Compact",
     "ESP32 -> MAX98357A -> Visaton FRS 5 XTS. Smallest chamber, quiet rooms.", 6, false, false,
     false},
    {PresetId::STANDARD, "STANDARD", "Standard",
     "ESP32 -> PCM5102A -> TPA3118D2 -> Visaton FRS 8 M -> sealed chamber -> trumpet.", 9, true,
     false, false},
    {PresetId::QUALITY, "QUALITY", "Quality",
     "ESP32 -> PCM5102A -> TPA3118D2 -> Monacor SPX-30M.", 10, false, false, false},
    {PresetId::FEEDBACK, "FEEDBACK", "Feedback (measurement)",
     "ES8388 codec with microphone input, prepared for acoustic calibration.", 9, false, false,
     false},
    {PresetId::INTEGRATED, "INTEGRATED", "Integrated PCB",
     "ESP32 -> TAS5760M -> speaker. Target of the dedicated board.", 9, false, false, false},
    {PresetId::CUSTOM, "CUSTOM", "Custom", "Configure every element by hand.", 0, false, false,
     false},
};

void setSpeaker(InstrumentConfiguration& cfg, SpeakerProfileId id) {
    applySpeakerProfileDefaults(id, cfg.speaker);
    cfg.amplifier.speakerImpedanceOhm = cfg.speaker.impedanceOhm;
    cfg.audio.highPassHz = cfg.speaker.recommendedHighPassHz;
}

void setAmplifier(InstrumentConfiguration& cfg, AmplifierType type) {
    applyAmplifierDefaults(type, cfg.amplifier);
}

}  // namespace

uint8_t presetCount() { return static_cast<uint8_t>(sizeof(kPresets) / sizeof(kPresets[0])); }

const PresetInfo& presetInfo(uint8_t index) {
    return kPresets[index < presetCount() ? index : 0];
}

const PresetInfo* findPreset(const char* key) {
    for (uint8_t i = 0; i < presetCount(); ++i) {
        if (strEqualsI(key, kPresets[i].key)) return &kPresets[i];
    }
    return nullptr;
}

bool applyPreset(PresetId id, InstrumentConfiguration& cfg) {
    switch (id) {
        case PresetId::LOW_COST:
            cfg.audio.backend = AudioBackendType::MAX98357A;
            cfg.audio.bitDepth = 16;
            setAmplifier(cfg, AmplifierType::MAX98357_INTERNAL);
            setSpeaker(cfg, SpeakerProfileId::DAYTON_CE70PR4);
            cfg.acoustic.coupling = AcousticCouplingType::SEALED_CHAMBER;
            break;

        case PresetId::COMPACT:
            cfg.audio.backend = AudioBackendType::MAX98357A;
            cfg.audio.bitDepth = 16;
            setAmplifier(cfg, AmplifierType::MAX98357_INTERNAL);
            setSpeaker(cfg, SpeakerProfileId::VISATON_FRS5_XTS);
            cfg.acoustic.coupling = AcousticCouplingType::SEALED_CHAMBER;
            // Small driver, small chambers, and the first cone starts at the
            // 50 mm cone rather than 60 mm.
            cfg.acoustic.rearChamberVolumeMl = 60.0f;
            cfg.acoustic.frontChamberVolumeMl = 20.0f;
            cfg.acoustic.stage1 = HornStageConfig{50.0f, 24.0f, 45.0f};
            cfg.acoustic.intermediateDiameterMm = 24.0f;
            cfg.acoustic.stage2 = HornStageConfig{24.0f, 12.0f, 35.0f};
            break;

        case PresetId::STANDARD:
            cfg.audio.backend = AudioBackendType::PCM5102A;
            cfg.audio.bitDepth = 24;
            setAmplifier(cfg, AmplifierType::TPA3118D2);
            setSpeaker(cfg, SpeakerProfileId::VISATON_FRS8M);
            // The reference bench: FRS 8 M, 120 ml sealed rear chamber, a
            // 35 ml front chamber and the two-stage cone 60 -> 28 -> 12 mm.
            // These are build dimensions, not measurements: the acoustic model
            // reports them as derived until the bench says otherwise.
            cfg.acoustic.coupling = AcousticCouplingType::SEALED_CHAMBER;
            cfg.acoustic.rearChamberVolumeMl = 120.0f;
            cfg.acoustic.frontChamberVolumeMl = 35.0f;
            cfg.acoustic.stage1 = HornStageConfig{60.0f, 28.0f, 58.0f};
            cfg.acoustic.intermediateDiameterMm = 28.0f;
            cfg.acoustic.intermediateLengthMm = 20.0f;
            cfg.acoustic.stage2 = HornStageConfig{28.0f, 12.0f, 40.0f};
            cfg.acoustic.leadpipeDiameterMm = 11.0f;
            break;

        case PresetId::QUALITY:
            cfg.audio.backend = AudioBackendType::PCM5102A;
            cfg.audio.bitDepth = 24;
            setAmplifier(cfg, AmplifierType::TPA3118D2);
            setSpeaker(cfg, SpeakerProfileId::MONACOR_SPX30M);
            cfg.acoustic.coupling = AcousticCouplingType::SEALED_CHAMBER;
            break;

        case PresetId::FEEDBACK:
            cfg.audio.backend = AudioBackendType::ES8388;
            cfg.audio.bitDepth = 24;
            cfg.audio.i2s.din = 10;   // codec ADC line, used by the future
            cfg.audio.i2s.mclk = 11;  // measurement loop
            cfg.audio.codecAddress = 0x10;
            setAmplifier(cfg, AmplifierType::TPA3118D2);
            setSpeaker(cfg, SpeakerProfileId::VISATON_FRS8M);
            cfg.acoustic.coupling = AcousticCouplingType::SEALED_CHAMBER;
            break;

        case PresetId::INTEGRATED:
            cfg.audio.backend = AudioBackendType::TAS5760M;
            cfg.audio.bitDepth = 24;
            cfg.audio.codecAddress = 0x6C;
            setAmplifier(cfg, AmplifierType::TAS5760_INTERNAL);
            setSpeaker(cfg, SpeakerProfileId::VISATON_FRS8M);
            cfg.acoustic.coupling = AcousticCouplingType::SEALED_CHAMBER;
            break;

        case PresetId::CUSTOM:
            break;

        default:
            return false;
    }

    const PresetInfo& info = presetInfo(static_cast<uint8_t>(id));
    copyString(cfg.system.presetName, sizeof(cfg.system.presetName), info.key);
    return true;
}

}  // namespace ot
