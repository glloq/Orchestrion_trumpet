// ============================================================================
//  Valves: fingering table, servo travel limits, solenoid thermal guard,
//  All Notes Off and PANIC.
// ============================================================================
#include <unity.h>

#include "Mocks.h"
#include "valves/FingeringEngine.h"
#include "valves/ServoMotion.h"
#include "valves/SolenoidSafety.h"
#include "valves/ValveTiming.h"
#include "valves/ValveController.h"

using namespace ot;
using namespace ot::mock;

namespace {
constexpr uint8_t V1 = 0x01;
constexpr uint8_t V2 = 0x02;
constexpr uint8_t V3 = 0x04;
}  // namespace

void setUp() { hostSetMillis(0); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Fingering
// ---------------------------------------------------------------------------
void test_fingering_standard_chart(void) {
    // Written pitch -> valve combination, from the standard trumpet chart.
    TEST_ASSERT_EQUAL_UINT8(0, FingeringEngine::defaultPrimary(60));            // C4  open
    TEST_ASSERT_EQUAL_UINT8(V1 | V2 | V3, FingeringEngine::defaultPrimary(61)); // C#4 1-2-3
    TEST_ASSERT_EQUAL_UINT8(V1 | V3, FingeringEngine::defaultPrimary(62));      // D4  1-3
    TEST_ASSERT_EQUAL_UINT8(V2 | V3, FingeringEngine::defaultPrimary(63));      // Eb4 2-3
    TEST_ASSERT_EQUAL_UINT8(V1 | V2, FingeringEngine::defaultPrimary(64));      // E4  1-2
    TEST_ASSERT_EQUAL_UINT8(V1, FingeringEngine::defaultPrimary(65));           // F4  1
    TEST_ASSERT_EQUAL_UINT8(V2, FingeringEngine::defaultPrimary(66));           // F#4 2
    TEST_ASSERT_EQUAL_UINT8(0, FingeringEngine::defaultPrimary(67));            // G4  open
    TEST_ASSERT_EQUAL_UINT8(0, FingeringEngine::defaultPrimary(72));            // C5  open
    TEST_ASSERT_EQUAL_UINT8(0, FingeringEngine::defaultPrimary(76));            // E5  open
    TEST_ASSERT_EQUAL_UINT8(0, FingeringEngine::defaultPrimary(79));            // G5  open
    TEST_ASSERT_EQUAL_UINT8(0, FingeringEngine::defaultPrimary(84));            // C6  open
}

void test_fingering_alternate_is_a_real_equivalent(void) {
    // Valve 3 is the same tube length as valves 1+2, so those two grips are
    // genuinely interchangeable; nothing else is.
    TEST_ASSERT_EQUAL_UINT8(V3, FingeringEngine::defaultAlternate(64));   // E4: 1-2 <-> 3
    TEST_ASSERT_EQUAL_UINT8(kNoFingering, FingeringEngine::defaultAlternate(60));
    TEST_ASSERT_EQUAL_UINT8(kNoFingering, FingeringEngine::defaultAlternate(62));
}

void test_fingering_range(void) {
    TEST_ASSERT_TRUE(FingeringEngine::inStandardRange(54));    // F#3, lowest standard note
    TEST_ASSERT_TRUE(FingeringEngine::inStandardRange(84));    // C6
    TEST_ASSERT_FALSE(FingeringEngine::inStandardRange(53));
    TEST_ASSERT_FALSE(FingeringEngine::inStandardRange(85));
    // Outside the chart the pattern still repeats by octave rather than
    // leaving a hole.
    TEST_ASSERT_EQUAL_UINT8(FingeringEngine::defaultPrimary(60),
                            FingeringEngine::defaultPrimary(48));
}

void test_fingering_bb_transposition_concert(void) {
    InstrumentConfig cfg;
    cfg.type = InstrumentType::BB_TRUMPET;
    cfg.pitchMode = PitchInterpretation::CONCERT;
    FingeringEngine engine;
    engine.configure(cfg);

    TEST_ASSERT_EQUAL_INT8(2, engine.transposeSemitones());
    // Concert Bb3 (58) is written C4 on a Bb trumpet: open.
    TEST_ASSERT_EQUAL_INT(60, engine.writtenNote(58));
    TEST_ASSERT_EQUAL_UINT8(0, engine.fingeringForMidiNote(58));
    // The sound engine plays the concert pitch it was given.
    TEST_ASSERT_EQUAL_INT(58, engine.soundingNote(58));
}

void test_fingering_bb_transposition_written(void) {
    InstrumentConfig cfg;
    cfg.type = InstrumentType::BB_TRUMPET;
    cfg.pitchMode = PitchInterpretation::WRITTEN;
    FingeringEngine engine;
    engine.configure(cfg);

    // Written C4 arrives as MIDI 60: open fingering, sounding Bb3.
    TEST_ASSERT_EQUAL_INT(60, engine.writtenNote(60));
    TEST_ASSERT_EQUAL_UINT8(0, engine.fingeringForMidiNote(60));
    TEST_ASSERT_EQUAL_INT(58, engine.soundingNote(60));
}

void test_fingering_other_instruments(void) {
    InstrumentConfig cfg;
    FingeringEngine engine;

    cfg.type = InstrumentType::C_TRUMPET;
    engine.configure(cfg);
    TEST_ASSERT_EQUAL_INT8(0, engine.transposeSemitones());

    cfg.type = InstrumentType::EB_TRUMPET;
    engine.configure(cfg);
    TEST_ASSERT_EQUAL_INT8(-3, engine.transposeSemitones());

    cfg.type = InstrumentType::CUSTOM;
    cfg.customTransposeSemitones = 5;
    engine.configure(cfg);
    TEST_ASSERT_EQUAL_INT8(5, engine.transposeSemitones());
}

void test_fingering_table_can_be_edited_and_reset(void) {
    InstrumentConfig cfg;
    FingeringEngine engine;
    engine.configure(cfg);

    TEST_ASSERT_TRUE(engine.setFingering(60, V1 | V3, V2));
    TEST_ASSERT_EQUAL_UINT8(V1 | V3, engine.primary(60));
    TEST_ASSERT_EQUAL_UINT8(V2, engine.alternate(60));

    // A mask cannot address a valve that does not exist.
    TEST_ASSERT_FALSE(engine.setFingering(60, 0x10, kNoFingering));

    engine.resetToDefault();
    TEST_ASSERT_EQUAL_UINT8(0, engine.primary(60));
}

// An edited chart has to survive a reboot, and only the edits may be stored:
// writing all 128 notes out would fossilise the standard chart in the file.
void test_fingering_overrides_are_only_the_differences(void) {
    InstrumentConfig cfg;
    FingeringEngine engine;
    engine.configure(cfg);

    FingeringOverride items[kMaxFingeringOverrides];
    bool overflowed = true;
    TEST_ASSERT_EQUAL_UINT8(0, engine.collectOverrides(items, kMaxFingeringOverrides, &overflowed));
    TEST_ASSERT_FALSE(overflowed);

    engine.setFingering(60, V1 | V3, V2);
    engine.setFingering(72, kNoFingering, kNoFingering);
    const uint8_t n = engine.collectOverrides(items, kMaxFingeringOverrides, &overflowed);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_FALSE(overflowed);
    TEST_ASSERT_EQUAL_UINT8(60, items[0].written);
    TEST_ASSERT_EQUAL_UINT8(V1 | V3, items[0].primary);
    TEST_ASSERT_EQUAL_UINT8(V2, items[0].alternate);
    TEST_ASSERT_EQUAL_UINT8(kNoFingeringMask, items[1].primary);

    // Reboot: the standard chart, then the stored edits on top of it.
    cfg.fingeringOverrideCount = n;
    for (uint8_t i = 0; i < n; ++i) cfg.fingeringOverrides[i] = items[i];
    FingeringEngine afterBoot;
    afterBoot.configure(cfg);
    TEST_ASSERT_EQUAL_UINT8(V1 | V3, afterBoot.primary(60));
    TEST_ASSERT_EQUAL_UINT8(V2, afterBoot.alternate(60));
    TEST_ASSERT_EQUAL_UINT8(kNoFingering, afterBoot.primary(72));
    // Everything else is still the factory chart.
    TEST_ASSERT_EQUAL_UINT8(FingeringEngine::defaultPrimary(64), afterBoot.primary(64));
}

void test_fingering_overrides_report_an_overflow(void) {
    InstrumentConfig cfg;
    FingeringEngine engine;
    engine.configure(cfg);

    // More edited notes than the configuration can carry.
    uint8_t edited = 0;
    for (int n = 40; n <= 96 && edited < kMaxFingeringOverrides + 3; ++n) {
        const uint8_t want = static_cast<uint8_t>(FingeringEngine::defaultPrimary(n) ^ V3);
        if (want & 0xF0) continue;
        engine.setFingering(static_cast<uint8_t>(n), want, kNoFingering);
        ++edited;
    }

    FingeringOverride items[kMaxFingeringOverrides];
    bool overflowed = false;
    const uint8_t n = engine.collectOverrides(items, kMaxFingeringOverrides, &overflowed);
    TEST_ASSERT_EQUAL_UINT8(kMaxFingeringOverrides, n);
    TEST_ASSERT_TRUE(overflowed);
}

// ---------------------------------------------------------------------------
// Servo motion
// ---------------------------------------------------------------------------
void test_servo_reaches_the_target_and_never_overshoots(void) {
    ValveConfig cfg;
    cfg.releasedAngle = 40;
    cfg.pressedAngle = 88;
    cfg.speedDegPerSec = 900;
    cfg.accelDegPerSec2 = 6000;

    ServoMotion motion;
    motion.configure(cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, motion.angle());

    motion.setTarget(true);
    TEST_ASSERT_TRUE(motion.isMoving());
    for (uint16_t step = 0; step < 500 && motion.isMoving(); ++step) {
        motion.update(1);
        // The commanded angle must never leave the calibrated travel.
        TEST_ASSERT_TRUE(motion.angle() >= 40.0f - 0.001f);
        TEST_ASSERT_TRUE(motion.angle() <= 88.0f + 0.001f);
    }
    TEST_ASSERT_FALSE(motion.isMoving());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 88.0f, motion.angle());
}

