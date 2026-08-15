// ============================================================================
//  Es8388.h - Everest ES8388 stereo codec (DAC + ADC).
//
//  Selected by the FEEDBACK preset: the ADC input is what a future acoustic
//  calibration loop (measurement microphone -> sweep -> EQ correction) will be
//  built on.  In this firmware version the ADC is initialised and the capture
//  channel is opened, but no measurement is performed yet - see docs/AUDIO.md.
//
//  Maturity: EXPERIMENTAL.  The register sequence follows the datasheet and
//  compiles for both targets, but the reference instrument uses a PCM5102A, so
//  this backend has not been validated on silicon by the project.
// ============================================================================
#pragma once

#include "audio/backends/CodecI2c.h"
#include "audio/backends/I2sBackendBase.h"

namespace ot {

class Es8388Backend final : public I2sBackendBase {
public:
    const char* name() const override { return "ES8388"; }
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
