// ============================================================================
//  Pca9685Valve.h - servos behind a PCA9685 16 channel PWM expander.
//
//  Keeps the servo supply and its noise away from the ESP32 and frees the
//  LEDC channels.  The optional OE pin lets the firmware kill every output at
//  once, which is what emergencyStop() uses.
//
//  The register access is written in-tree (four registers, one I2C write) so
//  the project does not depend on an external library for something this small.
// ============================================================================
#pragma once

#include "valves/IValveActuator.h"
#include "valves/ServoMotion.h"

namespace ot {

class Pca9685Valve final : public IValveActuator {
public:
    void configureBus(const I2cPins& pins, uint8_t address, int8_t oePin, uint16_t frequencyHz);
    bool addValve(uint8_t valve, const ValveConfig& cfg);

    bool begin() override;
    void setValve(uint8_t valve, bool pressed) override;
    void releaseAll() override;
    void update() override;
    void emergencyStop() override;
    void enable() override;
    bool isPressed(uint8_t valve) const override;
    bool hasFault(uint8_t valve) const override;
    const char* faultText(uint8_t valve) const override;

    void previewAngle(uint8_t valve, uint16_t angle);
    float currentAngle(uint8_t valve) const;
    bool devicePresent() const { return present_; }

private:
    struct Slot {
        bool used = false;
        uint8_t channel = 0;
        ServoMotion motion;
    };

    bool writeRegister(uint8_t reg, uint8_t value);
    bool setPwm(uint8_t channel, uint16_t on, uint16_t off);
    void writePulse(Slot& slot);
    uint16_t pulseToTicks(uint16_t pulseUs) const;

    Slot slots_[kMaxValves];
    I2cPins bus_;
    uint8_t address_ = 0x40;
    int8_t oePin_ = -1;
    uint16_t frequencyHz_ = 50;
    uint32_t lastUpdateMs_ = 0;
    bool present_ = false;
    bool stopped_ = false;
    bool started_ = false;
};

}  // namespace ot
