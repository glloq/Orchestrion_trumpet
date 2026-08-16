// ============================================================================
//  Configuration: parsing, round trip, migration and validation.
// ============================================================================
#include <unity.h>

#include "config/BoardCaps.h"
#include "config/ConfigManager.h"
#include "config/ConfigMigration.h"
#include "config/ConfigSerializer.h"
#include <cstring>
#include "config/ConfigValidator.h"
#include "config/Presets.h"

using namespace ot;

namespace {

BoardCapabilities makeS3Caps() {
    BoardCapabilities caps = boardCaps();   // host build reports the S3 feature set
    return caps;
}

BoardCapabilities makeClassicCaps() {
    static const uint8_t reserved[] = {6, 7, 8, 9, 10, 11};
    static const uint8_t inputOnly[] = {34, 35, 36, 37, 38, 39};
    BoardCapabilities caps;
    caps.board = BoardType::ESP32;
    caps.chipName = "ESP32";
    caps.gpioMax = 39;
    caps.hasNativeUsb = false;
    caps.hasInternalDac = true;
    caps.reservedPins = reserved;
    caps.reservedPinCount = sizeof(reserved);
    caps.inputOnlyPins = inputOnly;
    caps.inputOnlyPinCount = sizeof(inputOnly);
    return caps;
}

bool hasIssue(const ValidationReport& report, Severity severity, const char* fieldFragment) {
    for (uint8_t i = 0; i < report.count(); ++i) {
        if (report.issue(i).severity != severity) continue;
        if (!fieldFragment) return true;
        if (strstr(report.issue(i).field, fieldFragment) != nullptr) return true;
    }
    return false;
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Parsing / serialisation
// ---------------------------------------------------------------------------
void test_defaults_are_valid(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    for (uint8_t i = 0; i < report.count(); ++i) {
        if (report.issue(i).severity == Severity::ERROR) {
            TEST_FAIL_MESSAGE(report.issue(i).message);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(kConfigSchemaVersion, cfg.schemaVersion);
}

void test_round_trip_preserves_values(void) {
    InstrumentConfiguration original;
    ConfigManager::makeDefaults(original);
    original.audio.sampleRate = 44100;
    original.voicing.engine = SynthEngineType::WAVETABLE;
    original.voicing.envelope.attackMs = 7.5f;
    original.instrument.notePriority = NotePriority::HIGHEST;
    original.valves.count = 4;
    original.valves.items[2].type = ValveActuatorType::SOLENOID;
    original.valves.items[2].maxOnMs = 1500;
    original.valves.items[3].type = ValveActuatorType::SERVO;
    original.valves.items[3].driver = ServoDriverType::PCA9685;
    original.valves.items[3].channel = 7;
    copyString(original.system.deviceName, kNameLen, "Trompette de test");

    static char buffer[8192];
    const size_t length = serializeConfig(original, buffer, sizeof(buffer));
    TEST_ASSERT_GREATER_THAN(200, length);

    InstrumentConfiguration restored;
    TEST_ASSERT_TRUE(deserializeConfig(buffer, length, restored));

    TEST_ASSERT_EQUAL_UINT32(44100, restored.audio.sampleRate);
    TEST_ASSERT_EQUAL(SynthEngineType::WAVETABLE, restored.voicing.engine);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, restored.voicing.envelope.attackMs);
    TEST_ASSERT_EQUAL(NotePriority::HIGHEST, restored.instrument.notePriority);
    TEST_ASSERT_EQUAL_UINT8(4, restored.valves.count);
    TEST_ASSERT_EQUAL(ValveActuatorType::SOLENOID, restored.valves.items[2].type);
    TEST_ASSERT_EQUAL_UINT16(1500, restored.valves.items[2].maxOnMs);
    TEST_ASSERT_EQUAL(ServoDriverType::PCA9685, restored.valves.items[3].driver);
    TEST_ASSERT_EQUAL_INT8(7, restored.valves.items[3].channel);
    TEST_ASSERT_EQUAL_STRING("Trompette de test", restored.system.deviceName);
    TEST_ASSERT_EQUAL_UINT8(original.midi.routeCount, restored.midi.routeCount);
}

void test_fingering_edits_survive_the_round_trip(void) {
    InstrumentConfiguration original;
    ConfigManager::makeDefaults(original);
    original.instrument.fingeringOverrideCount = 2;
    original.instrument.fingeringOverrides[0] = {60, 0x05, 0x02};
    original.instrument.fingeringOverrides[1] = {72, kNoFingeringMask, kNoFingeringMask};

    static char buffer[8192];
    const size_t length = serializeConfig(original, buffer, sizeof(buffer));
    InstrumentConfiguration restored;
    TEST_ASSERT_TRUE(deserializeConfig(buffer, length, restored));

    TEST_ASSERT_EQUAL_UINT8(2, restored.instrument.fingeringOverrideCount);
    TEST_ASSERT_EQUAL_UINT8(60, restored.instrument.fingeringOverrides[0].written);
    TEST_ASSERT_EQUAL_UINT8(0x05, restored.instrument.fingeringOverrides[0].primary);
    TEST_ASSERT_EQUAL_UINT8(0x02, restored.instrument.fingeringOverrides[0].alternate);
    // "no fingering" is not the open position and must not become one.
    TEST_ASSERT_EQUAL_UINT8(kNoFingeringMask, restored.instrument.fingeringOverrides[1].primary);

    // A file with no edits carries no chart at all.
    ConfigManager::makeDefaults(original);
    const size_t plain = serializeConfig(original, buffer, sizeof(buffer));
    TEST_ASSERT_GREATER_THAN(200u, plain);
    TEST_ASSERT_NULL(strstr(buffer, "\"fingering\""));
}

// A configuration file with everything the schema allows: four voicings with
// their harmonics, EQ bands and register curves, every route, four valves and a
// full fingering chart. The serialisation buffer is a fixed 8 kB and this is
// the only thing that says whether it is big enough - a configuration that
// cannot be written is a configuration that is silently lost.
namespace {

void fillToTheBrim(InstrumentConfiguration& cfg) {
    ConfigManager::makeDefaults(cfg);
    copyString(cfg.system.deviceName, kNameLen, "Orchestrion trumpet, bench number four");
    copyString(cfg.wifi.ssid, sizeof(cfg.wifi.ssid), "a-rather-long-network-name-here");
    copyString(cfg.midi.ble.deviceName, kNameLen, "GMB MIDI Trumpet, workshop unit");

    VoicingConfig v = cfg.voicing;
    v.additive.harmonicCount = kMaxHarmonics;
    for (uint8_t h = 0; h < kMaxHarmonics; ++h) v.additive.harmonicGain[h] = 0.123456f + h * 0.01f;
    for (uint8_t b = 0; b < kMaxEqBands; ++b) {
        v.eq[b].frequency = 123.456f * (b + 1);
        v.eq[b].gainDb = -4.25f + b;
        v.eq[b].q = 0.707f + b * 0.1f;
        v.eq[b].enabled = true;
    }
    for (uint8_t r = 0; r < kRegisterPoints; ++r) {
        v.registerCurve[r].gainDb = -3.75f + r;
        v.registerCurve[r].brightness = -0.5f + r * 0.25f;
    }
    v.outputTrimDb = -1.75f;
    copyString(v.name, kNameLen, "Bright and brassy, take four");
    cfg.voicing = v;

    cfg.voicings.count = kMaxVoicings;
    for (uint8_t i = 0; i < kMaxVoicings; ++i) {
        cfg.voicings.items[i] = v;
        copyString(cfg.voicings.items[i].name, kNameLen, "Voicing with a long enough name");
    }

    cfg.midi.routeCount = kMaxRoutes;
    for (uint8_t i = 0; i < kMaxRoutes; ++i) {
        MidiRoute& r = cfg.midi.routes[i];
        r.source = MidiPort::USB;
        r.destination = MidiPort::SOUND_ENGINE;
        r.enabled = true;
        r.channelMask = 0xAAAA;
        r.transpose = -12;
        r.velocityCurve = VelocityCurve::SOFT;
        r.fixedVelocity = 111;
        r.noteMin = 21;
        r.noteMax = 108;
    }

    cfg.valves.count = kMaxValves;
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        cfg.valves.items[i].type = i % 2 ? ValveActuatorType::SOLENOID : ValveActuatorType::SERVO;
        cfg.valves.items[i].measuredSettleMs = 44 + i;
    }

    cfg.instrument.fingeringOverrideCount = kMaxFingeringOverrides;
    for (uint8_t i = 0; i < kMaxFingeringOverrides; ++i) {
        cfg.instrument.fingeringOverrides[i] = {static_cast<uint8_t>(40 + i), 0x05, 0x02};
    }
}

}  // namespace

void test_the_largest_configuration_still_fits(void) {
    InstrumentConfiguration cfg;
    fillToTheBrim(cfg);

    // The same buffer ConfigManager::save() uses.
    static char buffer[kConfigJsonCapacity];
    const size_t length = serializeConfig(cfg, buffer, sizeof(buffer), SecretPolicy::INCLUDE);
    TEST_ASSERT_TRUE_MESSAGE(length > 0, "the largest legal configuration did not serialise");
    // save() refuses anything that reaches the end of the buffer, so the test
    // has to clear that same bar - with room to spare for the next field.
    TEST_ASSERT_TRUE_MESSAGE(length < sizeof(buffer) - 1024,
                             "less than 1 kB of headroom left in the configuration buffer");

    // And it must survive the round trip, not merely fit.
    InstrumentConfiguration restored;
    TEST_ASSERT_TRUE(deserializeConfig(buffer, length, restored));
    TEST_ASSERT_EQUAL_UINT8(kMaxVoicings, restored.voicings.count);
    TEST_ASSERT_EQUAL_UINT8(kMaxRoutes, restored.midi.routeCount);
    TEST_ASSERT_EQUAL_UINT8(kMaxFingeringOverrides,
                            restored.instrument.fingeringOverrideCount);
    TEST_ASSERT_EQUAL_UINT8(kMaxHarmonics, restored.voicing.additive.harmonicCount);
    TEST_ASSERT_TRUE(restored.voicing.eq[kMaxEqBands - 1].enabled);
}

// The redacted form is what the export file and the REST API carry, and it goes
// through a buffer of the same size in the web server.
void test_the_largest_configuration_fits_redacted(void) {
    InstrumentConfiguration cfg;
    fillToTheBrim(cfg);
    static char buffer[kConfigJsonCapacity];
    const size_t length = serializeConfig(cfg, buffer, sizeof(buffer), SecretPolicy::REDACT);
    TEST_ASSERT_TRUE(length > 0);
    TEST_ASSERT_TRUE(length < sizeof(buffer) - 1024);
}

void test_missing_fields_keep_their_default(void) {
    // A minimal document from an older or hand-written file.
    const char* json = "{\"schemaVersion\":2,\"audio\":{\"sampleRate\":32000}}";
    InstrumentConfiguration cfg;
    TEST_ASSERT_TRUE(deserializeConfig(json, strlen(json), cfg));

    TEST_ASSERT_EQUAL_UINT32(32000, cfg.audio.sampleRate);
    // Untouched members must equal the factory value, not zero.
    TEST_ASSERT_EQUAL_UINT8(3, cfg.valves.count);
    TEST_ASSERT_EQUAL(AudioBackendType::PCM5102A, cfg.audio.backend);
    TEST_ASSERT_EQUAL_UINT8(2, cfg.voicing.pitchBendRangeSemitones);
}

void test_unknown_fields_are_ignored(void) {
    const char* json =
        "{\"schemaVersion\":2,\"somethingFromTheFuture\":{\"a\":1},"
        "\"audio\":{\"backend\":\"MAX98357A\",\"unknown\":42}}";
    InstrumentConfiguration cfg;
    TEST_ASSERT_TRUE(deserializeConfig(json, strlen(json), cfg));
    TEST_ASSERT_EQUAL(AudioBackendType::MAX98357A, cfg.audio.backend);
}

void test_garbage_is_rejected(void) {
    InstrumentConfiguration cfg;
    TEST_ASSERT_FALSE(deserializeConfig("not json at all", 15, cfg));
    // Valid JSON but no schema version: not a configuration.
    TEST_ASSERT_FALSE(deserializeConfig("{\"audio\":{}}", 11, cfg));
}

// ---------------------------------------------------------------------------
// Secrets
// ---------------------------------------------------------------------------
void test_wifi_passwords_never_leave_the_device(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    copyString(cfg.wifi.ssid, sizeof(cfg.wifi.ssid), "HomeNetwork");
    copyString(cfg.wifi.password, sizeof(cfg.wifi.password), "s3cr3t-passphrase");
    copyString(cfg.wifi.apPassword, sizeof(cfg.wifi.apPassword), "hotspot-pass");

    static char buffer[8192];
    // What the REST API and the export file carry.
    const size_t redacted = serializeConfig(cfg, buffer, sizeof(buffer), SecretPolicy::REDACT);
    TEST_ASSERT_GREATER_THAN(200, redacted);
    TEST_ASSERT_NULL(strstr(buffer, "s3cr3t-passphrase"));
    TEST_ASSERT_NULL(strstr(buffer, "hotspot-pass"));
    // The SSID is not a secret, and the UI needs to know a password exists.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "HomeNetwork"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "passwordSet"));

    // What the on-device store keeps.
    const size_t full = serializeConfig(cfg, buffer, sizeof(buffer), SecretPolicy::INCLUDE);
    TEST_ASSERT_GREATER_THAN(redacted, full);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "s3cr3t-passphrase"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "hotspot-pass"));
}

