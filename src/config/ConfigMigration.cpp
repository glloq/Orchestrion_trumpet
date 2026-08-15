#include "config/ConfigMigration.h"

#include "midi/MidiRouter.h"

namespace ot {

namespace {

// v1 -> v2
//   * v1 had no MIDI routing matrix at all: inputs were hard wired to both
//     engines.  Rebuild that behaviour explicitly so the instrument keeps
//     playing exactly as before the update.
//   * v1 stored no acoustic coupling; the reference build has always been a
//     sealed chamber, so that is the honest default for an existing user.
void upgrade1to2(InstrumentConfiguration& cfg) {
    if (cfg.midi.routeCount == 0) MidiRouter::makeDefaultRoutes(cfg.midi);
    if (cfg.acoustic.chamberVolumeMl <= 0.0f) {
        cfg.acoustic = AcousticConfig();
    }
    cfg.schemaVersion = 2;
}

}  // namespace

MigrationResult migrateConfig(InstrumentConfiguration& cfg) {
    MigrationResult result;
    result.fromVersion = cfg.schemaVersion;
    result.toVersion = cfg.schemaVersion;

    if (cfg.schemaVersion > kConfigSchemaVersion) {
        // A configuration written by a newer firmware.  Refuse to guess: the
        // caller falls back to the defaults and tells the user.
        result.unsupported = true;
        return result;
    }

    while (cfg.schemaVersion < kConfigSchemaVersion) {
        const uint16_t before = cfg.schemaVersion;
        switch (cfg.schemaVersion) {
            case 0:
            case 1:
                upgrade1to2(cfg);
                break;
            default:
                cfg.schemaVersion = kConfigSchemaVersion;
                break;
        }
        result.migrated = true;
        if (cfg.schemaVersion <= before) {
            // Defensive: never loop forever on a malformed version number.
            cfg.schemaVersion = kConfigSchemaVersion;
            break;
        }
    }

    result.toVersion = cfg.schemaVersion;
    return result;
}

}  // namespace ot
