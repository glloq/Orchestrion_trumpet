// ============================================================================
//  ServoMotion.h - trapezoidal motion planner for one servo.
//
//  Sending a raw target angle makes a hobby servo slam into its end stop,
//  which is loud and eventually breaks the linkage.  This planner ramps the
//  commanded angle with a configurable speed and acceleration, clamps to the
//  calibrated travel and tells the driver when the movement is finished so the
//  PWM can be detached (silence, less current, less heat).
//
//  Pure maths, no hardware: unit tested on the host.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

class ServoMotion {
public:
    void configure(const ValveConfig& cfg);

    void setTarget(bool pressed);
    // `dtMs` is the time since the previous call.
    void update(uint32_t dtMs);

    float angle() const { return angle_; }
    bool isMoving() const { return moving_; }
    bool targetPressed() const { return pressed_; }
    // True once the movement has been finished for longer than the configured
    // detach delay: the driver may stop generating pulses.
    bool shouldDetach() const;

    uint16_t pulseUs() const;
    void forceTo(bool pressed);

private:
    float minAngle() const;
    float maxAngle() const;

    ValveConfig cfg_;
    float angle_ = 0.0f;
    float target_ = 0.0f;
    float velocity_ = 0.0f;
    uint32_t idleMs_ = 0;
    bool pressed_ = false;
    bool moving_ = false;
};

}  // namespace ot
