// ============================================================================
//  Orchestrion Trumpet - ESP32 modular MIDI trumpet
//
//  main() does almost nothing: AppController owns the boot sequence and the
//  FreeRTOS tasks.  loop() is the lowest priority context in the firmware and
//  is used for telemetry only - never for audio, MIDI or valve timing.
// ============================================================================
#include <Arduino.h>

#include "core/AppController.h"
#include "diagnostics/Logger.h"

void setup() {
    Serial.begin(115200);
    // Do not wait for a serial monitor: the instrument must boot standalone.
    delay(50);

    OT_LOGI("main", "Orchestrion Trumpet %s", OT_FIRMWARE_VERSION);
    ot::AppController::instance().begin();
}

void loop() {
    ot::AppController::instance().tick();
    // The Arduino loop task is priority 1: yielding here gives the whole core
    // back to the audio, MIDI and actuator tasks.
    delay(20);
}
