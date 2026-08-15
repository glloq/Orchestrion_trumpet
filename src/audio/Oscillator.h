// ============================================================================
//  Oscillator.h - phase accumulators and the shared sine table.
//
//  Everything in the audio path uses fixed size tables and integer phase: no
//  allocation, no trigonometric call per sample.
// ============================================================================
#pragma once

#include "core/Platform.h"

namespace ot {

static constexpr uint16_t kSineTableSize = 1024;

// Builds the shared table once (idempotent).
void initSineTable();
// Linear interpolated sine, phase is a 32 bit fraction of one turn.
float sineFromPhase(uint32_t phase);

class Oscillator {
public:
    void setSampleRate(uint32_t sampleRate) { sampleRate_ = sampleRate ? sampleRate : 48000; }
    void setFrequency(float hz);
    void resetPhase(uint32_t phase = 0) { phase_ = phase; }

    float frequency() const { return frequency_; }
    uint32_t phase() const { return phase_; }

    float next() {
        const float s = sineFromPhase(phase_);
        phase_ += increment_;
        return s;
    }

private:
    uint32_t sampleRate_ = 48000;
    uint32_t phase_ = 0;
    uint32_t increment_ = 0;
    float frequency_ = 0.0f;
};

// MIDI note -> Hz, honouring a fractional detune in semitones (pitch bend,
// vibrato, portamento are all expressed that way).
float noteToFrequency(float midiNote);

// Cheap deterministic white noise, uniform in [-1, 1].
class NoiseSource {
public:
    float next() {
        // xorshift32: fast, no multiply, good enough for breath noise.
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);
    }

private:
    uint32_t state_ = 0x1234567u;
};

}  // namespace ot
