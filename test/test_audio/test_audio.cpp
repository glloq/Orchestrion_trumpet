// ============================================================================
//  Audio: speaker protection, limiter, envelope, filters and the generators.
// ============================================================================
#include <unity.h>

#include <cmath>

#include "Mocks.h"
#include "audio/AdditiveSynth.h"
#include "audio/Envelope.h"
#include "audio/Filters.h"
#include "audio/AcousticModel.h"
#include "core/LatestValue.h"
#include "audio/Limiter.h"
#include "audio/Oscillator.h"
#include "audio/Profiles.h"
#include "audio/WavetableSynth.h"

using namespace ot;
using namespace ot::mock;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Speaker protection
// ---------------------------------------------------------------------------
void test_protection_derates_an_oversized_amplifier(void) {
    SpeakerConfig speaker;
    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS5_XTS, speaker);   // 4 W limit
    AmplifierConfig amp;
    applyAmplifierDefaults(AmplifierType::TPA3118D2, amp);                      // 25 W

    const float scale = SpeakerProtection::safePeakScale(speaker, amp);
    // sqrt(4/25) = 0.4: the DSP may only use 40% of full scale.
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.4f, scale);
    TEST_ASSERT_TRUE(scale > 0.0f && scale < 1.0f);
}

void test_protection_leaves_a_matched_pair_alone(void) {
    SpeakerConfig speaker;
    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS8M, speaker);   // 20 W limit
    AmplifierConfig amp;
    applyAmplifierDefaults(AmplifierType::MAX98357_INTERNAL, amp);           // 3.2 W

    // The amplifier cannot overdrive the speaker, so nothing is taken away.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, SpeakerProtection::safePeakScale(speaker, amp));
}

void test_protection_honours_the_volume_limit(void) {
    SpeakerConfig speaker;
    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS8M, speaker);
    AmplifierConfig amp;
    applyAmplifierDefaults(AmplifierType::MAX98357_INTERNAL, amp);
    amp.volumeLimit = 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, SpeakerProtection::safePeakScale(speaker, amp));
}

void test_protection_never_returns_zero(void) {
    SpeakerConfig speaker;
    speaker.profile = SpeakerProfileId::CUSTOM;
    speaker.impedanceOhm = 8.0f;
    speaker.powerRmsW = 0.1f;
    speaker.powerLimitW = 0.05f;
    AmplifierConfig amp;
    amp.type = AmplifierType::CUSTOM;
    amp.maxPowerW = 200.0f;

    const float scale = SpeakerProtection::safePeakScale(speaker, amp);
    TEST_ASSERT_TRUE(scale > 0.0f);   // silence is not a safe state either
    TEST_ASSERT_TRUE(scale < 0.1f);
}