void test_saving_a_redacted_document_keeps_the_stored_passwords(void) {
    InstrumentConfiguration stored;
    ConfigManager::makeDefaults(stored);
    copyString(stored.wifi.password, sizeof(stored.wifi.password), "s3cr3t-passphrase");
    copyString(stored.wifi.apPassword, sizeof(stored.wifi.apPassword), "hotspot-pass");

    // Round trip through the redacted form, exactly as the web UI does.
    static char buffer[8192];
    const size_t n = serializeConfig(stored, buffer, sizeof(buffer), SecretPolicy::REDACT);
    InstrumentConfiguration fromBrowser;
    TEST_ASSERT_TRUE(deserializeConfig(buffer, n, fromBrowser));
    TEST_ASSERT_EQUAL_STRING("", fromBrowser.wifi.password);

    // Saving it must not wipe the credentials already on the device.
    preserveSecrets(fromBrowser, stored);
    TEST_ASSERT_EQUAL_STRING("s3cr3t-passphrase", fromBrowser.wifi.password);
    TEST_ASSERT_EQUAL_STRING("hotspot-pass", fromBrowser.wifi.apPassword);
}

void test_an_explicit_password_still_wins(void) {
    InstrumentConfiguration stored;
    ConfigManager::makeDefaults(stored);
    copyString(stored.wifi.password, sizeof(stored.wifi.password), "old-password");

    InstrumentConfiguration incoming = stored;
    copyString(incoming.wifi.password, sizeof(incoming.wifi.password), "new-password");
    preserveSecrets(incoming, stored);
    TEST_ASSERT_EQUAL_STRING("new-password", incoming.wifi.password);
}

