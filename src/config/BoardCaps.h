// ============================================================================
//  BoardCaps.h - what the silicon in front of us can actually do.
//
//  The web UI only ever offers features reported here, which is how one single
//  firmware image can serve an ESP32-WROOM (no native USB, has an internal
//  DAC) and an ESP32-S3 (native USB-MIDI, no internal DAC) without the user
//  ever picking a "board type" that could be wrong.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

struct BoardCapabilities {
    BoardType board = BoardType::UNKNOWN;
    const char* chipName = "unknown";
    uint8_t cores = 1;
    uint32_t flashSizeBytes = 0;
    uint32_t psramSizeBytes = 0;
    uint8_t gpioMax = 39;

    bool hasWifi = true;
    bool hasBle = true;
    bool hasNativeUsb = false;      // true class-compliant USB-MIDI possible
    bool hasInternalDac = false;    // 8 bit DAC on GPIO25/26 (ESP32 classic)
    bool hasI2s = true;
    bool hasPsram = false;
    bool hasTouchPins = true;

    // GPIO that must never be exposed to the user for a peripheral.
    const uint8_t* reservedPins = nullptr;
    uint8_t reservedPinCount = 0;
    // GPIO that are input-only (cannot drive a servo/solenoid/I2S signal).
    const uint8_t* inputOnlyPins = nullptr;
    uint8_t inputOnlyPinCount = 0;
    // GPIO that work but carry a caveat (strapping, USB, PSRAM on some modules).
    const uint8_t* warnPins = nullptr;
    uint8_t warnPinCount = 0;

    bool isReserved(uint8_t pin) const;
    bool isInputOnly(uint8_t pin) const;
    bool isWarned(uint8_t pin) const;
    bool isValidGpio(int pin) const;
    bool supportsBackend(AudioBackendType b) const;
};

// Detected once at boot from the compile target and the runtime chip info.
const BoardCapabilities& boardCaps();
void initBoardCaps();

}  // namespace ot