void test_servo_speed_is_limited(void) {
    ValveConfig cfg;
    cfg.releasedAngle = 0;
    cfg.pressedAngle = 180;
    cfg.speedDegPerSec = 100;      // slow on purpose
    cfg.accelDegPerSec2 = 100000;  // acceleration is not the limit here

    ServoMotion motion;
    motion.configure(cfg);
    motion.setTarget(true);
    motion.update(100);   // 100 ms at 100 deg/s -> at most 10 degrees
    TEST_ASSERT_TRUE(motion.angle() <= 11.0f);
    TEST_ASSERT_TRUE(motion.isMoving());
}

void test_servo_detaches_after_the_movement(void) {
    ValveConfig cfg;
    cfg.releasedAngle = 40;
    cfg.pressedAngle = 88;
    cfg.detachAfterMove = true;
    cfg.detachDelayMs = 200;

    ServoMotion motion;
    motion.configure(cfg);
    motion.setTarget(true);
    for (uint16_t i = 0; i < 500 && motion.isMoving(); ++i) motion.update(1);

    TEST_ASSERT_FALSE(motion.shouldDetach());   // the delay has not elapsed
    motion.update(250);
    TEST_ASSERT_TRUE(motion.shouldDetach());

    // Moving again re-arms the servo.
    motion.setTarget(false);
    TEST_ASSERT_FALSE(motion.shouldDetach());
}

