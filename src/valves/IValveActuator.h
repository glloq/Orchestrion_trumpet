// ============================================================================
//  IValveActuator.h - the only thing the valve engine knows about hardware.
//
//  Servo, PCA9685 servo, solenoid and the test mock all implement it, which is
//  what makes a mixed servo/solenoid instrument possible without a single
//  branch in the MIDI path.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

class IValveActuator {
public:
    virtual ~IValveActuator() = default;

    virtual bool begin() = 0;
    virtual void setValve(uint8_t valve, bool pressed) = 0;
    virtual void releaseAll() = 0;
    // Called from the actuator task; drives motion ramps, solenoid PWM phases
    // and the thermal guard.  Must never block.
    virtual void update() = 0;

    // Cuts the drive completely (panic / emergency / OTA).  After this call
    // setValve() is ignored until enable() is called again.
    virtual void emergencyStop() {}
    virtual void enable() {}

    virtual bool isPressed(uint8_t valve) const = 0;
    // A fault latches until the condition clears and the user acknowledges.
    virtual bool hasFault(uint8_t valve) const { return false; }
    virtual const char* faultText(uint8_t valve) const { return nullptr; }
};

}  // namespace ot
