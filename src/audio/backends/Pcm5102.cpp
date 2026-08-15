#include "audio/backends/Pcm5102.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#endif

namespace ot {

bool Pcm5102Backend::preparePeripheral() {
#if !defined(OT_HOST_BUILD)
    if (cfg_.sdModePin >= 0) {
        // XSMT is active low: hold the DAC muted until the audio task is ready
        // so the amplifier never sees the I2S start-up transient.
        pinMode(static_cast<uint8_t>(cfg_.sdModePin), OUTPUT);
        digitalWrite(static_cast<uint8_t>(cfg_.sdModePin), LOW);
    }
#endif
    return true;
}

void Pcm5102Backend::applyMute(bool state) {
#if !defined(OT_HOST_BUILD)
    if (cfg_.sdModePin >= 0) {
        digitalWrite(static_cast<uint8_t>(cfg_.sdModePin), state ? LOW : HIGH);
    }
#else
    (void)state;
#endif
}

}  // namespace ot