void test_servo_pulse_mapping_and_invert(void) {
    ValveConfig cfg;
    cfg.minPulseUs = 500;
    cfg.maxPulseUs = 2400;
    cfg.releasedAngle = 0;
    cfg.pressedAngle = 180;

    ServoMotion motion;
    motion.configure(cfg);
    motion.forceTo(false);
    TEST_ASSERT_UINT16_WITHIN(2, 500, motion.pulseUs());
    motion.forceTo(true);
    TEST_ASSERT_UINT16_WITHIN(2, 2400, motion.pulseUs());

    cfg.invert = true;
    ServoMotion inverted;
    inverted.configure(cfg);
    inverted.forceTo(true);
    TEST_ASSERT_UINT16_WITHIN(2, 500, inverted.pulseUs());
}

// ---------------------------------------------------------------------------
// Solenoid thermal guard
// ---------------------------------------------------------------------------
void test_solenoid_pull_in_then_hold(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 50;
    cfg.holdPwm = 35;
    cfg.maxOnMs = 5000;
    cfg.cooldownMs = 3000;

    SolenoidSafety safety;
    safety.configure(cfg);

    safety.request(true, 0);
    TEST_ASSERT_EQUAL_UINT8(100, safety.dutyPercent());

    safety.update(20);
    TEST_ASSERT_EQUAL_UINT8(100, safety.dutyPercent());   // still pulling in

    safety.update(60);
    TEST_ASSERT_EQUAL_UINT8(35, safety.dutyPercent());    // dropped to hold

    safety.request(false, 100);
    TEST_ASSERT_EQUAL_UINT8(0, safety.dutyPercent());
    TEST_ASSERT_EQUAL(SolenoidFault::NONE, safety.fault());
}

