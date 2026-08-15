// ============================================================================
//  dump_default_config - prints the factory configuration as JSON.
//
//  Used to keep documentation and the web UI mock in step with the firmware:
//  the JSON comes from the same C++ code the device runs, so it cannot drift.
//
//    g++ -std=gnu++17 -DOT_HOST_BUILD=1 -I src -I <ArduinoJson>/src \
//        tools/dump_default_config.cpp src/config/*.cpp src/audio/Profiles.cpp \
//        src/audio/Limiter.cpp src/midi/MidiRouter.cpp test/support/HostClock.cpp \
//        -o dump_default_config
// ============================================================================
#include <cstdio>
#include <string>

#include "config/ConfigManager.h"
#include "config/ConfigSerializer.h"

int main() {
    ot::initBoardCaps();
    ot::InstrumentConfiguration cfg;
    ot::ConfigManager::makeDefaults(cfg);

    JsonDocument doc;
    ot::configToJson(cfg, doc.to<JsonObject>());
    std::string out;
    serializeJsonPretty(doc, out);
    std::printf("%s\n", out.c_str());
    return 0;
}