// ---------------------------------------------------------------------------
// Migration
// ---------------------------------------------------------------------------
void test_migration_v1_creates_routes(void) {
    // A v1 file had no routing matrix: every input fed both engines.
    const char* json = "{\"schemaVersion\":1,\"audio\":{\"backend\":\"PCM5102A\"}}";
    InstrumentConfiguration cfg;
    TEST_ASSERT_TRUE(deserializeConfig(json, strlen(json), cfg));

    TEST_ASSERT_EQUAL_UINT16(kConfigSchemaVersion, cfg.schemaVersion);
    TEST_ASSERT_GREATER_THAN_UINT8(0, cfg.midi.routeCount);

    bool usbToSound = false, dinToValves = false;
    for (uint8_t i = 0; i < cfg.midi.routeCount; ++i) {
        const MidiRoute& r = cfg.midi.routes[i];
        if (r.source == MidiPort::USB && r.destination == MidiPort::SOUND_ENGINE && r.enabled) {
            usbToSound = true;
        }
        if (r.source == MidiPort::DIN && r.destination == MidiPort::VALVE_ENGINE && r.enabled) {
            dinToValves = true;
        }
    }
    TEST_ASSERT_TRUE(usbToSound);
    TEST_ASSERT_TRUE(dinToValves);
}