void test_solenoid_releases_on_a_stuck_note(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 50;
    cfg.holdPwm = 35;
    cfg.maxOnMs = 1000;
    cfg.cooldownMs = 2000;

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);

    // The MIDI note never ends: the guard must fire anyway.
    for (uint32_t t = 10; t <= 1200; t += 10) safety.update(t);

    TEST_ASSERT_EQUAL_UINT8(0, safety.dutyPercent());
    TEST_ASSERT_EQUAL(SolenoidFault::OVER_TIME, safety.fault());
    TEST_ASSERT_TRUE(safety.inCooldown());
    // Still held down from the MIDI point of view, but not energised.
    TEST_ASSERT_TRUE(safety.pressedRequested());
    TEST_ASSERT_FALSE(safety.isEnergised());
}

void test_solenoid_cooldown_then_recovery(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 20;
    cfg.holdPwm = 30;
    cfg.maxOnMs = 500;
    cfg.cooldownMs = 1000;
    cfg.maxDutyPercent = 100;   // isolate the maximum-on-time path

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);
    for (uint32_t t = 10; t <= 600; t += 10) safety.update(t);
    TEST_ASSERT_EQUAL(SolenoidFault::OVER_TIME, safety.fault());

    // During the cooldown the coil stays off even though the note is held.
    safety.update(1000);
    TEST_ASSERT_EQUAL_UINT8(0, safety.dutyPercent());

    // Once it expires and the note is still held, one fresh pull-in is allowed.
    safety.update(1700);
    TEST_ASSERT_EQUAL_UINT8(100, safety.dutyPercent());
}

void test_solenoid_duty_cycle_limit(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 10;
    cfg.holdPwm = 40;
    cfg.maxOnMs = 100000;      // do not let the on-time guard fire
    cfg.cooldownMs = 500;
    cfg.maxDutyPercent = 30;

    SolenoidSafety safety;
    safety.configure(cfg);

    // Held continuously at 40% against a 30% ceiling: the thermal load really
    // is above what the coil was allowed, so this must still trip.
    safety.request(true, 0);
    uint32_t t = 0;
    for (; t <= 20000; t += 50) {
        safety.update(t);
        if (safety.fault() == SolenoidFault::OVER_DUTY) break;
    }
    TEST_ASSERT_EQUAL(SolenoidFault::OVER_DUTY, safety.fault());
    TEST_ASSERT_EQUAL_UINT8(0, safety.dutyPercent());
}

// The hold level exists precisely so a long note does not have to trip the
// duty ceiling.  Counting a coil held at 35% as 100% duty made every note
// longer than two seconds fail with OVER_DUTY.
void test_solenoid_hold_level_is_not_counted_as_full_duty(void) {
    ValveConfig cfg;            // the shipped defaults
    cfg.pullInPwm = 100;
    cfg.pullInMs = 50;
    cfg.holdPwm = 35;
    cfg.maxDutyPercent = 60;
    cfg.maxOnMs = 0;            // isolate the duty guard from the time guard

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);
    for (uint32_t t = 0; t <= 30000; t += 20) {
        safety.update(t);
        TEST_ASSERT_EQUAL_MESSAGE(SolenoidFault::NONE, safety.fault(),
                                  "a note held at the hold level must not trip the duty guard");
    }
    TEST_ASSERT_EQUAL_UINT8(35, safety.dutyPercent());
    // Equivalent continuous duty settles at the hold level, not at 100%.
    TEST_ASSERT_UINT8_WITHIN(4, 35, safety.measuredDutyPercent());
}

// The measured figure is a *thermal* duty: 50% PWM heats a coil like 50% of
// the drive, not like being on all the time.
void test_solenoid_measured_duty_follows_the_pwm_level(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 50;
    cfg.pullInMs = 60000;       // stay at the pull-in level for the whole test
    cfg.holdPwm = 50;
    cfg.maxDutyPercent = 0;     // guard off
    cfg.maxOnMs = 0;

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);
    for (uint32_t t = 0; t <= 20000; t += 20) safety.update(t);
    TEST_ASSERT_UINT8_WITHIN(3, 50, safety.measuredDutyPercent());
}

