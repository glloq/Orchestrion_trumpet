// ============================================================================
//  Tas5760.h - TI TAS5760M, I2S class-D amplifier.
//
//    ESP32 --I2S--> TAS5760M --> speaker
//
//  This is the target of the future integrated PCB: one chip replaces the DAC
//  and the amplifier.  The part works in hardware control mode without any
//  I2C, so the driver treats the control bus as optional: when the codec
//  answers, volume/mute go through the registers; otherwise SPK_SD is used as
//  a hardware mute and everything still works.
//
//  Maturity: EXPERIMENTAL (no board of the project carries the part yet).
// ============================================================================
#pragma once

#include "audio/backends/CodecI2c.h"
#include "audio/backends/I2sBackendBase.h"

namespace ot {

class Tas5760Backend final : public I2sBackendBase {
public:
    const char* name() const override { return "TAS5760M"; }
    BackendMaturity maturity() const override { return BackendMaturity::EXPERIMENTAL; }

protected:
    uint8_t maximumBitDepth() const override { return 32; }
    bool preparePeripheral() override;
    bool startCodec() override;
    void stopCodec() override;
    void applyMute(bool state) override;

private:
    CodecI2c bus_;
    bool i2cControl_ = false;
};

}  // namespace ot
