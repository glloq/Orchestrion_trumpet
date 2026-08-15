#include "audio/backends/NullBackend.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace ot {

void NullBackend::writeSamples(const int16_t* buffer, size_t count) {
    (void)buffer;
    if (count == 0 || cfg_.sampleRate == 0) return;
#if !defined(OT_HOST_BUILD)
    // No DMA to block on, so pace the audio task on the FreeRTOS clock.  This
    // is a vTaskDelay and not a delay(): the CPU is released to the MIDI, valve
    // and network tasks exactly as it would be with a real backend.
    const uint32_t blockMs = (count * 1000UL) / cfg_.sampleRate;
    vTaskDelay(pdMS_TO_TICKS(blockMs ? blockMs : 1));
#endif
}

}  // namespace ot
