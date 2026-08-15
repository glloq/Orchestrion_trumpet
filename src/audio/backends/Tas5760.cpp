#include "audio/backends/Tas5760.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#endif

namespace ot {

namespace {
constexpr uint8_t kPowerControl = 0x01;
constexpr uint8_t kDigitalControl = 0x02;
constexpr uint8_t kVolumeControlCfg = 0x03;
constexpr uint8_t kLeftChannelVolume = 0x04;
constexpr uint8_t kRightChannelVolume = 0x05;
constexpr uint8_t kAnalogControl = 0x06;
constexpr uint8_t kFaultCfg = 0x08;
}  // namespace

bool Tas5760Backend::preparePeripheral() {
#if !defined(OT_HOST_BUILD)
    if (cfg_.sdModePin >= 0) {
        // SPK_SD low = amplifier in shutdown.  Hold it there while the I2S
        // clocks start so the speaker never hears the transient.
        pinMode(static_cast<uint8_t>(cfg_.sdModePin), OUTPUT);
        digitalWrite(static_cast<uint8_t>(cfg_.sdModePin), LOW);
    }
#endif
    i2cControl_ = bus_.begin(cfg_.i2c, cfg_.codecAddress);
    if (!i2cControl_) {
        OT_LOGW("tas5760", "no I2C answer at 0x%02X, falling back to hardware control mode",
                cfg_.codecAddress);
    }
    return true;
}

bool Tas5760Backend::startCodec() {
    if (!i2cControl_) return true;   // hardware control mode: nothing to do
    bool ok = true;
    ok = bus_.write8(kPowerControl, 0xFE) && ok;      // sleep off, SPK_SD honoured
    ok = bus_.write8(kDigitalControl, bitDepth_ >= 24 ? 0x05 : 0x04) && ok;  // I2S 24/16 bit
    ok = bus_.write8(kVolumeControlCfg, 0x80) && ok;  // fade enabled, muted
    ok = bus_.write8(kLeftChannelVolume, 0xCF) && ok; // 0 dB
    ok = bus_.write8(kRightChannelVolume, 0xCF) && ok;
    ok = bus_.write8(kAnalogControl, 0x51) && ok;     // PWM rate, 19.2 dB gain
    ok = bus_.write8(kFaultCfg, 0x00) && ok;          // over-current protection on
    if (!ok) OT_LOGW("tas5760", "some register writes were not acknowledged");
    return ok;
}

void Tas5760Backend::stopCodec() {
    applyMute(true);
    if (i2cControl_) bus_.write8(kPowerControl, 0x00);
}

void Tas5760Backend::applyMute(bool state) {
    if (i2cControl_) {
        // Bit 0 of the volume configuration register is the soft mute.
        bus_.write8(kVolumeControlCfg, state ? 0x81 : 0x80);
    }
#if !defined(OT_HOST_BUILD)
    if (cfg_.sdModePin >= 0) {
        digitalWrite(static_cast<uint8_t>(cfg_.sdModePin), state ? LOW : HIGH);
    }
#endif
}

}  // namespace ot
