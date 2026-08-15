// ============================================================================
//  Max98357.h - Maxim MAX98357A, I2S class-D amplifier with the speaker
//  directly attached.
//
//    ESP32 --I2S--> MAX98357A --> 4 ohm speaker
//
//  The part only accepts 16 bit I2S data reliably on the cheap breakout
//  modules, so this backend caps the bit depth: the web UI shows 16 bit and
//  the user is never told they got 24.
//  SD_MODE doubles as the shutdown pin and is used as a hardware mute.
// ============================================================================
#pragma once

#include "audio/backends/I2sBackendBase.h"

namespace ot {

class Max98357Backend final : public I2sBackendBase {
public:
    const char* name() const override { return "MAX98357A"; }

protected:
    uint8_t maximumBitDepth() const override { return 16; }
    bool preparePeripheral() override;
    void applyMute(bool state) override;
};

}  // namespace ot