void test_migration_v2_repairs_the_speaker_rating(void) {
    // A v2 file that stored the old, wrong Monacor figures: 30 W "RMS" and a
    // 22 W protection limit, both above the manufacturer's 20 W continuous
    // rating.  Migrating must repair a catalogued driver rather than keep
    // running the coil above what it is specified for.
    const char* json =
        "{\"schemaVersion\":2,\"speaker\":{\"profile\":\"MONACOR_SPX30M\","
        "\"powerRms\":30.0,\"powerLimit\":22.0,\"impedance\":8.0},"
        "\"acoustic\":{\"coupling\":\"SEALED_CHAMBER\",\"chamberVolumeMl\":120.0,"
        "\"outletDiameterMm\":11.0,\"outletLengthMm\":45.0,\"highPassHz\":170.0}}";
    InstrumentConfiguration cfg;
    TEST_ASSERT_TRUE(deserializeConfig(json, strlen(json), cfg));

    TEST_ASSERT_EQUAL_UINT16(kConfigSchemaVersion, cfg.schemaVersion);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, cfg.speaker.powerRmsW);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, cfg.speaker.powerMaxW);
    TEST_ASSERT_TRUE(cfg.speaker.powerLimitW <= cfg.speaker.powerRmsW);

    // The old single-chamber geometry is carried over, not discarded.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.acoustic.rearChamberVolumeMl);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.0f, cfg.acoustic.leadpipeDiameterMm);
    // A hand-entered high pass was the only figure a v2 user could give, so it
    // becomes the measured override rather than being thrown away.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 170.0f, cfg.acoustic.measuredHighPassHz);
    TEST_ASSERT_TRUE(cfg.acoustic.stage1.inletDiameterMm > 0.0f);
    TEST_ASSERT_TRUE(cfg.valves.sync.enabled);
}

void test_migration_v2_leaves_a_custom_speaker_alone(void) {
    // A CUSTOM speaker is the builder's own measurement: migrating must not
    // overwrite it with a catalogue row.
    const char* json =
        "{\"schemaVersion\":2,\"speaker\":{\"profile\":\"CUSTOM\",\"name\":\"my driver\","
        "\"powerRms\":7.5,\"powerLimit\":5.0,\"impedance\":6.0}}";
    InstrumentConfiguration cfg;
    TEST_ASSERT_TRUE(deserializeConfig(json, strlen(json), cfg));

    TEST_ASSERT_EQUAL_STRING("my driver", cfg.speaker.name);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, cfg.speaker.powerRmsW);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, cfg.speaker.powerLimitW);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, cfg.speaker.impedanceOhm);
    // No maximum was ever stored: fall back to the continuous rating rather
    // than to something invented.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, cfg.speaker.powerMaxW);
}

