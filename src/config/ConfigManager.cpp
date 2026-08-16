#include "config/ConfigManager.h"

#include "config/BoardCaps.h"
#include "config/ConfigSerializer.h"
#include "config/Presets.h"
#include "diagnostics/Logger.h"
#include "midi/MidiRouter.h"

#if !defined(OT_HOST_BUILD)
#include <FS.h>
#include <LittleFS.h>
#endif

namespace ot {

namespace {
constexpr const char* kPath = "/config.json";
constexpr const char* kPathNew = "/config.new";
constexpr const char* kPathBackup = "/config.bak";

}  // namespace

void ConfigManager::makeDefaults(InstrumentConfiguration& cfg) {
    cfg = InstrumentConfiguration();
    const BoardCapabilities& caps = boardCaps();
    cfg.board = caps.board;

    // Pin maps that are known to be free on the two reference dev boards.
    if (caps.board == BoardType::ESP32) {
        cfg.audio.i2s = {26, 25, 22, -1, -1};
        cfg.audio.i2c = {21, 19, 400000};
        cfg.valves.pca9685I2c = {21, 19, 400000};
        cfg.midi.din.rxGpio = 16;
        cfg.midi.din.txGpio = 17;
        cfg.valves.items[0].gpio = 32;
        cfg.valves.items[1].gpio = 33;
        cfg.valves.items[2].gpio = 27;
        cfg.valves.items[3].gpio = 14;
        // No native USB on the classic ESP32.
        cfg.midi.usb.inEnabled = false;
        cfg.midi.usb.outEnabled = false;
    } else {
        cfg.audio.i2s = {5, 6, 7, -1, -1};
        cfg.audio.i2c = {8, 9, 400000};
        cfg.valves.pca9685I2c = {8, 9, 400000};
        cfg.midi.din.rxGpio = 18;
        cfg.midi.din.txGpio = 17;
        cfg.valves.items[0].gpio = 15;
        cfg.valves.items[1].gpio = 16;
        cfg.valves.items[2].gpio = 4;
        cfg.valves.items[3].gpio = 2;
    }

    applyPreset(PresetId::STANDARD, cfg);
    MidiRouter::makeDefaultRoutes(cfg.midi);
    ConfigValidator::sanitise(cfg, caps);
}

bool ConfigManager::begin() {
#if defined(OT_HOST_BUILD)
    mounted_ = false;
    return false;
#else
    // `true` formats the partition when the mount fails: a brand new board has
    // no filesystem and must still be able to serve the web UI.
    mounted_ = LittleFS.begin(true);
    if (!mounted_) OT_LOGE("config", "LittleFS mount failed");
    return mounted_;
#endif
}

ConfigLoadResult ConfigManager::load() {
    makeDefaults(config_);
    migration_ = MigrationResult();
    safeMode_ = false;

#if defined(OT_HOST_BUILD)
    return ConfigLoadResult::DEFAULTS_NO_FILE;
#else
    if (!mounted_) {
        safeMode_ = true;
        return ConfigLoadResult::DEFAULTS_NO_FILE;
    }
    if (!LittleFS.exists(kPath)) {
        OT_LOGI("config", "no stored configuration, starting from the defaults");
        return ConfigLoadResult::DEFAULTS_NO_FILE;
    }

    File f = LittleFS.open(kPath, "r");
    if (!f) {
        safeMode_ = true;
        return ConfigLoadResult::DEFAULTS_CORRUPT;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        OT_LOGE("config", "config.json is not valid JSON (%s)", err.c_str());
        safeMode_ = true;
        return ConfigLoadResult::DEFAULTS_CORRUPT;
    }

    InstrumentConfiguration loaded;
    const uint16_t storedVersion =
        doc["schemaVersion"].is<int>() ? static_cast<uint16_t>(doc["schemaVersion"].as<int>()) : 0;
    if (storedVersion > kConfigSchemaVersion) {
        OT_LOGE("config", "config.json was written by a newer firmware (v%u)",
                static_cast<unsigned>(storedVersion));
        migration_.fromVersion = storedVersion;
        migration_.unsupported = true;
        safeMode_ = true;
        return ConfigLoadResult::DEFAULTS_TOO_NEW;
    }

    if (!configFromJson(doc.as<JsonObjectConst>(), loaded)) {
        safeMode_ = true;
        return ConfigLoadResult::DEFAULTS_CORRUPT;
    }

    const bool migrated = storedVersion != kConfigSchemaVersion;
    migration_.fromVersion = storedVersion;
    migration_.toVersion = loaded.schemaVersion;
    migration_.migrated = migrated;

    // A stored file is never trusted blindly: repair what can be repaired and
    // refuse to start the actuators when an ERROR remains.
    ConfigValidator::sanitise(loaded, boardCaps());
    ValidationReport report;
    ConfigValidator::validate(loaded, report);
    if (report.hasErrors()) {
        OT_LOGE("config", "stored configuration has %u blocking problems, entering SAFE MODE",
                static_cast<unsigned>(report.count()));
        for (uint8_t i = 0; i < report.count(); ++i) {
            OT_LOGE("config", "  [%s] %s", ConfigValidator::toString(report.issue(i).severity),
                    report.issue(i).message);
        }
        config_ = loaded;   // keep it so the user can fix it in the web UI
        safeMode_ = true;
        return ConfigLoadResult::DEFAULTS_CORRUPT;
    }

    config_ = loaded;
    if (config_.system.safeModeForced) safeMode_ = true;
    if (migrated) {
        OT_LOGI("config", "configuration migrated from v%u to v%u",
                static_cast<unsigned>(storedVersion),
                static_cast<unsigned>(config_.schemaVersion));
        save();
        return ConfigLoadResult::LOADED_AND_MIGRATED;
    }
    return ConfigLoadResult::LOADED;
#endif
}

bool ConfigManager::writeFile(const char* path, const char* data, size_t length) {
#if defined(OT_HOST_BUILD)
    (void)path;
    (void)data;
    (void)length;
    return false;
#else
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    const size_t written = f.write(reinterpret_cast<const uint8_t*>(data), length);
    f.close();
    return written == length;
#endif
}

bool ConfigManager::save() {
#if defined(OT_HOST_BUILD)
    return false;
#else
    if (!mounted_) return false;

    // Straight into the file rather than through a fixed intermediate buffer.
    // A full instrument - four voicings, every route, four valves, a complete
    // fingering chart - is nearly 16 kB of JSON, and a 16 kB static buffer that
    // exists only for the duration of a save is 16 kB the audio engine cannot
    // have.
    size_t length = 0;
    {
        File f = LittleFS.open(kPathNew, "w");
        if (!f) {
            OT_LOGE("config", "could not write %s", kPathNew);
            return false;
        }
        JsonDocument doc;
        configToJson(config_, doc.to<JsonObject>(), SecretPolicy::INCLUDE);
        length = serializeJson(doc, f);
        f.close();
    }
    if (length == 0) {
        OT_LOGE("config", "the configuration could not be serialised");
        return false;
    }
    if (LittleFS.exists(kPathBackup)) LittleFS.remove(kPathBackup);
    if (LittleFS.exists(kPath)) LittleFS.rename(kPath, kPathBackup);
    if (!LittleFS.rename(kPathNew, kPath)) {
        OT_LOGE("config", "could not commit %s", kPath);
        if (LittleFS.exists(kPathBackup)) LittleFS.rename(kPathBackup, kPath);
        return false;
    }
    return true;
#endif
}

bool ConfigManager::factoryReset() {
    makeDefaults(config_);
    safeMode_ = false;
#if !defined(OT_HOST_BUILD)
    if (mounted_) {
        if (LittleFS.exists(kPath)) LittleFS.remove(kPath);
        if (LittleFS.exists(kPathBackup)) LittleFS.remove(kPathBackup);
        if (LittleFS.exists(kPathNew)) LittleFS.remove(kPathNew);
    }
#endif
    return save();
}

bool ConfigManager::applyAndSave(const InstrumentConfiguration& candidate,
                                 ValidationReport& report) {
    InstrumentConfiguration next = candidate;
    next.schemaVersion = kConfigSchemaVersion;
    next.board = boardCaps().board;
    // The candidate came back from the web UI with redacted passwords; saving
    // it must not wipe the credentials already stored on the device.
    preserveSecrets(next, config_);
    ConfigValidator::sanitise(next, boardCaps());
    ConfigValidator::validate(next, report);
    if (report.hasErrors()) return false;

    config_ = next;
    return save();
}

bool ConfigManager::setWifiCredentials(const char* stationPassword, const char* apPassword) {
    if (stationPassword) {
        copyString(config_.wifi.password, sizeof(config_.wifi.password), stationPassword);
    }
    if (apPassword) {
        copyString(config_.wifi.apPassword, sizeof(config_.wifi.apPassword), apPassword);
    }
    return save();
}

size_t ConfigManager::exportJson(char* out, size_t outSize) const {
    return serializeConfig(config_, out, outSize, SecretPolicy::REDACT);
}

bool ConfigManager::importJson(const char* json, size_t length, ValidationReport& report) {
    InstrumentConfiguration candidate;
    if (!deserializeConfig(json, length, candidate)) {
        report.add(Severity::ERROR, "import", "the document is not a valid configuration");
        return false;
    }
    return applyAndSave(candidate, report);
}

}  // namespace ot
