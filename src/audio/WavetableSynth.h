// ============================================================================
//  WavetableSynth.h - band limited single cycle trumpet wave.
//
//  Cheaper than the additive engine (one interpolated lookup per sample) and
//  useful on the plain ESP32 or when the CPU is busy.  Aliasing is handled by
//  a mip-map: one table per octave, each built with only the partials that fit
//  under Nyquist for that octave.  Two tables (dark / bright) are cross-faded
//  by the same "blow" parameter as the additive engine so both generators
//  react identically to the controllers.
// ============================================================================
#pragma once

#include "audio/Oscillator.h"
#include "config/ConfigTypes.h"

namespace ot {

static constexpr uint16_t kWaveTableSize = 512;
static constexpr uint8_t kWaveMipLevels = 8;

class WavetableSynth {
public:
    void configure(const AdditiveConfig& cfg, uint32_t sampleRate);

    void setFrequency(float hz);
    void setBrightness(float b) { brightness_ = clampValue(b, 0.0f, 1.0f); }
    void resetPhase() { phase_ = 0; }

    float process();

private:
    void build(const AdditiveConfig& cfg);

    // Two voicings, 8 octave levels, 512 points, stored as int16 to keep the
    // whole set at 16 kB.
    int16_t dark_[kWaveMipLevels][kWaveTableSize];
    int16_t bright_[kWaveMipLevels][kWaveTableSize];
    uint32_t sampleRate_ = 48000;
    uint32_t phase_ = 0;
    uint32_t increment_ = 0;
    float frequency_ = 0.0f;
    float brightness_ = 0.5f;
    uint8_t level_ = 0;
    bool built_ = false;
};

}  // namespace ot