void test_migration_is_idempotent(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    const uint8_t routesBefore = cfg.midi.routeCount;

    MigrationResult first = migrateConfig(cfg);
    TEST_ASSERT_FALSE(first.migrated);
    MigrationResult second = migrateConfig(cfg);
    TEST_ASSERT_FALSE(second.migrated);
    TEST_ASSERT_EQUAL_UINT8(routesBefore, cfg.midi.routeCount);
}

void test_migration_refuses_a_newer_schema(void) {
    InstrumentConfiguration cfg;
    cfg.schemaVersion = kConfigSchemaVersion + 5;
    MigrationResult result = migrateConfig(cfg);
    TEST_ASSERT_TRUE(result.unsupported);
    TEST_ASSERT_FALSE(result.migrated);
}

// ---------------------------------------------------------------------------
// The two engines must be handed the same notes
// ---------------------------------------------------------------------------
namespace {

// The routes the defaults create from a given source to each engine.
MidiRoute* engineRoute(InstrumentConfiguration& cfg, MidiPort source, MidiPort destination) {
    for (uint8_t i = 0; i < cfg.midi.routeCount && i < kMaxRoutes; ++i) {
        MidiRoute& r = cfg.midi.routes[i];
        if (r.source == source && r.destination == destination) return &r;
    }
    return nullptr;
}

}  // namespace

void test_defaults_feed_both_engines_identically(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_FALSE(hasIssue(report, Severity::WARNING, "midi.routes"));
}

void test_a_transpose_on_one_engine_only_is_a_warning(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    MidiRoute* sound = engineRoute(cfg, MidiPort::USB, MidiPort::SOUND_ENGINE);
    TEST_ASSERT_NOT_NULL(sound);
    sound->transpose = 12;   // the synthesis plays an octave the pistons do not

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_TRUE(hasIssue(report, Severity::WARNING, "midi.routes"));
}

void test_sound_without_pistons_is_a_warning(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    MidiRoute* valves = engineRoute(cfg, MidiPort::USB, MidiPort::VALVE_ENGINE);
    TEST_ASSERT_NOT_NULL(valves);
    valves->enabled = false;

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_TRUE(hasIssue(report, Severity::WARNING, "midi.routes"));
}

// ---------------------------------------------------------------------------
// GPIO conflict detection
// ---------------------------------------------------------------------------
void test_duplicate_gpio_is_an_error(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    cfg.valves.items[0].gpio = cfg.audio.i2s.bclk;   // servo on the I2S clock

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_TRUE(report.hasErrors());
    TEST_ASSERT_TRUE(hasIssue(report, Severity::ERROR, "valves.1"));
}

void test_flash_pin_is_an_error(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    const BoardCapabilities caps = makeClassicCaps();
    cfg.valves.items[0].gpio = 7;   // SPI flash on a WROOM module

    ValidationReport report;
    ConfigValidator::validate(cfg, caps, report);
    TEST_ASSERT_TRUE(report.hasErrors());
}

void test_input_only_pin_cannot_drive_a_servo(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    const BoardCapabilities caps = makeClassicCaps();
    cfg.valves.items[0].gpio = 35;   // input only on the classic ESP32

    ValidationReport report;
    ConfigValidator::validate(cfg, caps, report);
    TEST_ASSERT_TRUE(report.hasErrors());
}

void test_pca9685_channel_conflict(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    for (uint8_t i = 0; i < 2; ++i) {
        cfg.valves.items[i].type = ValveActuatorType::SERVO;
        cfg.valves.items[i].driver = ServoDriverType::PCA9685;
        cfg.valves.items[i].channel = 3;
    }
    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_TRUE(report.hasErrors());
}

// ---------------------------------------------------------------------------
// Hardware compatibility
// ---------------------------------------------------------------------------
void test_usb_midi_unavailable_on_classic_esp32(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    cfg.midi.usb.inEnabled = true;

    ValidationReport report;
    ConfigValidator::validate(cfg, makeClassicCaps(), report);
    TEST_ASSERT_TRUE(hasIssue(report, Severity::ERROR, "midi.usb"));

    // ... and it is available on a board that has native USB.
    ValidationReport s3Report;
    ConfigValidator::validate(cfg, makeS3Caps(), s3Report);
    TEST_ASSERT_FALSE(hasIssue(s3Report, Severity::ERROR, "midi.usb"));
}

