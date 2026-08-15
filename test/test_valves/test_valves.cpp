// ============================================================================
//  Valves: fingering table, servo travel limits, solenoid thermal guard,
//  All Notes Off and PANIC.
// ============================================================================
#include <unity.h>

#include "Mocks.h"
#include "valves/FingeringEngine.h"
#include "valves/ServoMotion.h"
#include "valves/SolenoidSafety.h"
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

    // Held continuously: the measured duty climbs to 100% and trips the limit.
    safety.request(true, 0);
    uint32_t t = 0;
    for (; t <= 20000; t += 50) {
        safety.update(t);
        if (safety.fault() == SolenoidFault::OVER_DUTY) break;
    }
    TEST_ASSERT_EQUAL(SolenoidFault::OVER_DUTY, safety.fault());
    TEST_ASSERT_EQUAL_UINT8(0, safety.dutyPercent());
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fingering_standard_chart);
    RUN_TEST(test_fingering_alternate_is_a_real_equivalent);
    RUN_TEST(test_fingering_range);
    RUN_TEST(test_fingering_bb_transposition_concert);
    RUN_TEST(test_fingering_bb_transposition_written);
    RUN_TEST(test_fingering_other_instruments);
    RUN_TEST(test_fingering_table_can_be_edited_and_reset);
    RUN_TEST(test_servo_reaches_the_target_and_never_overshoots);
    RUN_TEST(test_servo_speed_is_limited);
    RUN_TEST(test_servo_detaches_after_the_movement);
    RUN_TEST(test_servo_pulse_mapping_and_invert);
    RUN_TEST(test_solenoid_pull_in_then_hold);
    RUN_TEST(test_solenoid_releases_on_a_stuck_note);
    RUN_TEST(test_solenoid_cooldown_then_recovery);
    RUN_TEST(test_solenoid_duty_cycle_limit);
    RUN_TEST(test_solenoid_release_clears_the_drive_immediately);
    RUN_TEST(test_controller_mixed_servo_and_solenoid);
    RUN_TEST(test_controller_all_notes_off_releases_every_valve);
    RUN_TEST(test_controller_panic_parks_everything);
    RUN_TEST(test_controller_modes);
    RUN_TEST(test_controller_ignores_valves_it_does_not_have);
    RUN_TEST(test_mock_actuator_contract);
    return UNITY_END();
}
