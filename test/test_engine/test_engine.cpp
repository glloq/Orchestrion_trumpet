// ============================================================================
//  Sound engine: end-to-end behaviour of AudioEngine, driven by MIDI.
// ============================================================================
#include <unity.h>

#include <cmath>

#include "audio/AudioEngine.h"
#include "config/ConfigManager.h"

using namespace ot;

namespace {

constexpr size_t kBlock = 128;

struct Rig {
    AudioEngine engine;
    InstrumentConfiguration cfg;
    float buffer[kBlock];

    Rig() {
        ConfigManager::makeDefaults(cfg);
        cfg.audio.startupMute = false;
        cfg.audio.vibrato.source = VibratoSource::OFF;
        // Most of these tests are about the DSP, not about the pistons: the
        // attack delay is exercised on its own in test_valve_sync.
        cfg.valves.sync.enabled = false;
    }

    void start() {
        engine.configure(cfg.audio, cfg.speaker, cfg.amplifier, cfg.acoustic, cfg.instrument,
                         cfg.valves);
        engine.begin();
        engine.setMuted(false);
    }

    void render(size_t blocks) {
        for (size_t i = 0; i < blocks; ++i) engine.renderBlock(buffer, kBlock);
    }

    float peakOver(size_t blocks) {
        float peak = 0.0f;
        for (size_t b = 0; b < blocks; ++b) {
            engine.renderBlock(buffer, kBlock);
            for (size_t i = 0; i < kBlock; ++i) peak = std::fmax(peak, std::fabs(buffer[i]));
        }
        return peak;
    }
};

}  // namespace

void setUp() { hostSetMillis(0); }
void tearDown() {}

void test_engine_is_silent_until_a_note_arrives(void) {
    Rig rig;
    rig.start();
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.0f, rig.peakOver(20));
}

void test_engine_produces_sound_on_a_note(void) {
    Rig rig;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    TEST_ASSERT_TRUE(rig.peakOver(60) > 0.01f);

    const AudioEngineStatus status = rig.engine.status();
    TEST_ASSERT_TRUE(status.noteActive);
    TEST_ASSERT_EQUAL_UINT8(69, status.note);
    // A 440 Hz concert note really comes out at 440 Hz.
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 440.0f, status.frequencyHz);
}

void test_engine_falls_silent_after_the_release(void) {
    Rig rig;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    rig.render(40);
    rig.engine.onMidi(MidiMessage::noteOff(1, 69));
    // Well past the 70 ms release at 48 kHz.
    rig.render(200);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rig.peakOver(20));
}

void test_engine_mute_is_absolute(void) {
    Rig rig;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    rig.render(40);
    rig.engine.setMuted(true);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, rig.peakOver(20));
}

void test_engine_panic_silences_immediately(void) {
    Rig rig;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    rig.render(40);
    rig.engine.panic();
    rig.render(4);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, rig.peakOver(10));
}

void test_engine_all_notes_off(void) {
    Rig rig;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    rig.render(40);
    rig.engine.onMidi(MidiMessage::controlChange(1, cc::AllNotesOff, 0));
    rig.render(200);
    TEST_ASSERT_FALSE(rig.engine.status().noteActive);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rig.peakOver(20));
}

void test_engine_pitch_bend_range(void) {
    Rig rig;
    rig.cfg.audio.pitchBendRangeSemitones = 2;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    rig.render(4);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 69.0f, rig.engine.livePitch());

    // Full bend up must be exactly the configured range.
    rig.engine.onMidi(MidiMessage::pitchBend(1, 8191));
    rig.render(4);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 71.0f, rig.engine.livePitch());

    rig.engine.onMidi(MidiMessage::pitchBend(1, -8192));
    rig.render(4);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 67.0f, rig.engine.livePitch());
}

void test_engine_vibrato_runs_at_the_configured_rate(void) {
    Rig rig;
    rig.cfg.audio.vibrato.source = VibratoSource::AUTOMATIC;
    rig.cfg.audio.vibrato.frequencyHz = 5.0f;
    rig.cfg.audio.vibrato.depthCents = 100.0f;
    rig.cfg.audio.vibrato.delayMs = 0.0f;
    rig.cfg.audio.vibrato.fadeInMs = 1.0f;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));

    // Render exactly one second and count how often the pitch crosses the
    // nominal note: a 5 Hz modulation crosses ten times.  This is the test
    // that catches the LFO being advanced at the wrong rate.
    const size_t blocks = 48000 / kBlock;
    int crossings = 0;
    rig.render(4);
    bool above = rig.engine.livePitch() >= 69.0f;
    for (size_t i = 0; i < blocks; ++i) {
        rig.engine.renderBlock(rig.buffer, kBlock);
        const bool nowAbove = rig.engine.livePitch() >= 69.0f;
        if (nowAbove != above) {
            ++crossings;
            above = nowAbove;
        }
    }
    TEST_ASSERT_INT_WITHIN(2, 10, crossings);
}

