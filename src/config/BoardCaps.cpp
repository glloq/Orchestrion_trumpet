#include "config/BoardCaps.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <soc/soc_caps.h>
#endif

namespace ot {

namespace {

BoardCapabilities g_caps;
bool g_initialised = false;

// ---- ESP32-WROOM-32 -------------------------------------------------------
// 6..11 are the SPI flash lines on every WROOM module.
const uint8_t kEsp32Reserved[] = {6, 7, 8, 9, 10, 11};
// 34..39 have no output driver at all.
const uint8_t kEsp32InputOnly[] = {34, 35, 36, 37, 38, 39};
// Strapping pins and pins with a boot-time level requirement.
const uint8_t kEsp32Warn[] = {0, 2, 12, 15, 1, 3};

// ---- ESP32-S3 -------------------------------------------------------------
// 26..32 are SPI flash / PSRAM on the common N8R8 and N16R8 modules.
const uint8_t kEsp32S3Reserved[] = {26, 27, 28, 29, 30, 31, 32};
const uint8_t kEsp32S3Warn[] = {0, 3, 45, 46, 19, 20, 33, 34, 35, 36, 37, 43, 44};

}  // namespace

bool BoardCapabilities::isReserved(uint8_t pin) const {
    for (uint8_t i = 0; i < reservedPinCount; ++i) {
        if (reservedPins[i] == pin) return true;
    }
    return false;
}

bool BoardCapabilities::isInputOnly(uint8_t pin) const {
    for (uint8_t i = 0; i < inputOnlyPinCount; ++i) {
        if (inputOnlyPins[i] == pin) return true;
    }
    return false;
}

bool BoardCapabilities::isWarned(uint8_t pin) const {
    for (uint8_t i = 0; i < warnPinCount; ++i) {
        if (warnPins[i] == pin) return true;
    }
    return false;
}

bool BoardCapabilities::isValidGpio(int pin) const {
    if (pin < 0) return false;
    if (pin > gpioMax) return false;
    if (board == BoardType::ESP32 && pin >= 20 && pin <= 21) return pin == 21;
    return true;
}

bool BoardCapabilities::supportsBackend(AudioBackendType b) const {
    switch (b) {
        case AudioBackendType::ESP32_INTERNAL_DAC:
            return hasInternalDac;
        case AudioBackendType::NONE:
            return true;
        default:
            return hasI2s;
    }
}

void initBoardCaps() {
    if (g_initialised) return;
    g_initialised = true;

#if defined(OT_HOST_BUILD)
    // Host tests exercise the S3 feature set: it is the superset.
    g_caps.board = BoardType::ESP32_S3;
    g_caps.chipName = "host";
    g_caps.cores = 2;
    g_caps.gpioMax = 48;
    g_caps.hasNativeUsb = true;
    g_caps.hasInternalDac = false;
    g_caps.reservedPins = kEsp32S3Reserved;
    g_caps.reservedPinCount = sizeof(kEsp32S3Reserved);
    g_caps.warnPins = kEsp32S3Warn;
    g_caps.warnPinCount = sizeof(kEsp32S3Warn);
#else
    esp_chip_info_t info;
    esp_chip_info(&info);
    g_caps.cores = info.cores;

    uint32_t flashSize = 0;
    esp_flash_get_size(nullptr, &flashSize);
    g_caps.flashSizeBytes = flashSize;
    g_caps.psramSizeBytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    g_caps.hasPsram = g_caps.psramSizeBytes > 0;

#if CONFIG_IDF_TARGET_ESP32S3
    g_caps.board = BoardType::ESP32_S3;
    g_caps.chipName = "ESP32-S3";
    g_caps.gpioMax = 48;
    g_caps.reservedPins = kEsp32S3Reserved;
    g_caps.reservedPinCount = sizeof(kEsp32S3Reserved);
    g_caps.warnPins = kEsp32S3Warn;
    g_caps.warnPinCount = sizeof(kEsp32S3Warn);
#elif CONFIG_IDF_TARGET_ESP32
    g_caps.board = BoardType::ESP32;
    g_caps.chipName = "ESP32";
    g_caps.gpioMax = 39;
    g_caps.reservedPins = kEsp32Reserved;
    g_caps.reservedPinCount = sizeof(kEsp32Reserved);
    g_caps.inputOnlyPins = kEsp32InputOnly;
    g_caps.inputOnlyPinCount = sizeof(kEsp32InputOnly);
    g_caps.warnPins = kEsp32Warn;
    g_caps.warnPinCount = sizeof(kEsp32Warn);
#else
    g_caps.board = BoardType::UNKNOWN;
    g_caps.chipName = "unsupported";
#endif

    // A class compliant USB-MIDI device requires the USB-OTG peripheral plus
    // the TinyUSB MIDI class compiled into the SDK.  On the plain ESP32 both
    // are missing, so the web UI hides USB entirely.
#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_TINYUSB_MIDI_ENABLED)
    g_caps.hasNativeUsb = true;
#else
    g_caps.hasNativeUsb = false;
#endif

#if SOC_DAC_SUPPORTED
    g_caps.hasInternalDac = true;
#else
    g_caps.hasInternalDac = false;
#endif

#if SOC_BLE_SUPPORTED
    g_caps.hasBle = true;
#else
    g_caps.hasBle = false;
#endif
#endif  // OT_HOST_BUILD
}

const BoardCapabilities& boardCaps() {
    if (!g_initialised) initBoardCaps();
    return g_caps;
}

}  // namespace ot
