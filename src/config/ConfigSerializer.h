// ============================================================================
//  ConfigSerializer.h - InstrumentConfiguration <-> JSON.
//
//  Kept separate from ConfigManager so it can be unit tested on the host
//  without a filesystem.  Unknown members are ignored and missing members keep
//  their default, which is what makes forward and backward compatibility work
//  together with ConfigMigration.
// ============================================================================
#pragma once

#include <ArduinoJson.h>

#include "config/ConfigTypes.h"

namespace ot {

void configToJson(const InstrumentConfiguration& cfg, JsonObject root);
// Returns false only when the document is not a usable configuration at all
// (wrong type, unreadable schema version).
bool configFromJson(JsonObjectConst root, InstrumentConfiguration& cfg);

// Convenience helpers used by the REST API.
size_t serializeConfig(const InstrumentConfiguration& cfg, char* out, size_t outSize);
bool deserializeConfig(const char* json, size_t length, InstrumentConfiguration& cfg);

}  // namespace ot
