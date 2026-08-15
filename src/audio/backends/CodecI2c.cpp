#include "audio/backends/CodecI2c.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <Wire.h>
#endif

namespace ot {

bool CodecI2c::begin(const I2cPins& pins, uint8_t address) {
    pins_ = pins;
    address_ = address;
#if defined(OT_HOST_BUILD)
    present_ = false;
    return false;
#else
    Wire.begin(pins.sda, pins.scl, pins.frequency);
    Wire.beginTransmission(address_);
    present_ = Wire.endTransmission() == 0;
    if (!present_) {
        OT_LOGE("codec", "no device at 0x%02X (SDA %d SCL %d)", address_, pins.sda, pins.scl);
    }
    return present_;
#endif
}

bool CodecI2c::write8(uint8_t reg, uint8_t value) {
#if defined(OT_HOST_BUILD)
    (void)reg;
    (void)value;
    return false;
#else
    if (!present_) return false;
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
#endif
}

bool CodecI2c::read8(uint8_t reg, uint8_t& value) {
#if defined(OT_HOST_BUILD)
    (void)reg;
    (void)value;
    return false;
#else
    if (!present_) return false;
    Wire.beginTransmission(address_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(address_, static_cast<uint8_t>(1)) != 1) return false;
    value = static_cast<uint8_t>(Wire.read());
    return true;
#endif
}

bool CodecI2c::write9(uint8_t reg, uint16_t value) {
#if defined(OT_HOST_BUILD)
    (void)reg;
    (void)value;
    return false;
#else
    if (!present_) return false;
    Wire.beginTransmission(address_);
    Wire.write(static_cast<uint8_t>((reg << 1) | ((value >> 8) & 0x01)));
    Wire.write(static_cast<uint8_t>(value & 0xFF));
    return Wire.endTransmission() == 0;
#endif
}

}  // namespace ot
