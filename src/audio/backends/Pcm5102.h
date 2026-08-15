// ============================================================================
//  Pcm5102.h - TI PCM5102A, the reference DAC of the project.
//
//    ESP32 --I2S--> PCM5102A --line level--> external amplifier (TPA3118D2)
//
//  No control bus: the chip auto-detects the format and its filters are set by
//  strapping pins on the module (FLT/DEMP/XSMT/FMT).  XSMT can optionally be
//  wired to a GPIO, in which case the firmware uses it as a hardware mute -
//  that is what `sdModePin` means for this backend.
// ============================================================================
#pragma once

#include "audio/backends/I2sBackendBase.h"

namespace ot {

class Pcm5102Backend final : public I2sBackendBase {
public:
    const char* name() const override { return "PCM5102A"; }

protected:
    uint8_t maximumBitDepth() const override { return 32; }
    bool preparePeripheral() override;
    void applyMute(bool state) override;
};

}  // namespace ot
