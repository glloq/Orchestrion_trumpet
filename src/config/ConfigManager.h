// ============================================================================
//  ConfigManager.h - persistence.
//
//  The configuration lives in LittleFS as /config.json.  Saving is atomic
//  (write to /config.new, then rename) so a power cut in the middle of a save
//  never leaves an unbootable instrument, and the previous file is kept as
//  /config.bak for one generation.
// ============================================================================
#pragma once

#include "config/ConfigMigration.h"
#include "config/ConfigTypes.h"
#include "config/ConfigValidator.h"

namespace ot {

enum class ConfigLoadResult : uint8_t {
    LOADED = 0,
    LOADED_AND_MIGRATED,
    DEFAULTS_NO_FILE,
    DEFAULTS_CORRUPT,
    DEFAULTS_TOO_NEW
};

class ConfigManager {
public:
    bool begin();   // mounts LittleFS
    bool mounted() const { return mounted_; }

    ConfigLoadResult load();
    bool save();
    bool factoryReset();

    InstrumentConfiguration& config() { return config_; }
    const InstrumentConfiguration& config() const { return config_; }

    // Replaces the whole configuration after validating it.  On an ERROR the
    // stored configuration is left untouched and `report` explains why.
    bool applyAndSave(const InstrumentConfiguration& candidate, ValidationReport& report);

    // Sets the Wi-Fi credentials, which never travel through the ordinary
    // configuration document.  An empty string clears the corresponding one.
    bool setWifiCredentials(const char* stationPassword, const char* apPassword);

    // Import/export used by the REST API.  Both are redacted: an exported file
    // can be shared without leaking the owner's network credentials.  `import`
    // goes through exactly the same validation as a normal save.
    size_t exportJson(char* out, size_t outSize) const;
    bool importJson(const char* json, size_t length, ValidationReport& report);

    const MigrationResult& lastMigration() const { return migration_; }
    // True when the stored configuration could not be trusted and the
    // instrument must come up in SAFE MODE (network + web UI only).
    bool safeModeRequired() const { return safeMode_; }
    void setSafeMode(bool state) { safeMode_ = state; }

    // Fills a brand new configuration: STANDARD preset, default routes,
    // board-specific pin map.
    static void makeDefaults(InstrumentConfiguration& cfg);

private:
    bool writeFile(const char* path, const char* data, size_t length);

    InstrumentConfiguration config_;
    MigrationResult migration_;
    bool mounted_ = false;
    bool safeMode_ = false;
};

}  // namespace ot