void test_internal_dac_unavailable_on_s3(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    cfg.audio.backend = AudioBackendType::ESP32_INTERNAL_DAC;

    ValidationReport report;
    ConfigValidator::validate(cfg, makeS3Caps(), report);
    TEST_ASSERT_TRUE(hasIssue(report, Severity::ERROR, "audio.backend"));
}

void test_speaker_power_mismatch(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    cfg.speaker.powerRmsW = 5.0f;
    cfg.speaker.powerLimitW = 20.0f;   // above the driver rating

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_TRUE(hasIssue(report, Severity::ERROR, "speaker.powerLimit"));
}

void test_amplifier_stronger_than_speaker_is_a_warning(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    cfg.amplifier.maxPowerW = 25.0f;
    cfg.speaker.powerRmsW = 8.0f;
    cfg.speaker.powerLimitW = 5.0f;

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_FALSE(report.hasErrors());
    TEST_ASSERT_TRUE(hasIssue(report, Severity::WARNING, "amplifier.maxPower"));
}

// ---------------------------------------------------------------------------
// Actuator safety at configuration level
// ---------------------------------------------------------------------------
void test_solenoid_without_timeout_is_refused(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    cfg.valves.items[0].type = ValveActuatorType::SOLENOID;
    cfg.valves.items[0].maxOnMs = 0;

    ValidationReport report;
    ConfigValidator::validate(cfg, boardCaps(), report);
    TEST_ASSERT_TRUE(report.hasErrors());
}

