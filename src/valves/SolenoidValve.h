// ============================================================================
//  SolenoidValve.h - solenoids through logic level MOSFETs.
//
//  Hardware (see docs/VALVES.md): ESP32 GPIO -> gate resistor -> logic level
//  MOSFET -> solenoid, with a flyback diode across the coil, a fuse and bulk
//  decoupling on the solenoid supply.  A solenoid is NEVER powered from the
//  ESP32 regulator.
//
//  All the timing and thermal behaviour lives in SolenoidSafety; this class
//  only turns the resulting duty into PWM.
// ============================================================================
#pragma once

#include "valves/IValveActuator.h"
#include "valves/SolenoidSafety.h"

namespace ot {

class SolenoidValve final : public IValveActuator {
public:
    bool addValve(uint8_t valve, const ValveConfig& cfg, uint16_t pwmFrequencyHz);

    bool begin() override;
    void setValve(uint8_t valve, bool pressed) override;
    void releaseAll() override;
    void update() override;
    void emergencyStop() override;
    void enable() override;
    bool isPressed(uint8_t valve) const override;
    bool hasFault(uint8_t valve) const override;
    const char* faultText(uint8_t valve) const override;

    void clearFaults();
    uint8_t dutyPercent(uint8_t valve) const;

private:
    struct Slot {
        bool used = false;
        bool attached = false;
        int8_t pin = -1;
        bool activeHigh = true;
        SolenoidSafety safety;
    };

    void applyDuty(Slot& slot);

    Slot slots_[kMaxValves];
    uint16_t pwmFrequencyHz_ = 20000;
    bool stopped_ = false;
    bool started_ = false;
};

}  // namespace ot
