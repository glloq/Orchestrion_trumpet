// ============================================================================
//  Presets.h - the documented hardware bundles offered by the wizard.
//
//  Picking a preset only fills in the audio chain (DAC, amplifier, speaker,
//  DSP and power limits).  MIDI, valves and network settings are untouched so
//  a preset can be applied at any time without losing a calibration.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

enum class PresetId : uint8_t {
    LOW_COST = 0,
    COMPACT,
    STANDARD,     // reference configuration used for audio development
    QUALITY,
    FEEDBACK,
    INTEGRATED,
    CUSTOM,
    COUNT
};

struct PresetInfo {
    PresetId id;
    const char* key;
    const char* title;
    const char* summary;
    uint8_t stars;        // relative quality, 0..10 (half stars)
    bool recommended;
    bool requiresNativeUsb;
    bool requiresInternalDac;
};

uint8_t presetCount();
const PresetInfo& presetInfo(uint8_t index);
const PresetInfo* findPreset(const char* key);

// Applies the audio chain of `id` on top of `cfg`.
bool applyPreset(PresetId id, InstrumentConfiguration& cfg);

}  // namespace ot
