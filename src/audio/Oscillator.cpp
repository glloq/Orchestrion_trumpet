#include "audio/Oscillator.h"

#include <cmath>

namespace ot {

namespace {
float g_sine[kSineTableSize + 1];
bool g_ready = false;
}  // namespace

void initSineTable() {
    if (g_ready) return;
    for (uint16_t i = 0; i <= kSineTableSize; ++i) {
        g_sine[i] = std::sin(2.0f * 3.14159265358979f * static_cast<float>(i) /
                             static_cast<float>(kSineTableSize));
    }
    g_ready = true;
}

float sineFromPhase(uint32_t phase) {
    // Top 10 bits index the table, the next 8 bits interpolate.
    const uint32_t index = phase >> 22;                       // 0..1023
    const float frac = static_cast<float>((phase >> 6) & 0xFFFF) * (1.0f / 65536.0f);
    const float a = g_sine[index];
    const float b = g_sine[index + 1];
    return a + (b - a) * frac;
}

void Oscillator::setFrequency(float hz) {
    frequency_ = hz;
    if (hz <= 0.0f) {
        increment_ = 0;
        return;
    }
    const float ratio = hz / static_cast<float>(sampleRate_);
    increment_ = static_cast<uint32_t>(ratio * 4294967296.0f);
}

float noteToFrequency(float midiNote) {
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

}  // namespace ot
