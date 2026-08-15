#include "audio/backends/Es8388.h"

#include "diagnostics/Logger.h"

namespace ot {

namespace {
// Register map subset used here (ES8388 datasheet, section 8).
constexpr uint8_t kChipControl1 = 0x00;
constexpr uint8_t kChipControl2 = 0x01;
constexpr uint8_t kChipPower = 0x02;
constexpr uint8_t kAdcPower = 0x03;
constexpr uint8_t kDacPower = 0x04;
constexpr uint8_t kAdcControl1 = 0x09;
constexpr uint8_t kAdcControl2 = 0x0A;
constexpr uint8_t kAdcControl3 = 0x0B;
constexpr uint8_t kAdcControl4 = 0x0C;
constexpr uint8_t kAdcControl5 = 0x0D;
constexpr uint8_t kAdcControl8 = 0x10;
constexpr uint8_t kAdcControl9 = 0x11;
constexpr uint8_t kDacControl1 = 0x17;
constexpr uint8_t kDacControl2 = 0x18;
constexpr uint8_t kDacControl3 = 0x19;
constexpr uint8_t kDacControl4 = 0x1A;   // LDAC volume
constexpr uint8_t kDacControl5 = 0x1B;   // RDAC volume
constexpr uint8_t kDacControl16 = 0x26;
constexpr uint8_t kDacControl17 = 0x27;
constexpr uint8_t kDacControl20 = 0x2A;
constexpr uint8_t kDacControl21 = 0x2B;
constexpr uint8_t kDacControl24 = 0x2E;  // LOUT1 volume
constexpr uint8_t kDacControl25 = 0x2F;  // ROUT1 volume
constexpr uint8_t kDacControl26 = 0x30;  // LOUT2 volume
constexpr uint8_t kDacControl27 = 0x31;  // ROUT2 volume
}  // namespace

bool Es8388Backend::preparePeripheral() {
    if (!bus_.begin(cfg_.i2c, cfg_.codecAddress)) {
        setError("ES8388 not found on I2C");
        return false;
    }
    return true;
}

bool Es8388Backend::startCodec() {
    if (!bus_.present()) return false;
    bool ok = true;

    // Power down everything first, then bring the blocks up in a defined order.
    ok = bus_.write8(kDacControl3, 0x04) && ok;   // DAC muted
    ok = bus_.write8(kChipControl2, 0x50) && ok;
    ok = bus_.write8(kChipPower, 0x00) && ok;     // all blocks powered
    // Slave mode: the ESP32 is the I2S master, exactly like every other
    // backend, so the audio task keeps owning the clock.
    ok = bus_.write8(kChipControl1, 0x0C) && ok;

    // ---- DAC path -------------------------------------------------------
    ok = bus_.write8(kDacPower, 0x3C) && ok;      // DAC + both line outputs
    ok = bus_.write8(kDacControl1, bitDepth_ >= 24 ? 0x00 : 0x18) && ok;  // I2S, 24/16 bit
    ok = bus_.write8(kDacControl2, 0x02) && ok;   // 256x oversampling
    ok = bus_.write8(kDacControl16, 0x00) && ok;
    ok = bus_.write8(kDacControl17, 0x90) && ok;  // LIN1 -> LOUT1 mixer
    ok = bus_.write8(kDacControl20, 0x90) && ok;
    ok = bus_.write8(kDacControl21, 0x80) && ok;
    ok = bus_.write8(kDacControl4, 0x00) && ok;   // 0 dB
    ok = bus_.write8(kDacControl5, 0x00) && ok;
    ok = bus_.write8(kDacControl24, 0x1E) && ok;  // line out level
    ok = bus_.write8(kDacControl25, 0x1E) && ok;
    ok = bus_.write8(kDacControl26, 0x1E) && ok;
    ok = bus_.write8(kDacControl27, 0x1E) && ok;

    // ---- ADC path (measurement microphone, prepared for calibration) ----
    ok = bus_.write8(kAdcPower, 0x00) && ok;
    ok = bus_.write8(kAdcControl1, 0x88) && ok;   // +24 dB microphone PGA
    ok = bus_.write8(kAdcControl2, 0xF0) && ok;   // differential LINPUT1/RINPUT1
    ok = bus_.write8(kAdcControl3, 0x02) && ok;
    ok = bus_.write8(kAdcControl4, bitDepth_ >= 24 ? 0x0C : 0x0D) && ok;
    ok = bus_.write8(kAdcControl5, 0x02) && ok;
    ok = bus_.write8(kAdcControl8, 0x00) && ok;
    ok = bus_.write8(kAdcControl9, 0x00) && ok;

    if (!ok) OT_LOGW("es8388", "some register writes were not acknowledged");
    return ok;
}

void Es8388Backend::stopCodec() {
    if (!bus_.present()) return;
    bus_.write8(kDacControl3, 0x04);   // mute
    bus_.write8(kChipPower, 0xFF);     // power down
}

void Es8388Backend::applyMute(bool state) {
    if (!bus_.present()) return;
    bus_.write8(kDacControl3, state ? 0x04 : 0x00);
}

}  // namespace ot
