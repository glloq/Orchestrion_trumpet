// ============================================================================
//  SolenoidSafety.h - thermal and duty-cycle guard for one solenoid.
//
//  A solenoid held at 100% will cook itself in seconds.  This class owns the
//  whole safety story and is deliberately free of any hardware call so it can
//  be unit tested: give it the elapsed time and the requested state, it tells
//  you the PWM duty to apply.
//
//  Rules enforced:
//    * pull-in at full power for a short burst, then drop to the hold level;
//    * a hard maximum continuous ON time -> release + latched fault;
//    * a long term duty cycle ceiling -> forced cooldown;
//    * the guard keeps running even if MIDI stops arriving.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

enum class SolenoidFault : uint8_t { NONE = 0, OVER_TIME, OVER_DUTY };

class SolenoidSafety {
public:
    void configure(const ValveConfig& cfg);

    // `nowMs` is a monotonic millisecond clock.
    void request(bool pressed, uint32_t nowMs);
    void update(uint32_t nowMs);

    // 0..100 percent of the PWM period the gate should be on.
    uint8_t dutyPercent() const { return duty_; }
    bool isEnergised() const { return duty_ > 0; }
    bool pressedRequested() const { return requested_; }

    SolenoidFault fault() const { return fault_; }
    bool inCooldown() const { return cooldownUntilMs_ != 0; }
    void clearFault() { fault_ = SolenoidFault::NONE; }

    // Rolling ON ratio over the observation window, 0..100.
    uint8_t measuredDutyPercent() const;

private:
    void energiseOff(uint32_t nowMs);

    ValveConfig cfg_;
    bool requested_ = false;
    uint32_t onSinceMs_ = 0;
    uint32_t lastUpdateMs_ = 0;
    uint32_t cooldownUntilMs_ = 0;
    uint32_t windowStartMs_ = 0;
    uint32_t onAccumulatorMs_ = 0;
    uint32_t windowLengthMs_ = 10000;
    uint8_t duty_ = 0;
    SolenoidFault fault_ = SolenoidFault::NONE;
};

}  // namespace ot