void test_effective_high_pass_takes_the_strictest(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 200.0f,
                             SpeakerProtection::effectiveHighPassHz(120.0f, 200.0f, 170.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 300.0f,
                             SpeakerProtection::effectiveHighPassHz(300.0f, 200.0f, 170.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 170.0f,
                             SpeakerProtection::effectiveHighPassHz(120.0f, 150.0f, 170.0f));
}

// ---------------------------------------------------------------------------
// Limiter
// ---------------------------------------------------------------------------
void test_limiter_hard_ceiling_always_clamps(void) {
    LimiterConfig cfg;
    cfg.enabled = false;          // even with the soft limiter disabled
    cfg.hardCeiling = 0.5f;
    Limiter limiter;
    limiter.configure(cfg, 48000);
    limiter.setPeakScale(1.0f);

    TEST_ASSERT_TRUE(limiter.process(2.0f) <= 0.5f + 1e-6f);
    TEST_ASSERT_TRUE(limiter.process(-2.0f) >= -0.5f - 1e-6f);
    TEST_ASSERT_GREATER_THAN_UINT32(0, limiter.clipEvents());
}

void test_limiter_applies_the_peak_scale(void) {
    LimiterConfig cfg;
    cfg.enabled = false;
    cfg.hardCeiling = 1.0f;
    Limiter limiter;
    limiter.configure(cfg, 48000);
    limiter.setPeakScale(0.25f);

    // A half scale signal comes out at a quarter of that.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.125f, limiter.process(0.5f));
}

void test_limiter_reduces_a_loud_signal(void) {
    LimiterConfig cfg;
    cfg.enabled = true;
    cfg.thresholdDb = -6.0f;      // 0.501
    cfg.attackMs = 0.5f;
    cfg.releaseMs = 50.0f;
    cfg.hardCeiling = 1.0f;
    Limiter limiter;
    limiter.configure(cfg, 48000);
    limiter.setPeakScale(1.0f);

    float peak = 0.0f;
    // A sustained full scale sine must end up under the threshold.
    for (uint32_t i = 0; i < 4800; ++i) {
        const float s = std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        const float out = limiter.process(s);
        if (i > 2400) peak = std::fmax(peak, std::fabs(out));
    }
    TEST_ASSERT_TRUE(peak <= 0.55f);
    TEST_ASSERT_TRUE(limiter.gainReduction() < 1.0f);
}

void test_limiter_leaves_a_quiet_signal_untouched(void) {
    LimiterConfig cfg;
    cfg.enabled = true;
    cfg.thresholdDb = -3.0f;
    cfg.hardCeiling = 1.0f;
    Limiter limiter;
    limiter.configure(cfg, 48000);
    limiter.setPeakScale(1.0f);

    for (uint32_t i = 0; i < 2000; ++i) limiter.process(0.05f);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.05f, limiter.process(0.05f));
    TEST_ASSERT_EQUAL_UINT32(0, limiter.clipEvents());
}

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------
void test_envelope_runs_through_its_stages(void) {
    EnvelopeConfig cfg;
    cfg.attackMs = 5.0f;
    cfg.decayMs = 20.0f;
    cfg.sustain = 0.6f;
    cfg.releaseMs = 10.0f;

    Envelope env;
    env.configure(cfg, 48000);
    TEST_ASSERT_EQUAL(Envelope::Stage::Idle, env.stage());

    env.noteOn(true);
    // Attack: rises to 1.0 within roughly 5 ms.
    for (uint32_t i = 0; i < 48000 * 5 / 1000 + 4; ++i) env.process();
    TEST_ASSERT_TRUE(env.value() > 0.95f);

    // Decay then sustain.
    for (uint32_t i = 0; i < 48000 * 100 / 1000; ++i) env.process();
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.6f, env.value());

    env.noteOff();
    for (uint32_t i = 0; i < 48000 * 200 / 1000; ++i) env.process();
    TEST_ASSERT_EQUAL(Envelope::Stage::Idle, env.stage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, env.value());
}

void test_envelope_attack_transient_decays(void) {
    EnvelopeConfig cfg;
    cfg.attackMs = 10.0f;
    Envelope env;
    env.configure(cfg, 48000);
    env.noteOn(true);

    const float first = env.attackTransient();
    for (uint32_t i = 0; i < 4800; ++i) env.process();   // 100 ms
    TEST_ASSERT_TRUE(env.attackTransient() < first * 0.2f);
}

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------
namespace {
float responseAt(Biquad& filter, float frequencyHz, uint32_t sampleRate) {
    filter.reset();
    float peak = 0.0f;
    const uint32_t total = sampleRate / 8;
    for (uint32_t i = 0; i < total; ++i) {
        const float s = std::sin(2.0f * 3.14159265f * frequencyHz * i / sampleRate);
        const float out = filter.process(s);
        if (i > total / 2) peak = std::fmax(peak, std::fabs(out));
    }
    return peak;
}
}  // namespace

void test_high_pass_removes_low_frequencies(void) {
    Biquad hpf;
    hpf.setHighPass(200.0f, 0.707f, 48000);
    const float low = responseAt(hpf, 40.0f, 48000);
    const float high = responseAt(hpf, 2000.0f, 48000);
    TEST_ASSERT_TRUE(low < 0.15f);
    TEST_ASSERT_TRUE(high > 0.9f);
}

void test_peaking_eq_boosts_its_band(void) {
    Biquad eq;
    eq.setPeaking(1000.0f, 6.0f, 1.0f, 48000);
    const float atBand = responseAt(eq, 1000.0f, 48000);
    const float faraway = responseAt(eq, 60.0f, 48000);
    TEST_ASSERT_TRUE(atBand > 1.7f);        // +6 dB is a factor of 2
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 1.0f, faraway);
}

