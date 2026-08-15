// ============================================================================
//  ConfigMigration.h - keep old configurations alive across firmware updates.
//
//  A user who calibrated three servos must never lose that work because the
//  schema gained a field.  Each step upgrades one version to the next and the
//  chain is applied until the configuration reaches kConfigSchemaVersion.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

struct MigrationResult {
    uint16_t fromVersion = 0;
    uint16_t toVersion = 0;
    bool migrated = false;
    bool unsupported = false;   // newer than this firmware understands
};

MigrationResult migrateConfig(InstrumentConfiguration& cfg);

}  // namespace ot