void test_sanitise_repairs_a_dangerous_file(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    cfg.valves.items[0].type = ValveActuatorType::SOLENOID;
    cfg.valves.items[0].maxOnMs = 0;
    cfg.valves.items[0].pullInPwm = 250;
    cfg.valves.items[0].cooldownMs = 0;
    cfg.valves.count = 9;
    cfg.audio.sampleRate = 1000000;
    cfg.midi.globalChannelMask = 0;

    TEST_ASSERT_TRUE(ConfigValidator::sanitise(cfg, boardCaps()));
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(20000, cfg.valves.items[0].maxOnMs);
    TEST_ASSERT_GREATER_THAN_UINT16(0, cfg.valves.items[0].maxOnMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, cfg.valves.items[0].pullInPwm);
    TEST_ASSERT_GREATER_THAN_UINT16(0, cfg.valves.items[0].cooldownMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(kMaxValves, cfg.valves.count);
    TEST_ASSERT_EQUAL_UINT32(48000, cfg.audio.sampleRate);
    TEST_ASSERT_NOT_EQUAL(0, cfg.midi.globalChannelMask);
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
void test_presets_produce_valid_configurations(void) {
    for (uint8_t i = 0; i < presetCount(); ++i) {
        const PresetInfo& info = presetInfo(i);
        InstrumentConfiguration cfg;
        ConfigManager::makeDefaults(cfg);
        TEST_ASSERT_TRUE(applyPreset(info.id, cfg));
        ConfigValidator::sanitise(cfg, boardCaps());

        ValidationReport report;
        ConfigValidator::validate(cfg, boardCaps(), report);
        for (uint8_t k = 0; k < report.count(); ++k) {
            if (report.issue(k).severity == Severity::ERROR) {
                char message[160];
                snprintf(message, sizeof(message), "preset %s: %s", info.key,
                         report.issue(k).message);
                TEST_FAIL_MESSAGE(message);
            }
        }
    }
}

void test_standard_preset_is_the_reference_chain(void) {
    InstrumentConfiguration cfg;
    ConfigManager::makeDefaults(cfg);
    applyPreset(PresetId::STANDARD, cfg);
    TEST_ASSERT_EQUAL(AudioBackendType::PCM5102A, cfg.audio.backend);
    TEST_ASSERT_EQUAL(AmplifierType::TPA3118D2, cfg.amplifier.type);
    TEST_ASSERT_EQUAL(SpeakerProfileId::VISATON_FRS8M, cfg.speaker.profile);
    TEST_ASSERT_EQUAL(AcousticCouplingType::SEALED_CHAMBER, cfg.acoustic.coupling);
}

// ---------------------------------------------------------------------------
// Voicing: the live path's guard rail
// ---------------------------------------------------------------------------
void test_voicing_sanitiser_rejects_nonsense(void) {
    VoicingConfig v;
    const float nan = 0.0f / (v.darkTilt - v.darkTilt);   // NaN without <cmath>
    v.envelope.attackMs = -50.0f;
    v.envelope.sustain = 9.0f;
    v.vibrato.frequencyHz = nan;
    v.additive.harmonicCount = 200;
    v.additive.harmonicGain[3] = nan;
    v.darkTilt = 900.0f;
    v.hybridMix = -4.0f;
    v.outputTrimDb = 400.0f;
    v.eq[0].frequency = 0.0f;
    v.eq[0].gainDb = 200.0f;
    v.eq[0].q = 0.0f;
    v.pitchBendRangeSemitones = 0;
    v.engine = static_cast<SynthEngineType>(99);

    ConfigValidator::sanitiseVoicing(v);

    TEST_ASSERT_TRUE(v.envelope.attackMs >= 0.5f);
    TEST_ASSERT_TRUE(v.envelope.sustain <= 1.0f);
    TEST_ASSERT_TRUE(v.vibrato.frequencyHz == v.vibrato.frequencyHz);   // not NaN
    TEST_ASSERT_TRUE(v.vibrato.frequencyHz >= 0.1f);
    TEST_ASSERT_EQUAL_UINT8(kMaxHarmonics, v.additive.harmonicCount);
    TEST_ASSERT_TRUE(v.additive.harmonicGain[3] == v.additive.harmonicGain[3]);
    TEST_ASSERT_TRUE(v.darkTilt <= 6.0f);
    TEST_ASSERT_TRUE(v.hybridMix >= 0.0f && v.hybridMix <= 1.0f);
    // The trim sits before the limiter, but it still may not be turned into a
    // 400 dB shove that leaves the protection stage as the only thing left.
    TEST_ASSERT_TRUE(v.outputTrimDb <= 12.0f);
    TEST_ASSERT_TRUE(v.eq[0].frequency >= 20.0f);
    TEST_ASSERT_TRUE(v.eq[0].gainDb <= 18.0f);
    TEST_ASSERT_TRUE(v.eq[0].q >= 0.1f);
    TEST_ASSERT_EQUAL_UINT8(2, v.pitchBendRangeSemitones);
    TEST_ASSERT_EQUAL(SynthEngineType::ADDITIVE, v.engine);
}

void test_voicing_sanitiser_sorts_the_register_curve(void) {
    VoicingConfig v;
    v.registerCurve[0] = {90, -3.0f, 0.0f};
    v.registerCurve[1] = {40, 0.0f, 0.0f};
    v.registerCurve[2] = {70, 0.0f, 0.0f};
    v.registerCurve[3] = {50, 0.0f, 0.0f};
    v.registerCurve[4] = {60, 0.0f, 0.0f};
    ConfigValidator::sanitiseVoicing(v);
    // The curve is walked in order; an unsorted one silently skips points.
    for (uint8_t i = 1; i < kRegisterPoints; ++i) {
        TEST_ASSERT_TRUE(v.registerCurve[i].note >= v.registerCurve[i - 1].note);
    }
    TEST_ASSERT_EQUAL_UINT8(40, v.registerCurve[0].note);
    TEST_ASSERT_EQUAL_UINT8(90, v.registerCurve[kRegisterPoints - 1].note);
}

void test_voicing_round_trip(void) {
    InstrumentConfiguration original;
    ConfigManager::makeDefaults(original);
    copyString(original.voicing.name, kNameLen, "Prototype 07");
    original.voicing.engine = SynthEngineType::BRASS_EXCITER;
    original.voicing.darkTilt = 3.1f;
    original.voicing.hybridMix = 0.25f;
    original.voicing.additive.harmonicGain[6] = 0.137f;
    original.voicing.eq[4].enabled = true;
    original.voicing.eq[4].gainDb = -4.5f;
    original.voicing.registerCurve[3] = {74, -2.5f, 0.3f};
    original.voicing.exciter.asymmetry = -0.4f;
    original.voicings.count = 1;
    original.voicings.items[0] = original.voicing;
    copyString(original.voicings.items[0].name, kNameLen, "Bright");

    static char buffer[16384];
    const size_t length = serializeConfig(original, buffer, sizeof(buffer));
    TEST_ASSERT_GREATER_THAN(400, length);

    InstrumentConfiguration restored;
    TEST_ASSERT_TRUE(deserializeConfig(buffer, length, restored));

    TEST_ASSERT_EQUAL_STRING("Prototype 07", restored.voicing.name);
    TEST_ASSERT_EQUAL(SynthEngineType::BRASS_EXCITER, restored.voicing.engine);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.1f, restored.voicing.darkTilt);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, restored.voicing.hybridMix);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.137f, restored.voicing.additive.harmonicGain[6]);
    TEST_ASSERT_TRUE(restored.voicing.eq[4].enabled);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -4.5f, restored.voicing.eq[4].gainDb);
    TEST_ASSERT_EQUAL_UINT8(74, restored.voicing.registerCurve[3].note);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, restored.voicing.registerCurve[3].brightness);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.4f, restored.voicing.exciter.asymmetry);
    TEST_ASSERT_EQUAL_UINT8(1, restored.voicings.count);
    TEST_ASSERT_EQUAL_STRING("Bright", restored.voicings.items[0].name);
}