void test_bypassed_filter_is_transparent(void) {
    Biquad filter;
    filter.setPeaking(1000.0f, 0.0f, 1.0f, 48000);   // 0 dB -> bypassed
    TEST_ASSERT_TRUE(filter.bypassed());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.42f, filter.process(0.42f));
}

void test_dc_blocker_removes_a_constant_offset(void) {
    DcBlocker blocker;
    blocker.setSampleRate(48000);
    float last = 0.0f;
    for (uint32_t i = 0; i < 48000; ++i) last = blocker.process(0.5f);
    TEST_ASSERT_TRUE(std::fabs(last) < 0.02f);
}

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------
void test_note_to_frequency(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 440.0f, noteToFrequency(69.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 261.63f, noteToFrequency(60.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 880.0f, noteToFrequency(81.0f));
    // A fractional note (pitch bend, vibrato, portamento) is a real pitch.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 453.08f, noteToFrequency(69.5f));
}

void test_additive_output_is_bounded_and_finite(void) {
    AdditiveConfig cfg;
    AdditiveSynth synth;
    synth.configure(cfg, 48000);
    synth.setFrequency(440.0f);
    synth.setBrightness(1.0f);

    float peak = 0.0f;
    for (uint32_t i = 0; i < 48000; ++i) {
        const float s = synth.process();
        TEST_ASSERT_TRUE(std::isfinite(s));
        peak = std::fmax(peak, std::fabs(s));
    }
    TEST_ASSERT_TRUE(peak <= 1.0f);
    TEST_ASSERT_TRUE(peak > 0.1f);   // it really is producing something
}

void test_additive_drops_partials_above_nyquist(void) {
    AdditiveConfig cfg;
    cfg.harmonicCount = 16;
    AdditiveSynth synth;
    synth.configure(cfg, 48000);

    synth.setFrequency(220.0f);
    synth.process();
    const uint8_t lowNote = synth.activeHarmonics();

    // At 6 kHz only the first three or four partials fit under 24 kHz.
    synth.setFrequency(6000.0f);
    synth.process();
    const uint8_t highNote = synth.activeHarmonics();

    TEST_ASSERT_TRUE(highNote < lowNote);
    TEST_ASSERT_TRUE(highNote >= 1);
}

void test_brightness_changes_the_harmonic_content(void) {
    AdditiveConfig cfg;
    AdditiveSynth dark, bright;
    dark.configure(cfg, 48000);
    bright.configure(cfg, 48000);
    dark.setFrequency(220.0f);
    bright.setFrequency(220.0f);
    dark.setBrightness(0.05f);
    bright.setBrightness(1.0f);

    // A brighter sound has more energy between successive samples: measure the
    // mean absolute difference, which grows with the upper partials.
    float darkSlope = 0.0f, brightSlope = 0.0f, prevDark = 0.0f, prevBright = 0.0f;
    for (uint32_t i = 0; i < 8192; ++i) {
        const float d = dark.process();
        const float b = bright.process();
        darkSlope += std::fabs(d - prevDark);
        brightSlope += std::fabs(b - prevBright);
        prevDark = d;
        prevBright = b;
    }
    TEST_ASSERT_TRUE(brightSlope > darkSlope);
}

// A voicing change must not make the audio task rebuild the mip-map: the
// producer prepares, the consumer takes it, and an unchanged spectrum costs
// nothing at all.
// The mailbox the live voicing travels through. A struct plus a volatile flag
// looked like this and was not this: the point is that the consumer only ever
// reads a value the producer has finished writing.
void test_latest_value_hands_over_whole_values(void) {
    struct Big {
        int a = 0;
        float b[8] = {0};
        char name[16] = "";
    };
    LatestValue<Big> box;

    Big out;
    TEST_ASSERT_FALSE(box.pending());
    TEST_ASSERT_FALSE(box.take(out));

    Big first;
    first.a = 7;
    for (int i = 0; i < 8; ++i) first.b[i] = static_cast<float>(i);
    copyString(first.name, sizeof(first.name), "first");
    box.publish(first);
    TEST_ASSERT_TRUE(box.pending());
    TEST_ASSERT_TRUE(box.take(out));
    TEST_ASSERT_EQUAL_INT(7, out.a);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, out.b[5]);
    TEST_ASSERT_EQUAL_STRING("first", out.name);
    // Taken once, and only once.
    TEST_ASSERT_FALSE(box.pending());
    TEST_ASSERT_FALSE(box.take(out));

    // Publishing faster than the consumer takes: the newest value wins and no
    // intermediate one is ever half-read.
    for (int n = 0; n < 10; ++n) {
        Big v;
        v.a = 100 + n;
        copyString(v.name, sizeof(v.name), "many");
        box.publish(v);
    }
    TEST_ASSERT_TRUE(box.take(out));
    TEST_ASSERT_EQUAL_INT(109, out.a);
    TEST_ASSERT_EQUAL_STRING("many", out.name);
    TEST_ASSERT_EQUAL_UINT32(11, box.published());
    TEST_ASSERT_EQUAL_UINT32(2, box.taken());
}

