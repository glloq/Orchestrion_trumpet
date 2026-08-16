#include "audio/WavetableSynth.h"

#include <cmath>
#include <new>

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

WavetableSynth::~WavetableSynth() {
    delete sets_[0];
    delete sets_[1];
}

void WavetableSynth::build(TableSet& set, const BuildSpec& spec) {
    const float nyquist = static_cast<float>(spec.sampleRate) * 0.5f;
    for (uint8_t level = 0; level < kWaveMipLevels; ++level) {
        const float f0 = kLevelBaseHz * std::pow(2.0f, static_cast<float>(level));
        // How many partials still fit under Nyquist at the TOP of this octave.
        int allowed = static_cast<int>(nyquist / (f0 * 2.0f));
        if (allowed < 1) allowed = 1;
        if (allowed > spec.additive.harmonicCount) allowed = spec.additive.harmonicCount;
        // The tilts come from the voicing. They used to be compiled in, which
        // made the Body <-> Brightness macro do nothing at all on this engine
        // while it worked on the additive one - the same slider, two answers.
        renderTable(set.dark[level], spec.additive, static_cast<uint8_t>(allowed), spec.darkTilt);
        renderTable(set.bright[level], spec.additive, static_cast<uint8_t>(allowed),
                    spec.brightTilt);
    }
}

bool WavetableSynth::matches(const AdditiveConfig& cfg, uint32_t sampleRate, float darkTilt,
                             float brightTilt) const {
    // Read from the live set. The consumer may swap under us, but the only
    // value it can swap in is one this same producer built, so a stale answer
    // is either "rebuild something identical" or "skip a rebuild that was
    // already done" - never a wrong table.
    const BuildSpec& live = specs_[ctrl_.load(std::memory_order_acquire) & kActive];
    if (!live.valid) return false;
    if (live.sampleRate != (sampleRate ? sampleRate : 48000)) return false;
    if (live.darkTilt != darkTilt || live.brightTilt != brightTilt) return false;
    if (live.additive.harmonicCount != cfg.harmonicCount) return false;
    for (uint8_t i = 0; i < kMaxHarmonics; ++i) {
        if (live.additive.harmonicGain[i] != cfg.harmonicGain[i]) return false;
    }
    return true;
}

void WavetableSynth::configure(const AdditiveConfig& cfg, uint32_t sampleRate, float darkTilt,
                               float brightTilt) {
    sampleRate_ = sampleRate ? sampleRate : 48000;
    if (!sets_[0]) sets_[0] = new (std::nothrow) TableSet();
    if (!sets_[1]) sets_[1] = new (std::nothrow) TableSet();
    if (!sets_[0] || !sets_[1]) {
        // Out of memory: this generator stays silent rather than reading a
        // table that does not exist. The additive engine is unaffected.
        built_ = false;
        return;
    }
    BuildSpec spec;
    spec.additive = cfg;
    spec.sampleRate = sampleRate_;
    spec.darkTilt = darkTilt;
    spec.brightTilt = brightTilt;
    spec.valid = true;

    liveSet_ = 0;
    specs_[0] = spec;
    build(*sets_[0], spec);
    ctrl_.store(0, std::memory_order_release);   // set 0 live, nothing waiting
    built_ = true;
}

bool WavetableSynth::prepare(const AdditiveConfig& cfg, uint32_t sampleRate, float darkTilt,
                             float brightTilt) {
    if (!sets_[0] || !sets_[1]) return false;
    uint8_t cur = ctrl_.load(std::memory_order_acquire);
    // Claim the spare. While kBuilding is set the consumer refuses to swap, so
    // the set this returns is genuinely ours for the duration of the build.
    for (;;) {
        // kBuilding: another producer, which there is not. kReady: a finished
        // build the audio task has not taken yet - and it may take it at any
        // instant, so writing into that set now is exactly the tear this whole
        // arrangement exists to prevent.
        if (cur & (kBuilding | kReady)) return false;
        if (ctrl_.compare_exchange_weak(cur, static_cast<uint8_t>(cur | kBuilding),
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            break;
        }
    }
    const uint8_t spare = static_cast<uint8_t>(1u - (cur & kActive));

    BuildSpec spec;
    spec.additive = cfg;
    spec.sampleRate = sampleRate ? sampleRate : 48000;
    spec.darkTilt = darkTilt;
    spec.brightTilt = brightTilt;
    spec.valid = true;
    specs_[spare] = spec;
    build(*sets_[spare], spec);

    // The live set cannot have changed while we held kBuilding, so publishing
    // is a plain store: same active bit, waiting flag set, building cleared.
    ctrl_.store(static_cast<uint8_t>((cur & kActive) | kReady), std::memory_order_release);
    return true;
}

bool WavetableSynth::adopt() {
    uint8_t cur = ctrl_.load(std::memory_order_acquire);
    if ((cur & (kReady | kBuilding)) != kReady) return false;
    // Flip the live set and clear the waiting flag in one operation. If the
    // producer got in first this fails and we simply try again next block.
    const uint8_t next = static_cast<uint8_t>((~cur) & kActive);
    if (!ctrl_.compare_exchange_strong(cur, next, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return false;
    }
    liveSet_ = next & kActive;
    sampleRate_ = specs_[liveSet_].sampleRate;
    built_ = true;
    return true;
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

    const TableSet& set = *sets_[liveSet_];
    const int16_t* d = set.dark[level_];
    const int16_t* b = set.bright[level_];
    const float dv = (d[index] + (d[next] - d[index]) * frac) * (1.0f / 32768.0f);
    const float bv = (b[index] + (b[next] - b[index]) * frac) * (1.0f / 32768.0f);
    return dv + (bv - dv) * brightness_;
}

}  // namespace ot
