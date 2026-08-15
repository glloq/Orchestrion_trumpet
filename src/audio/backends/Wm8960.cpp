#include "audio/backends/Wm8960.h"

#include "diagnostics/Logger.h"

namespace ot {

namespace {
constexpr uint8_t kLeftInputVolume = 0x00;
constexpr uint8_t kRightInputVolume = 0x01;
constexpr uint8_t kLeftHeadphoneVolume = 0x02;
constexpr uint8_t kRightHeadphoneVolume = 0x03;
constexpr uint8_t kClocking1 = 0x04;
constexpr uint8_t kAdcDacControl1 = 0x05;
constexpr uint8_t kAudioInterface1 = 0x07;
constexpr uint8_t kClocking2 = 0x08;
constexpr uint8_t kLeftDacVolume = 0x0A;
constexpr uint8_t kRightDacVolume = 0x0B;
constexpr uint8_t kReset = 0x0F;
constexpr uint8_t kPowerMgmt1 = 0x19;
constexpr uint8_t kPowerMgmt2 = 0x1A;
constexpr uint8_t kAdditional1 = 0x1B;
constexpr uint8_t kAdcLeftPath = 0x20;
constexpr uint8_t kAdcRightPath = 0x21;
constexpr uint8_t kLeftOutMix = 0x22;
constexpr uint8_t kRightOutMix = 0x25;
constexpr uint8_t kPowerMgmt3 = 0x2F;
constexpr uint8_t kLeftSpeakerVolume = 0x28;
constexpr uint8_t kRightSpeakerVolume = 0x29;
}  // namespace

bool Wm8960Backend::preparePeripheral() {
    if (!bus_.begin(cfg_.i2c, cfg_.codecAddress)) {
        setError("WM8960 not found on I2C");
        return false;
    }
    return true;
}

bool Wm8960Backend::startCodec() {
    if (!bus_.present()) return false;
    bool ok = true;

    ok = bus_.write9(kReset, 0x000) && ok;

    // Power: VMID at 50k, VREF, DAC + output mixers, ADC + input PGA.
    ok = bus_.write9(kPowerMgmt1, 0x0FE) && ok;
    ok = bus_.write9(kPowerMgmt2, 0x1F8) && ok;
    ok = bus_.write9(kPowerMgmt3, 0x03C) && ok;

    // I2S slave, the ESP32 stays the master clock source.
    // Bits [3:2] select the word length: 00 = 16 bit, 10 = 24 bit.
    ok = bus_.write9(kAudioInterface1, bitDepth_ >= 24 ? 0x00A : 0x002) && ok;
    ok = bus_.write9(kClocking1, 0x000) && ok;
    ok = bus_.write9(kClocking2, 0x1C4) && ok;

    // DAC path: unmuted, 0 dB, routed to both output mixers.
    ok = bus_.write9(kAdcDacControl1, 0x000) && ok;
    ok = bus_.write9(kLeftDacVolume, 0x1FF) && ok;
    ok = bus_.write9(kRightDacVolume, 0x1FF) && ok;
    ok = bus_.write9(kLeftOutMix, 0x100) && ok;
    ok = bus_.write9(kRightOutMix, 0x100) && ok;
    ok = bus_.write9(kLeftHeadphoneVolume, 0x179) && ok;
    ok = bus_.write9(kRightHeadphoneVolume, 0x179) && ok;
    ok = bus_.write9(kLeftSpeakerVolume, 0x179) && ok;
    ok = bus_.write9(kRightSpeakerVolume, 0x179) && ok;
    ok = bus_.write9(kAdditional1, 0x0C0) && ok;

    // ADC path, prepared for the measurement microphone.
    ok = bus_.write9(kLeftInputVolume, 0x13F) && ok;
    ok = bus_.write9(kRightInputVolume, 0x13F) && ok;
    ok = bus_.write9(kAdcLeftPath, 0x138) && ok;
    ok = bus_.write9(kAdcRightPath, 0x138) && ok;

    if (!ok) OT_LOGW("wm8960", "some register writes were not acknowledged");
    return ok;
}

void Wm8960Backend::stopCodec() {
    if (!bus_.present()) return;
    bus_.write9(kAdcDacControl1, 0x008);   // DAC soft mute
    bus_.write9(kPowerMgmt1, 0x000);
    bus_.write9(kPowerMgmt2, 0x000);
}

void Wm8960Backend::applyMute(bool state) {
    if (!bus_.present()) return;
    bus_.write9(kAdcDacControl1, state ? 0x008 : 0x000);
}

}  // namespace ot
