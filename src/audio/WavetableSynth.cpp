#include "audio/WavetableSynth.h"

#include <cmath>

namespace ot {

namespace {
// Level 0 covers the bottom of the range (up to ~110 Hz), each further level
// one octave up.  Frequency -> level is a log2 of the ratio.
constexpr float kLevelBaseHz = 55.0f;

void renderTable(int16_t* out, const AdditiveConfig& cfg, uint8_t maxHarmonics, float tilt) {
    float acc[kWaveTableSize];
    float peak = 0.0001f;
    for (uint16_t i = 0; i < kWaveTableSize; ++i) acc[i] = 0.0f;

    for (uint8_t h = 0; h < maxHarmonics && h < kMaxHarmonics; ++h) {
        const float n = static_cast<float>(h + 1);
        const float gain = cfg.harmonicGain[h] * std::pow(n, -tilt + 1.0f);
        if (gain <= 0.0f) continue;
        for (uint16_t i = 0; i < kWaveTableSize; ++i) {
            const float ph = 2.0f * 3.14159265358979f * n * static_cast<float>(i) /
                             static_cast<float>(kWaveTableSize);
            acc[i] += gain * std::sin(ph);
        }
    }
    for (uint16_t i = 0; i < kWaveTableSize; ++i) {
        const float a = std::fabs(acc[i]);
        if (a > peak) peak = a;
    }
    const float scale = 32000.0f / peak;
    for (uint16_t i = 0; i < kWaveTableSize; ++i) {
        out[i] = static_cast<int16_t>(clampValue(acc[i] * scale, -32000.0f, 32000.0f));
    }
}
}  // namespace

void WavetableSynth::configure(const AdditiveConfig& cfg, uint32_t sampleRate) {
    sampleRate_ = sampleRate ? sampleRate : 48000;
    build(cfg);
    built_ = true;
}

void WavetableSynth::build(const AdditiveConfig& cfg) {
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
    for (uint8_t level = 0; level < kWaveMipLevels; ++level) {
        const float f0 = kLevelBaseHz * std::pow(2.0f, static_cast<float>(level));
        // How many partials still fit under Nyquist at the TOP of this octave.
        int allowed = static_cast<int>(nyquist / (f0 * 2.0f));
        if (allowed < 1) allowed = 1;
        if (allowed > cfg.harmonicCount) allowed = cfg.harmonicCount;
        renderTable(dark_[level], cfg, static_cast<uint8_t>(allowed), 2.6f);
        renderTable(bright_[level], cfg, static_cast<uint8_t>(allowed), 0.6f);
    }
}

void WavetableSynth::setFrequency(float hz) {
    frequency_ = hz;
    if (hz <= 0.0f) {
        increment_ = 0;
        return;
    }
    increment_ = static_cast<uint32_t>((hz / static_cast<float>(sampleRate_)) * 4294967296.0f);

    int level = static_cast<int>(std::floor(std::log2(hz / kLevelBaseHz)));
    level = static_cast<int>(clampValue(level, 0, static_cast<int>(kWaveMipLevels) - 1));
    level_ = static_cast<uint8_t>(level);
}

float WavetableSynth::process() {
    if (!built_ || increment_ == 0) return 0.0f;

    const uint32_t index = phase_ >> 23;                    // 0..511
    const uint32_t next = (index + 1) & (kWaveTableSize - 1);
    const float frac = static_cast<float>((phase_ >> 7) & 0xFFFF) * (1.0f / 65536.0f);
    phase_ += increment_;

    const int16_t* d = dark_[level_];
    const int16_t* b = bright_[level_];
    const float dv = (d[index] + (d[next] - d[index]) * frac) * (1.0f / 32768.0f);
    const float bv = (b[index] + (b[next] - b[index]) * frac) * (1.0f / 32768.0f);
    return dv + (bv - dv) * brightness_;
}

}  // namespace ot
