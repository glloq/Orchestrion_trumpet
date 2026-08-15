// ============================================================================
//  Wm8960.h - Wolfson/Cirrus WM8960 stereo codec (DAC + ADC).
//
//  Same role as the ES8388: line level output into an external amplifier plus
//  a microphone input reserved for the future acoustic calibration loop.
//  Registers are write-only 9 bit words, so the driver keeps a shadow copy is
//  not needed here: every register is written with its full value.
//
//  Maturity: EXPERIMENTAL (implemented from the datasheet, not validated on
//  silicon by the project).
// ============================================================================
#pragma once

#include "audio/backends/CodecI2c.h"
#include "audio/backends/I2sBackendBase.h"

namespace ot {

class Wm8960Backend final : public I2sBackendBase {
public:
    const char* name() const override { return "WM8960"; }
    bool supportsCapture() const override { return true; }
    BackendMaturity maturity() const override { return BackendMaturity::EXPERIMENTAL; }

protected:
    uint8_t maximumBitDepth() const override { return 32; }
    bool preparePeripheral() override;
    bool startCodec() override;
    void stopCodec() override;
    void applyMute(bool state) override;

private:
    CodecI2c bus_;
};

}  // namespace ot
