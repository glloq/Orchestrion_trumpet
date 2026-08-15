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

// Wi-Fi passwords are stored on the device but must never leave it: anyone who
// can reach the hotspot could otherwise read the credentials of the user's home
// network from /api/config or an exported file.  The on-disk store is the only
// place written with INCLUDE.
enum class SecretPolicy : uint8_t { REDACT = 0, INCLUDE };

void configToJson(const InstrumentConfiguration& cfg, JsonObject root,
                  SecretPolicy secrets = SecretPolicy::REDACT);
// Returns false only when the document is not a usable configuration at all
// (wrong type, unreadable schema version).
bool configFromJson(JsonObjectConst root, InstrumentConfiguration& cfg);

// Copies the secrets of `previous` into `cfg` wherever `cfg` carries none.
// A configuration that came back from the web UI has redacted passwords, so
// saving it must not wipe the ones already stored.
void preserveSecrets(InstrumentConfiguration& cfg, const InstrumentConfiguration& previous);

// Convenience helpers used by the REST API.
size_t serializeConfig(const InstrumentConfiguration& cfg, char* out, size_t outSize,
                       SecretPolicy secrets = SecretPolicy::REDACT);
bool deserializeConfig(const char* json, size_t length, InstrumentConfiguration& cfg);

}  // namespace ot