void test_engine_vibrato_off_means_a_steady_pitch(void) {
    Rig rig;
    rig.cfg.audio.vibrato.source = VibratoSource::OFF;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    rig.render(4);
    for (size_t i = 0; i < 200; ++i) {
        rig.engine.renderBlock(rig.buffer, kBlock);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 69.0f, rig.engine.livePitch());
    }
}

void test_engine_velocity_changes_the_level(void) {
    Rig soft, loud;
    soft.start();
    loud.start();
    soft.engine.onMidi(MidiMessage::noteOn(1, 69, 20));
    loud.engine.onMidi(MidiMessage::noteOn(1, 69, 127));
    TEST_ASSERT_TRUE(loud.peakOver(60) > soft.peakOver(60));
}

void test_engine_respects_note_priority(void) {
    Rig rig;
    rig.cfg.instrument.notePriority = NotePriority::HIGHEST;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 60, 100));
    rig.engine.onMidi(MidiMessage::noteOn(1, 72, 100));
    rig.render(4);
    TEST_ASSERT_EQUAL_UINT8(72, rig.engine.status().note);
    rig.engine.onMidi(MidiMessage::noteOff(1, 72));
    rig.render(4);
    TEST_ASSERT_EQUAL_UINT8(60, rig.engine.status().note);
}

void test_engine_transposes_written_pitch(void) {
    Rig rig;
    rig.cfg.instrument.type = InstrumentType::BB_TRUMPET;
    rig.cfg.instrument.pitchMode = PitchInterpretation::WRITTEN;
    rig.start();
    // Written A4 on a Bb trumpet sounds a whole tone lower: G4, 392 Hz.
    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 100));
    rig.render(4);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 67.0f, rig.engine.livePitch());
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 392.0f, rig.engine.status().frequencyHz);
}

void test_engine_output_never_exceeds_the_safe_peak(void) {
    Rig rig;
    // A small driver behind a big amplifier: the protection stage must hold.
    applySpeakerProfileDefaults(SpeakerProfileId::VISATON_FRS5_XTS, rig.cfg.speaker);
    applyAmplifierDefaults(AmplifierType::TPA3118D2, rig.cfg.amplifier);
    rig.cfg.audio.masterVolume = 1.0f;
    rig.start();

    const float ceiling = rig.engine.safePeakScale();
    TEST_ASSERT_TRUE(ceiling < 0.6f);

    rig.engine.onMidi(MidiMessage::noteOn(1, 69, 127));
    rig.engine.onMidi(MidiMessage::controlChange(1, cc::Breath, 127));
    TEST_ASSERT_TRUE(rig.peakOver(200) <= ceiling + 0.001f);
}

void test_engine_test_tone_and_sweep(void) {
    Rig rig;
    rig.start();
    rig.engine.startTestTone(440.0f, 0.3f, 500);
    TEST_ASSERT_TRUE(rig.engine.testSignalActive());
    TEST_ASSERT_TRUE(rig.peakOver(60) > 0.05f);

    rig.engine.stopTestSignal();
    TEST_ASSERT_FALSE(rig.engine.testSignalActive());

    rig.engine.startSweep(100.0f, 4000.0f, 200, 0.3f);
    TEST_ASSERT_TRUE(rig.peakOver(30) > 0.05f);
}

// ---------------------------------------------------------------------------
// Valve -> audio synchronisation
//
// The pistons change the resonator, so a note that starts before they have
// arrived is played through the wrong bore.  The engine holds the attack for
// exactly as long as the actuators need, and not a millisecond longer.
// ---------------------------------------------------------------------------
void test_engine_waits_for_the_pistons_before_speaking(void) {
    Rig rig;
    rig.cfg.valves.sync.enabled = true;
    rig.cfg.valves.sync.maxDelayMs = 500;
    for (uint8_t i = 0; i < rig.cfg.valves.count; ++i) {
        rig.cfg.valves.items[i].type = ValveActuatorType::SERVO;
        rig.cfg.valves.items[i].releasedAngle = 40;
        rig.cfg.valves.items[i].pressedAngle = 88;
        rig.cfg.valves.items[i].speedDegPerSec = 900;    // ~65 ms with margin
    }
    rig.start();

    // Concert E4 is written F#4 on a Bb trumpet: valve 2 has to come down.
    // onMidi() only queues; the message is handled at the next block boundary.
    rig.engine.onMidi(MidiMessage::noteOn(1, 64, 100));
    rig.render(1);
    TEST_ASSERT_TRUE(rig.engine.lastAttackDelayMs() > 40);
    TEST_ASSERT_TRUE(rig.engine.lastAttackDelayMs() < 120);

    // 10 blocks is 27 ms at 48 kHz: the pistons are still moving.
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.0f, rig.peakOver(10));
    // By 100 ms they have arrived and the note speaks.
    TEST_ASSERT_TRUE(rig.peakOver(60) > 0.01f);
}