void test_wavetable_prepare_and_adopt(void) {
    AdditiveConfig cfg;
    WavetableSynth synth;
    synth.configure(cfg, 48000, 2.6f, 0.6f);
    TEST_ASSERT_TRUE(synth.matches(cfg, 48000, 2.6f, 0.6f));
    TEST_ASSERT_FALSE(synth.preparePending());
    // Nothing to take: adopt() is a no-op, not a rebuild.
    TEST_ASSERT_FALSE(synth.adopt());

    // A different spectrum is prepared off the audio task, and is only live
    // once the audio task has taken it.
    AdditiveConfig other = cfg;
    other.harmonicGain[3] = 0.9f;
    TEST_ASSERT_FALSE(synth.matches(other, 48000, 2.6f, 0.6f));
    TEST_ASSERT_TRUE(synth.prepare(other, 48000, 2.6f, 0.6f));
    TEST_ASSERT_TRUE(synth.preparePending());
    TEST_ASSERT_TRUE(synth.matches(cfg, 48000, 2.6f, 0.6f));   // still the old one
    TEST_ASSERT_TRUE(synth.adopt());
    TEST_ASSERT_TRUE(synth.matches(other, 48000, 2.6f, 0.6f));
    TEST_ASSERT_FALSE(synth.adopt());

    // The tilts are part of the tables, so moving them alone still rebuilds.
    TEST_ASSERT_FALSE(synth.matches(other, 48000, 3.4f, 0.6f));

    // A build that has not been taken yet owns the spare: the next one is
    // refused rather than overwriting a set the audio task may be about to use.
    TEST_ASSERT_TRUE(synth.prepare(cfg, 48000, 2.6f, 0.6f));
    TEST_ASSERT_FALSE(synth.prepare(other, 48000, 2.6f, 0.6f));
    TEST_ASSERT_TRUE(synth.adopt());
    TEST_ASSERT_TRUE(synth.matches(cfg, 48000, 2.6f, 0.6f));

    // And it really is a working oscillator afterwards.
    synth.setFrequency(220.0f);
    float peak = 0.0f;
    for (uint32_t i = 0; i < 4800; ++i) {
        const float s = synth.process();
        TEST_ASSERT_TRUE(std::isfinite(s));
        if (std::fabs(s) > peak) peak = std::fabs(s);
    }
    TEST_ASSERT_TRUE(peak > 0.1f);
}

void test_wavetable_output_is_bounded(void) {
    AdditiveConfig cfg;
    WavetableSynth synth;
    synth.configure(cfg, 48000, 2.6f, 0.6f);
    synth.setFrequency(330.0f);
    synth.setBrightness(0.8f);

    float peak = 0.0f;
    for (uint32_t i = 0; i < 24000; ++i) {
        const float s = synth.process();
        TEST_ASSERT_TRUE(std::isfinite(s));
        peak = std::fmax(peak, std::fabs(s));
    }
    TEST_ASSERT_TRUE(peak <= 1.05f);
    TEST_ASSERT_TRUE(peak > 0.1f);
}

void test_silent_generator_when_no_frequency(void) {
    AdditiveConfig cfg;
    AdditiveSynth synth;
    synth.configure(cfg, 48000);
    synth.setFrequency(0.0f);
    for (uint32_t i = 0; i < 128; ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, synth.process());
    }
}