void test_migration_v3_moves_the_sound_into_the_voicing(void) {
    // A v3 file kept the sound inside `audio`. Migrating must carry it across
    // rather than resetting the instrument to the factory voice.
    const char* json =
        "{\"schemaVersion\":3,\"audio\":{\"sampleRate\":48000,\"engine\":\"WAVETABLE\","
        "\"pitchBendRange\":12,"
        "\"envelope\":{\"attackMs\":33.0,\"breathNoise\":0.07},"
        "\"vibrato\":{\"frequencyHz\":6.5},"
        "\"additive\":{\"harmonicCount\":7},"
        "\"eq\":[{\"frequency\":250.0,\"gainDb\":-3.0,\"q\":1.0,\"enabled\":true}]}}";
    InstrumentConfiguration cfg;
    TEST_ASSERT_TRUE(deserializeConfig(json, strlen(json), cfg));

    TEST_ASSERT_EQUAL_UINT16(kConfigSchemaVersion, cfg.schemaVersion);
    TEST_ASSERT_EQUAL(SynthEngineType::WAVETABLE, cfg.voicing.engine);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.0f, cfg.voicing.envelope.attackMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.07f, cfg.voicing.envelope.breathNoise);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, cfg.voicing.vibrato.frequencyHz);
    TEST_ASSERT_EQUAL_UINT8(7, cfg.voicing.additive.harmonicCount);
    TEST_ASSERT_EQUAL_UINT8(12, cfg.voicing.pitchBendRangeSemitones);
    TEST_ASSERT_TRUE(cfg.voicing.eq[0].enabled);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.0f, cfg.voicing.eq[0].gainDb);

    // The three bands a v3 file never had come up usable, not at 0 Hz.
    for (uint8_t i = 3; i < kMaxEqBands; ++i) {
        TEST_ASSERT_TRUE(cfg.voicing.eq[i].frequency >= 20.0f);
        TEST_ASSERT_FALSE(cfg.voicing.eq[i].enabled);
    }
    // And the constants that used to be compiled in are now the stored values,
    // so a migrated instrument sounds exactly as it did.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.6f, cfg.voicing.darkTilt);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, cfg.voicing.brightTilt);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, cfg.voicing.hybridMix);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, cfg.voicing.velocityFloor);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, cfg.voicing.aftertouchToBrightness);
}

int main(int, char**) {
    initBoardCaps();
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_round_trip_preserves_values);
    RUN_TEST(test_the_largest_configuration_still_fits);
    RUN_TEST(test_the_largest_configuration_fits_redacted);
    RUN_TEST(test_fingering_edits_survive_the_round_trip);
    RUN_TEST(test_missing_fields_keep_their_default);
    RUN_TEST(test_unknown_fields_are_ignored);
    RUN_TEST(test_garbage_is_rejected);
    RUN_TEST(test_wifi_passwords_never_leave_the_device);
    RUN_TEST(test_saving_a_redacted_document_keeps_the_stored_passwords);
    RUN_TEST(test_an_explicit_password_still_wins);
    RUN_TEST(test_migration_v1_creates_routes);
    RUN_TEST(test_migration_v2_repairs_the_speaker_rating);
    RUN_TEST(test_migration_v2_leaves_a_custom_speaker_alone);
    RUN_TEST(test_migration_is_idempotent);
    RUN_TEST(test_migration_refuses_a_newer_schema);
    RUN_TEST(test_defaults_feed_both_engines_identically);
    RUN_TEST(test_a_transpose_on_one_engine_only_is_a_warning);
    RUN_TEST(test_sound_without_pistons_is_a_warning);
    RUN_TEST(test_duplicate_gpio_is_an_error);
    RUN_TEST(test_flash_pin_is_an_error);
    RUN_TEST(test_input_only_pin_cannot_drive_a_servo);
    RUN_TEST(test_pca9685_channel_conflict);
    RUN_TEST(test_usb_midi_unavailable_on_classic_esp32);
    RUN_TEST(test_internal_dac_unavailable_on_s3);
    RUN_TEST(test_speaker_power_mismatch);
    RUN_TEST(test_amplifier_stronger_than_speaker_is_a_warning);
    RUN_TEST(test_solenoid_without_timeout_is_refused);
    RUN_TEST(test_sanitise_repairs_a_dangerous_file);
    RUN_TEST(test_presets_produce_valid_configurations);
    RUN_TEST(test_standard_preset_is_the_reference_chain);
    RUN_TEST(test_voicing_sanitiser_rejects_nonsense);
    RUN_TEST(test_voicing_sanitiser_sorts_the_register_curve);
    RUN_TEST(test_voicing_round_trip);
    RUN_TEST(test_migration_v3_moves_the_sound_into_the_voicing);
    return UNITY_END();
}