// The observation window slides every few seconds.  It used to take the
// continuous-ON timestamp with it, so a solenoid configured at the sanitiser's
// own upper bound of 20 s never reached its limit at all.
void test_solenoid_max_on_time_survives_the_window_sliding(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 50;
    cfg.holdPwm = 35;
    cfg.maxOnMs = 20000;        // the largest value the validator will store
    cfg.maxDutyPercent = 0;     // isolate the time guard
    cfg.cooldownMs = 1000;

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);

    uint32_t trippedAt = 0;
    for (uint32_t t = 0; t <= 40000; t += 20) {
        safety.update(t);
        if (safety.fault() == SolenoidFault::OVER_TIME) {
            trippedAt = t;
            break;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(SolenoidFault::OVER_TIME, safety.fault(),
                              "a coil held past maxOnMs must always be released");
    TEST_ASSERT_UINT32_WITHIN(100, 20000, trippedAt);
    TEST_ASSERT_EQUAL_UINT8(0, safety.dutyPercent());
}

// The same, one step further out: the guard must not care how many times the
// window rolled over.
void test_solenoid_max_on_time_beyond_the_window(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 50;
    cfg.holdPwm = 35;
    cfg.maxOnMs = 45000;
    cfg.maxDutyPercent = 0;

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);
    uint32_t trippedAt = 0;
    for (uint32_t t = 0; t <= 90000; t += 20) {
        safety.update(t);
        if (safety.fault() == SolenoidFault::OVER_TIME) {
            trippedAt = t;
            break;
        }
    }
    TEST_ASSERT_EQUAL(SolenoidFault::OVER_TIME, safety.fault());
    TEST_ASSERT_UINT32_WITHIN(100, 45000, trippedAt);
}

// Pull-in raises the level; it is not a second press and must not restart the
// continuous clock either.
void test_solenoid_pull_in_to_hold_does_not_restart_the_clock(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 50;
    cfg.holdPwm = 35;
    cfg.maxOnMs = 2000;
    cfg.maxDutyPercent = 0;

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);
    safety.update(100);
    TEST_ASSERT_EQUAL_UINT8(35, safety.dutyPercent());      // already holding
    TEST_ASSERT_UINT32_WITHIN(5, 100, safety.continuousOnMs(100));

    uint32_t trippedAt = 0;
    for (uint32_t t = 100; t <= 6000; t += 20) {
        safety.update(t);
        if (safety.fault() == SolenoidFault::OVER_TIME) {
            trippedAt = t;
            break;
        }
    }
    TEST_ASSERT_UINT32_WITHIN(60, 2000, trippedAt);
}

void test_solenoid_release_clears_the_drive_immediately(void) {
    ValveConfig cfg;
    cfg.pullInPwm = 100;
    cfg.pullInMs = 50;
    cfg.holdPwm = 35;

    SolenoidSafety safety;
    safety.configure(cfg);
    safety.request(true, 0);
    safety.update(10);
    TEST_ASSERT_TRUE(safety.isEnergised());
    safety.request(false, 20);
    TEST_ASSERT_FALSE(safety.isEnergised());
    TEST_ASSERT_FALSE(safety.pressedRequested());
}

// ---------------------------------------------------------------------------
// Valve controller: fingering, All Notes Off, panic, mixed configuration
// ---------------------------------------------------------------------------
namespace {

ValvesConfig mixedConfig() {
    ValvesConfig cfg;
    cfg.count = 3;
    cfg.mode = ValveMode::AUTO;
    cfg.items[0].type = ValveActuatorType::SERVO;
    cfg.items[0].driver = ServoDriverType::ESP32_PWM;
    cfg.items[0].gpio = 15;
    cfg.items[1].type = ValveActuatorType::SERVO;
    cfg.items[1].driver = ServoDriverType::ESP32_PWM;
    cfg.items[1].gpio = 16;
    cfg.items[2].type = ValveActuatorType::SOLENOID;
    cfg.items[2].gpio = 4;
    cfg.items[2].maxOnMs = 5000;
    return cfg;
}

InstrumentConfig writtenPitch() {
    InstrumentConfig cfg;
    cfg.type = InstrumentType::BB_TRUMPET;
    cfg.pitchMode = PitchInterpretation::WRITTEN;
    return cfg;
}

}  // namespace

void test_controller_mixed_servo_and_solenoid(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());

    // Written E4 is 1-2 on a trumpet: valves 1 and 2 down, valve 3 up.
    controller.onMidi(MidiMessage::noteOn(1, 64, 100));
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());
    TEST_ASSERT_TRUE(controller.status(0).pressed);
    TEST_ASSERT_TRUE(controller.status(1).pressed);
    TEST_ASSERT_FALSE(controller.status(2).pressed);
    // The two technologies really are in use at the same time.
    TEST_ASSERT_EQUAL(ValveActuatorType::SERVO, controller.status(0).type);
    TEST_ASSERT_EQUAL(ValveActuatorType::SOLENOID, controller.status(2).type);

    // Written C#4 is 1-2-3: the solenoid comes down too.
    controller.onMidi(MidiMessage::noteOff(1, 64));
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());
    TEST_ASSERT_TRUE(controller.status(2).pressed);
}