// ---------------------------------------------------------------------------
// Backend contract
// ---------------------------------------------------------------------------
void test_mock_backend_contract(void) {
    MockAudioBackend backend;
    AudioConfig cfg;
    backend.configure(cfg);
    TEST_ASSERT_TRUE(backend.begin());
    TEST_ASSERT_TRUE(backend.isRunning());
    TEST_ASSERT_TRUE(backend.isMuted());   // a backend always comes up silent

    const int16_t samples[4] = {0, 1000, -2000, 32000};
    backend.writeSamples(samples, 4);
    TEST_ASSERT_EQUAL_size_t(4, backend.samplesWritten);
    TEST_ASSERT_EQUAL_INT32(32000, backend.peak);

    backend.mute(false);
    TEST_ASSERT_FALSE(backend.isMuted());
}

void test_backend_q31_default_narrows_to_16_bit(void) {
    MockAudioBackend backend;
    AudioConfig cfg;
    backend.configure(cfg);
    backend.begin();

    // Full scale Q31 must come out as full scale 16 bit.
    const int32_t samples[2] = {2147483392, -2147483392};
    backend.writeSamples32(samples, 2);
    TEST_ASSERT_EQUAL_size_t(2, backend.samplesWritten);
    TEST_ASSERT_INT32_WITHIN(2, 32767, backend.peak);
}


// ---------------------------------------------------------------------------
// Speaker catalogue.  These figures come from the manufacturers' datasheets;
// the point of pinning them is that a protection limit above the continuous
// rating is a real hazard, and it had shipped once already.
// ---------------------------------------------------------------------------
void test_catalogue_never_allows_more_than_the_rms_rating(void) {
    const SpeakerProfileId ids[] = {
        SpeakerProfileId::VISATON_FRS5_XTS, SpeakerProfileId::DAYTON_CE70PR4,
        SpeakerProfileId::VISATON_FRS8M, SpeakerProfileId::MONACOR_SPX30M,
        SpeakerProfileId::CUSTOM};
    for (SpeakerProfileId id : ids) {
        SpeakerConfig s;
        applySpeakerProfileDefaults(id, s);
        TEST_ASSERT_TRUE(s.powerLimitW > 0.0f);
        TEST_ASSERT_TRUE_MESSAGE(s.powerLimitW <= s.powerRmsW,
                                 "the protection limit is above the RMS rating");
        TEST_ASSERT_TRUE_MESSAGE(s.powerRmsW <= s.powerMaxW,
                                 "the RMS rating is above the maximum rating");
    }
}

void test_catalogue_figures_match_the_datasheets(void) {
    SpeakerConfig s;

    applySpeakerProfileDefaults(SpeakerProfileId::MONACOR_SPX30M, s);
    // 20 W RMS / 40 W max, 8 ohm.  The old table claimed 30 W RMS and allowed
    // 22 W, i.e. more than the coil is specified to take continuously.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, s.powerRmsW);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, s.powerMaxW);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, s.impedanceOhm);
    TEST_ASSERT_TRUE(s.powerLimitW <= 15.0f);

    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS5_XTS, s);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, s.powerRmsW);    // rated, not maximum
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, s.powerMaxW);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 120.0f, s.minFrequencyHz);

    applySpeakerProfileDefaults(SpeakerProfileId::DAYTON_CE70PR4, s);
    TEST_ASSERT_EQUAL_STRING("Dayton CE70PR-4", s.name);   // CE70P-4 does not exist
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, s.powerRmsW);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, s.impedanceOhm);

    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS8M, s);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, s.powerRmsW);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, s.powerMaxW);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 125.0f, s.fsHz);
}

// ---------------------------------------------------------------------------
// Acoustic model
// ---------------------------------------------------------------------------
void test_acoustic_model_reports_the_compression_ratio(void) {
    AcousticConfig a;              // reference geometry: 60 mm cone, 11 mm bore
    SpeakerConfig s;
    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS8M, s);
    const AcousticModel m = computeAcousticModel(a, s);

    // (60/11)^2 = 29.8 ... the reference build compresses hard, and the
    // validator is expected to say so.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 29.75f, m.compressionRatio);
    TEST_ASSERT_TRUE(m.helmholtzResonanceHz > 0.0f);
    TEST_ASSERT_TRUE(m.stage1HalfAngleDeg > 0.0f && m.stage1HalfAngleDeg < 45.0f);
}