void test_engine_does_not_wait_when_the_fingering_is_unchanged(void) {
    Rig rig;
    rig.cfg.valves.sync.enabled = true;
    rig.cfg.valves.sync.onlyWhenFingeringChanges = true;
    rig.start();

    // Two notes that share a valve combination - the chart itself decides
    // which, so this stays true if the chart is edited.
    FingeringEngine& chart = rig.engine.fingering();
    int first = -1, second = -1;
    for (int n = 52; n <= 80 && second < 0; ++n) {
        const uint8_t a = chart.fingeringForMidiNote(static_cast<uint8_t>(n));
        if (a == kNoFingering) continue;
        for (int m = n + 1; m <= 84; ++m) {
            if (chart.fingeringForMidiNote(static_cast<uint8_t>(m)) == a) {
                first = n;
                second = m;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(second > 0, "the chart has no two notes sharing a fingering");

    rig.engine.onMidi(MidiMessage::noteOn(1, static_cast<uint8_t>(first), 100));
    rig.render(80);   // let the pistons arrive and the note speak

    // Slurring to a note on the same combination: nothing has to move, so the
    // second note must not be delayed at all.
    rig.engine.onMidi(MidiMessage::noteOn(1, static_cast<uint8_t>(second), 100));
    rig.render(1);
    TEST_ASSERT_EQUAL_UINT16(0, rig.engine.lastAttackDelayMs());
}

void test_engine_never_waits_when_synchronisation_is_off(void) {
    Rig rig;
    rig.cfg.valves.sync.enabled = false;
    rig.start();
    rig.engine.onMidi(MidiMessage::noteOn(1, 64, 100));
    rig.render(1);
    TEST_ASSERT_EQUAL_UINT16(0, rig.engine.lastAttackDelayMs());
    TEST_ASSERT_TRUE(rig.peakOver(20) > 0.005f);
}

void test_engine_cancels_a_pending_note_that_is_released_first(void) {
    Rig rig;
    rig.cfg.valves.sync.enabled = true;
    rig.cfg.valves.sync.maxDelayMs = 500;
    for (uint8_t i = 0; i < rig.cfg.valves.count; ++i) {
        rig.cfg.valves.items[i].type = ValveActuatorType::SERVO;
        rig.cfg.valves.items[i].speedDegPerSec = 120;    // deliberately slow
    }
    rig.start();

    rig.engine.onMidi(MidiMessage::noteOn(1, 64, 100));
    rig.render(2);
    // Released long before the pistons arrive: a staccato note shorter than
    // the travel must not fire the attack after the note is already over.
    rig.engine.onMidi(MidiMessage::noteOff(1, 64));
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.0f, rig.peakOver(200));
}

int main(int, char**) {
    initBoardCaps();
    UNITY_BEGIN();
    RUN_TEST(test_engine_is_silent_until_a_note_arrives);
    RUN_TEST(test_engine_produces_sound_on_a_note);
    RUN_TEST(test_engine_falls_silent_after_the_release);
    RUN_TEST(test_engine_mute_is_absolute);
    RUN_TEST(test_engine_panic_silences_immediately);
    RUN_TEST(test_engine_all_notes_off);
    RUN_TEST(test_engine_pitch_bend_range);
    RUN_TEST(test_engine_vibrato_runs_at_the_configured_rate);
    RUN_TEST(test_engine_vibrato_off_means_a_steady_pitch);
    RUN_TEST(test_engine_velocity_changes_the_level);
    RUN_TEST(test_engine_respects_note_priority);
    RUN_TEST(test_engine_transposes_written_pitch);
    RUN_TEST(test_engine_output_never_exceeds_the_safe_peak);
    RUN_TEST(test_engine_test_tone_and_sweep);
    RUN_TEST(test_engine_waits_for_the_pistons_before_speaking);
    RUN_TEST(test_engine_does_not_wait_when_the_fingering_is_unchanged);
    RUN_TEST(test_engine_never_waits_when_synchronisation_is_off);
    RUN_TEST(test_engine_cancels_a_pending_note_that_is_released_first);
    return UNITY_END();
}
