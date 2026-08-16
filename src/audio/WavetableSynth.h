// ============================================================================
//  WavetableSynth.h - band limited single cycle trumpet wave.
//
//  Cheaper than the additive engine (one interpolated lookup per sample) and
//  useful on the plain ESP32 or when the CPU is busy.  Aliasing is handled by
//  a mip-map: one table per octave, each built with only the partials that fit
//  under Nyquist for that octave.  Two tables (dark / bright) are cross-faded
//  by the same "blow" parameter as the additive engine so both generators
//  react identically to the controllers.
//
//  Building that mip-map costs 8 levels x 2 tables x up to 16 partials x 512
//  points of sin() - over a hundred thousand of them.  Fine once at boot,
//  catastrophic inside an audio block, which is exactly where a Sound Lab
//  harmonic slider used to put it.  So the work is split:
//
//      prepare()   builds into the spare table set.  Called from the network or
//                  application task; never from the audio task.
//      adopt()     makes it the live set.  Called from the audio task, and
//                  costs one compare-and-swap.
//
//  Ownership is carried by a single atomic word rather than by a pair of
//  indices, because two indices written by two tasks are two more races:
//
//      bit 0   which set is live
//      bit 1   a freshly built set is waiting
//      bit 2   the producer is writing the spare right now
//
//  The consumer refuses to swap while bit 2 is set, and its swap flips bit 0
//  and clears bit 1 in one operation, so the producer can never be writing the
//  set the audio task is reading from.
// ============================================================================
#pragma once

#include <atomic>

#include "audio/Oscillator.h"
#include "config/ConfigTypes.h"

namespace ot {

static constexpr uint16_t kWaveTableSize = 512;
static constexpr uint8_t kWaveMipLevels = 8;

class WavetableSynth {
public:
    // Builds and makes live in one go. For boot and for the host tests: this is
    // the expensive path and it must never run on the audio task.
    void configure(const AdditiveConfig& cfg, uint32_t sampleRate, float darkTilt,
                   float brightTilt);

    // Producer side. False when the audio task has not taken the previous
    // build yet; the caller should retry rather than overwrite it.
    bool prepare(const AdditiveConfig& cfg, uint32_t sampleRate, float darkTilt,
                 float brightTilt);
    // True when these settings would rebuild exactly the tables already live,
    // so a slider that does not touch the spectrum costs nothing at all.
    bool matches(const AdditiveConfig& cfg, uint32_t sampleRate, float darkTilt,
                 float brightTilt) const;
    bool preparePending() const { return (ctrl_.load(std::memory_order_acquire) & kReady) != 0; }

    // Consumer side, audio task.
    bool adopt();

    ~WavetableSynth();

    void setFrequency(float hz);
    void setBrightness(float b) { brightness_ = clampValue(b, 0.0f, 1.0f); }
    void resetPhase() { phase_ = 0; }

    float process();

private:
    static constexpr uint8_t kActive = 0x01;
    static constexpr uint8_t kReady = 0x02;
    static constexpr uint8_t kBuilding = 0x04;

    struct TableSet {
        int16_t dark[kWaveMipLevels][kWaveTableSize];
        int16_t bright[kWaveMipLevels][kWaveTableSize];
    };

    // What a set was built from. One per set, owned by whoever owns the set.
    struct BuildSpec {
        AdditiveConfig additive;
        uint32_t sampleRate = 0;
        float darkTilt = 0.0f;
        float brightTilt = 0.0f;
        bool valid = false;
    };

    static void build(TableSet& set, const BuildSpec& spec);

    // On the heap, not in .bss: two sets are 32 kB and the classic ESP32's
    // static data segment does not have 32 kB to spare - it overflows the link
    // by exactly that. The allocation happens once, in configure(), and is
    // never touched again on the audio path.
    TableSet* sets_[2] = {nullptr, nullptr};
    BuildSpec specs_[2];
    std::atomic<uint8_t> ctrl_{0};   // set 0 live, nothing waiting, nobody building

    uint32_t sampleRate_ = 48000;
    uint32_t phase_ = 0;
    uint32_t increment_ = 0;
    float frequency_ = 0.0f;
    float brightness_ = 0.5f;
    uint8_t level_ = 0;
    uint8_t liveSet_ = 0;   // audio task's cached copy of bit 0
    bool built_ = false;
};

}  // namespace ot