void test_acoustic_model_never_invents_a_sealed_resonance(void) {
    AcousticConfig a;
    SpeakerConfig s;
    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS8M, s);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.vasLitres);   // not transcribed

    AcousticModel m = computeAcousticModel(a, s);
    TEST_ASSERT_FALSE(m.sealedResonanceKnown);
    TEST_ASSERT_EQUAL(static_cast<int>(AcousticSource::SPEAKER_PROFILE),
                      static_cast<int>(m.highPassSource));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, s.recommendedHighPassHz, m.highPassHz);

    // Once the builder enters fs and Vas the model uses them.
    s.fsHz = 125.0f;
    s.vasLitres = 1.4f;
    a.rearChamberVolumeMl = 120.0f;   // 0.12 litre
    m = computeAcousticModel(a, s);
    TEST_ASSERT_TRUE(m.sealedResonanceKnown);
    // 125 * sqrt(1 + 1.4/0.12) = 446 Hz
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 446.0f, m.sealedResonanceHz);
    TEST_ASSERT_EQUAL(static_cast<int>(AcousticSource::DERIVED),
                      static_cast<int>(m.highPassSource));
}

void test_acoustic_model_prefers_a_measured_figure(void) {
    AcousticConfig a;
    SpeakerConfig s;
    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS8M, s);
    s.fsHz = 125.0f;
    s.vasLitres = 1.4f;
    a.measuredHighPassHz = 190.0f;

    const AcousticModel m = computeAcousticModel(a, s);
    // A bench measurement beats the model, always.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 190.0f, m.highPassHz);
    TEST_ASSERT_EQUAL(static_cast<int>(AcousticSource::MEASURED),
                      static_cast<int>(m.highPassSource));
}

void test_acoustic_model_is_inert_in_open_air(void) {
    AcousticConfig a;
    a.coupling = AcousticCouplingType::OPEN_AIR;
    SpeakerConfig s;
    const AcousticModel m = computeAcousticModel(a, s);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.highPassHz);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.gainDb);
    TEST_ASSERT_EQUAL(static_cast<int>(AcousticSource::NOT_APPLICABLE),
                      static_cast<int>(m.highPassSource));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_protection_derates_an_oversized_amplifier);
    RUN_TEST(test_protection_leaves_a_matched_pair_alone);
    RUN_TEST(test_protection_honours_the_volume_limit);
    RUN_TEST(test_protection_never_returns_zero);
    RUN_TEST(test_effective_high_pass_takes_the_strictest);
    RUN_TEST(test_limiter_hard_ceiling_always_clamps);
    RUN_TEST(test_limiter_applies_the_peak_scale);
    RUN_TEST(test_limiter_reduces_a_loud_signal);
    RUN_TEST(test_limiter_leaves_a_quiet_signal_untouched);
    RUN_TEST(test_envelope_runs_through_its_stages);
    RUN_TEST(test_envelope_attack_transient_decays);
    RUN_TEST(test_high_pass_removes_low_frequencies);
    RUN_TEST(test_peaking_eq_boosts_its_band);
    RUN_TEST(test_bypassed_filter_is_transparent);
    RUN_TEST(test_dc_blocker_removes_a_constant_offset);
    RUN_TEST(test_note_to_frequency);
    RUN_TEST(test_additive_output_is_bounded_and_finite);
    RUN_TEST(test_additive_drops_partials_above_nyquist);
    RUN_TEST(test_brightness_changes_the_harmonic_content);
    RUN_TEST(test_latest_value_hands_over_whole_values);
    RUN_TEST(test_wavetable_prepare_and_adopt);
    RUN_TEST(test_wavetable_output_is_bounded);
    RUN_TEST(test_silent_generator_when_no_frequency);
    RUN_TEST(test_mock_backend_contract);
    RUN_TEST(test_backend_q31_default_narrows_to_16_bit);
    RUN_TEST(test_catalogue_never_allows_more_than_the_rms_rating);
    RUN_TEST(test_catalogue_figures_match_the_datasheets);
    RUN_TEST(test_acoustic_model_reports_the_compression_ratio);
    RUN_TEST(test_acoustic_model_never_invents_a_sealed_resonance);
    RUN_TEST(test_acoustic_model_prefers_a_measured_figure);
    RUN_TEST(test_acoustic_model_is_inert_in_open_air);
    return UNITY_END();
}
