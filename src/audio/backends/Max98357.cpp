#include "audio/backends/Max98357.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#endif

namespace ot {

bool Max98357Backend::preparePeripheral() {
#if !defined(OT_HOST_BUILD)
    if (cfg_.sdModePin >= 0) {
        pinMode(static_cast<uint8_t>(cfg_.sdModePin), OUTPUT);
        digitalWrite(static_cast<uint8_t>(cfg_.sdModePin), LOW);  // shutdown
    }
#endif
    return true;
}

void Max98357Backend::applyMute(bool state) {
#if !defined(OT_HOST_BUILD)
    if (cfg_.sdModePin >= 0) {
        // Driving SD_MODE high leaves the module in its default (left channel)
        // gain setting; low shuts the amplifier down completely.
        digitalWrite(static_cast<uint8_t>(cfg_.sdModePin), state ? LOW : HIGH);
    }
#else
    (void)state;
#endif
}

}  // namespace ot
