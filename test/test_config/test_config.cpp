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
    original.audio.engine = SynthEngineType::WAVETABLE;
    original.audio.envelope.attackMs = 7.5f;
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
    TEST_ASSERT_EQUAL(SynthEngineType::WAVETABLE, restored.audio.engine);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, restored.audio.envelope.attackMs);
    TEST_ASSERT_EQUAL(NotePriority::HIGHEST, restored.instrument.notePriority);
    TEST_ASSERT_EQUAL_UINT8(4, restored.valves.count);
    TEST_ASSERT_EQUAL(ValveActuatorType::SOLENOID, restored.valves.items[2].type);
    TEST_ASSERT_EQUAL_UINT16(1500, restored.valves.items[2].maxOnMs);
    TEST_ASSERT_EQUAL(ServoDriverType::PCA9685, restored.valves.items[3].driver);
    TEST_ASSERT_EQUAL_INT8(7, restored.valves.items[3].channel);
    TEST_ASSERT_EQUAL_STRING("Trompette de test", restored.system.deviceName);
    TEST_ASSERT_EQUAL_UINT8(original.midi.routeCount, restored.midi.routeCount);
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
    TEST_ASSERT_EQUAL_UINT8(2, cfg.audio.pitchBendRangeSemitones);
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

int main(int, char**) {
    initBoardCaps();
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_round_trip_preserves_values);
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
    return UNITY_END();
}
