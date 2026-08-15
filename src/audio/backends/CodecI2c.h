// ============================================================================
//  CodecI2c.h - the two register access shapes used by the audio codecs.
//
//  ES8388 and TAS5760 use 8 bit register / 8 bit value.
//  WM8960 uses a 7 bit register with a 9 bit value packed into two bytes.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

class CodecI2c {
public:
    bool begin(const I2cPins& pins, uint8_t address);
    bool present() const { return present_; }
    uint8_t address() const { return address_; }

    bool write8(uint8_t reg, uint8_t value);
    bool read8(uint8_t reg, uint8_t& value);
    // WM8960 style: 7 bit register, 9 bit data.
    bool write9(uint8_t reg, uint16_t value);

private:
    I2cPins pins_;
    uint8_t address_ = 0;
    bool present_ = false;
};

}  // namespace ot