void test_controller_all_notes_off_releases_every_valve(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());

    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());

    controller.onMidi(MidiMessage::controlChange(1, cc::AllNotesOff, 0));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());
    for (uint8_t i = 0; i < 3; ++i) TEST_ASSERT_FALSE(controller.status(i).pressed);
}

void test_controller_panic_parks_everything(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_NOT_EQUAL(0, controller.currentMask());

    controller.panic();
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());
    for (uint8_t i = 0; i < 3; ++i) TEST_ASSERT_FALSE(controller.status(i).pressed);

    // After a panic the valves ignore MIDI until they are released.
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());

    controller.enableAfterPanic();
    controller.onMidi(MidiMessage::noteOff(1, 61));
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());
}

void test_controller_modes(void) {
    ValveController controller;
    ValvesConfig cfg = mixedConfig();
    cfg.mode = ValveMode::MANUAL;
    controller.begin(cfg, writtenPitch());

    // MANUAL ignores MIDI notes entirely.
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());
    TEST_ASSERT_TRUE(controller.manualSet(1, true));
    TEST_ASSERT_TRUE(controller.status(1).pressed);

    // MIDI_CC binds each valve to its own controller number.
    controller.setMode(ValveMode::MIDI_CC);
    controller.onMidi(MidiMessage::controlChange(1, cfg.items[0].ccNumber, 127));
    TEST_ASSERT_TRUE(controller.status(0).pressed);
    controller.onMidi(MidiMessage::controlChange(1, cfg.items[0].ccNumber, 0));
    TEST_ASSERT_FALSE(controller.status(0).pressed);

    // DISABLED parks them and stops listening.
    controller.setMode(ValveMode::OFF);
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());
}

void test_test_pulse_restores_the_played_state(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());

    // Written E4 is 1-2: valve 1 down, valve 3 up.
    controller.onMidi(MidiMessage::noteOn(1, 64, 100));
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());

    // Pulsing valve 3 while the note is held must not disturb valves 1 and 2,
    // and valve 3 must go back up when the pulse ends.
    hostSetMillis(1000);
    TEST_ASSERT_TRUE(controller.testPulse(2, 200));
    TEST_ASSERT_TRUE(controller.status(2).pressed);
    hostSetMillis(1300);
    controller.update();
    TEST_ASSERT_FALSE(controller.status(2).pressed);
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());

    // Pulsing a valve the note actually needs must leave it down afterwards.
    hostSetMillis(2000);
    controller.testPulse(0, 200);
    hostSetMillis(2300);
    controller.update();
    TEST_ASSERT_TRUE(controller.status(0).pressed);
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());
}

// CC 64. The sound engine keeps a sustained note speaking; if the pistons come
// straight back up, that note finishes through the open bore — the two engines
// must agree about the current note, always.
void test_controller_sustain_holds_the_fingering(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());

    controller.onMidi(MidiMessage::controlChange(1, cc::Sustain, 127));
    // Written C#4 is 1-2-3.
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());

    // The key comes up; the pedal is down, so the pistons stay where they are.
    controller.onMidi(MidiMessage::noteOff(1, 61));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.desiredMask());

    // Releasing the pedal releases them.
    controller.onMidi(MidiMessage::controlChange(1, cc::Sustain, 0));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());
}

void test_controller_sustain_yields_to_a_new_note(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());

    controller.onMidi(MidiMessage::controlChange(1, cc::Sustain, 127));
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));    // 1-2-3
    controller.onMidi(MidiMessage::noteOff(1, 61));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());

    // A new note wins over what the pedal is holding: written E4 is 1-2.
    controller.onMidi(MidiMessage::noteOn(1, 64, 100));
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());

    // Its own key comes up, and now *that* fingering is what the pedal holds.
    controller.onMidi(MidiMessage::noteOff(1, 64));
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());

    // A key still down outlives the pedal.
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    controller.onMidi(MidiMessage::controlChange(1, cc::Sustain, 0));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());
    controller.onMidi(MidiMessage::noteOff(1, 61));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());
}

void test_controller_sustain_never_survives_a_panic_message(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());

    controller.onMidi(MidiMessage::controlChange(1, cc::Sustain, 127));
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    controller.onMidi(MidiMessage::noteOff(1, 61));
    TEST_ASSERT_EQUAL_UINT8(0x07, controller.currentMask());

    controller.onMidi(MidiMessage::controlChange(1, cc::AllNotesOff, 0));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());

    // And the pedal must not still be considered down afterwards.
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    controller.onMidi(MidiMessage::noteOff(1, 61));
    TEST_ASSERT_EQUAL_UINT8(0, controller.currentMask());
}

// A test pulse used to be cleaned up against `desiredMask()`, which answered 0
// while the pedal was holding a note: pulsing any valve lifted the sustained
// fingering for good.
void test_controller_sustained_fingering_survives_a_test_pulse(void) {
    ValveController controller;
    controller.begin(mixedConfig(), writtenPitch());

    controller.onMidi(MidiMessage::controlChange(1, cc::Sustain, 127));
    controller.onMidi(MidiMessage::noteOn(1, 64, 100));    // 1-2
    controller.onMidi(MidiMessage::noteOff(1, 64));
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());

    hostSetMillis(4000);
    TEST_ASSERT_TRUE(controller.testPulse(2, 200));
    hostSetMillis(4300);
    controller.update();
    TEST_ASSERT_FALSE(controller.status(2).pressed);
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());
}

void test_controller_ignores_valves_it_does_not_have(void) {
    ValveController controller;
    ValvesConfig cfg = mixedConfig();
    cfg.count = 2;   // a two-valve instrument
    controller.begin(cfg, writtenPitch());

    // C#4 asks for 1-2-3; only the two fitted valves may move.
    controller.onMidi(MidiMessage::noteOn(1, 61, 100));
    TEST_ASSERT_EQUAL_UINT8(0x03, controller.currentMask());
}

// ---------------------------------------------------------------------------
// Mock actuator, used as the reference implementation of the interface
// ---------------------------------------------------------------------------
void test_mock_actuator_contract(void) {
    MockValveActuator actuator;
    TEST_ASSERT_TRUE(actuator.begin());
    actuator.setValve(0, true);
    TEST_ASSERT_TRUE(actuator.isPressed(0));
    actuator.emergencyStop();
    TEST_ASSERT_FALSE(actuator.isPressed(0));
    actuator.setValve(0, true);
    TEST_ASSERT_FALSE(actuator.isPressed(0));   // refused while stopped
    TEST_ASSERT_EQUAL_UINT32(1, actuator.ignoredWhileStopped);
    actuator.enable();
    actuator.setValve(0, true);
    TEST_ASSERT_TRUE(actuator.isPressed(0));
}

// ---------------------------------------------------------------------------
// Valve -> audio synchronisation timing
// ---------------------------------------------------------------------------
void test_settle_time_follows_the_servo_travel(void) {
    ValveConfig v;
    v.type = ValveActuatorType::SERVO;
    v.releasedAngle = 40;
    v.pressedAngle = 88;
    v.speedDegPerSec = 900;
    // 48 deg at 900 deg/s is 53 ms, plus the mechanical margin.
    TEST_ASSERT_UINT16_WITHIN(4, 65, valveSettleMs(v));

    v.speedDegPerSec = 300;     // a slow hobby servo
    TEST_ASSERT_UINT16_WITHIN(4, 172, valveSettleMs(v));

    v.measuredSettleMs = 41;    // a bench figure always wins
    TEST_ASSERT_EQUAL_UINT16(41, valveSettleMs(v));
}

void test_settle_time_of_a_solenoid_is_its_pull_in(void) {
    ValveConfig v;
    v.type = ValveActuatorType::SOLENOID;
    v.pullInMs = 50;
    TEST_ASSERT_UINT16_WITHIN(4, 62, valveSettleMs(v));

    v.type = ValveActuatorType::OFF;
    TEST_ASSERT_EQUAL_UINT16(0, valveSettleMs(v));
}

void test_attack_delay_waits_for_the_slowest_moving_valve(void) {
    ValvesConfig valves;
    valves.count = 3;
    valves.mode = ValveMode::AUTO;
    for (uint8_t i = 0; i < 3; ++i) {
        valves.items[i].type = ValveActuatorType::SERVO;
        valves.items[i].releasedAngle = 40;
        valves.items[i].pressedAngle = 88;
        valves.items[i].speedDegPerSec = 900;
    }
    valves.items[2].speedDegPerSec = 200;   // valve 3 is the slow one

    // Only valve 1 moves: the fast time applies.
    TEST_ASSERT_UINT16_WITHIN(5, 65, noteAttackDelayMs(valves, 0b000, 0b001));
    // Valve 3 moves as well: the note waits for it, and for it alone - the
    // valves move together, so the delays do not add up.
    TEST_ASSERT_UINT16_WITHIN(6, 120, noteAttackDelayMs(valves, 0b000, 0b101));
    // Same fingering: nothing has to move, so nothing waits.
    TEST_ASSERT_EQUAL_UINT16(0, noteAttackDelayMs(valves, 0b101, 0b101));
}

void test_attack_delay_respects_the_policy(void) {
    ValvesConfig valves;
    valves.count = 3;
    valves.mode = ValveMode::AUTO;
    for (uint8_t i = 0; i < 3; ++i) {
        valves.items[i].type = ValveActuatorType::SERVO;
        valves.items[i].speedDegPerSec = 200;    // ~252 ms of travel
    }

    // The ceiling is a hard one: a badly configured servo cannot make every
    // note arrive a quarter of a second late.
    valves.sync.maxDelayMs = 80;
    TEST_ASSERT_EQUAL_UINT16(80, noteAttackDelayMs(valves, 0, 0b001));

    valves.sync.maxDelayMs = 500;
    valves.sync.trimMs = -40;
    const uint16_t trimmed = noteAttackDelayMs(valves, 0, 0b001);
    valves.sync.trimMs = 0;
    TEST_ASSERT_EQUAL_UINT16(trimmed + 40, noteAttackDelayMs(valves, 0, 0b001));

    valves.sync.enabled = false;
    TEST_ASSERT_EQUAL_UINT16(0, noteAttackDelayMs(valves, 0, 0b001));

    // Nothing is following the notes in MANUAL, so there is nothing to sync to.
    valves.sync.enabled = true;
    valves.mode = ValveMode::MANUAL;
    TEST_ASSERT_EQUAL_UINT16(0, noteAttackDelayMs(valves, 0, 0b001));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fingering_standard_chart);
    RUN_TEST(test_fingering_alternate_is_a_real_equivalent);
    RUN_TEST(test_fingering_range);
    RUN_TEST(test_fingering_bb_transposition_concert);
    RUN_TEST(test_fingering_bb_transposition_written);
    RUN_TEST(test_fingering_other_instruments);
    RUN_TEST(test_fingering_table_can_be_edited_and_reset);
    RUN_TEST(test_fingering_overrides_are_only_the_differences);
    RUN_TEST(test_fingering_overrides_report_an_overflow);
    RUN_TEST(test_servo_reaches_the_target_and_never_overshoots);
    RUN_TEST(test_servo_speed_is_limited);
    RUN_TEST(test_servo_detaches_after_the_movement);
    RUN_TEST(test_servo_pulse_mapping_and_invert);
    RUN_TEST(test_solenoid_pull_in_then_hold);
    RUN_TEST(test_solenoid_releases_on_a_stuck_note);
    RUN_TEST(test_solenoid_cooldown_then_recovery);
    RUN_TEST(test_solenoid_duty_cycle_limit);
    RUN_TEST(test_solenoid_release_clears_the_drive_immediately);
    RUN_TEST(test_solenoid_hold_level_is_not_counted_as_full_duty);
    RUN_TEST(test_solenoid_measured_duty_follows_the_pwm_level);
    RUN_TEST(test_solenoid_max_on_time_survives_the_window_sliding);
    RUN_TEST(test_solenoid_max_on_time_beyond_the_window);
    RUN_TEST(test_solenoid_pull_in_to_hold_does_not_restart_the_clock);
    RUN_TEST(test_controller_mixed_servo_and_solenoid);
    RUN_TEST(test_controller_all_notes_off_releases_every_valve);
    RUN_TEST(test_controller_panic_parks_everything);
    RUN_TEST(test_controller_modes);
    RUN_TEST(test_test_pulse_restores_the_played_state);
    RUN_TEST(test_controller_sustain_holds_the_fingering);
    RUN_TEST(test_controller_sustain_yields_to_a_new_note);
    RUN_TEST(test_controller_sustain_never_survives_a_panic_message);
    RUN_TEST(test_controller_sustained_fingering_survives_a_test_pulse);
    RUN_TEST(test_controller_ignores_valves_it_does_not_have);
    RUN_TEST(test_mock_actuator_contract);
    RUN_TEST(test_settle_time_follows_the_servo_travel);
    RUN_TEST(test_settle_time_of_a_solenoid_is_its_pull_in);
    RUN_TEST(test_attack_delay_waits_for_the_slowest_moving_valve);
    RUN_TEST(test_attack_delay_respects_the_policy);
    return UNITY_END();
}
