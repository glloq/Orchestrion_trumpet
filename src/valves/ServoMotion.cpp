#include "valves/ServoMotion.h"

#include <cmath>

namespace ot {

void ServoMotion::configure(const ValveConfig& cfg) {
    cfg_ = cfg;
    angle_ = static_cast<float>(cfg.releasedAngle);
    target_ = angle_;
    velocity_ = 0.0f;
    pressed_ = false;
    moving_ = false;
    idleMs_ = 0;
}

float ServoMotion::minAngle() const {
    return cfg_.pressedAngle < cfg_.releasedAngle ? static_cast<float>(cfg_.pressedAngle)
                                                  : static_cast<float>(cfg_.releasedAngle);
}

float ServoMotion::maxAngle() const {
    return cfg_.pressedAngle > cfg_.releasedAngle ? static_cast<float>(cfg_.pressedAngle)
                                                  : static_cast<float>(cfg_.releasedAngle);
}

void ServoMotion::setTarget(bool pressed) {
    pressed_ = pressed;
    target_ = static_cast<float>(pressed ? cfg_.pressedAngle : cfg_.releasedAngle);
    if (std::fabs(target_ - angle_) > 0.05f) {
        moving_ = true;
        idleMs_ = 0;
    }
}

void ServoMotion::forceTo(bool pressed) {
    pressed_ = pressed;
    target_ = static_cast<float>(pressed ? cfg_.pressedAngle : cfg_.releasedAngle);
    angle_ = target_;
    velocity_ = 0.0f;
    moving_ = false;
    idleMs_ = 0;
}

void ServoMotion::update(uint32_t dtMs) {
    if (dtMs == 0) return;
    const float dt = static_cast<float>(dtMs) * 0.001f;

    if (!moving_) {
        if (idleMs_ < 0xFFFF0000u) idleMs_ += dtMs;
        return;
    }

    const float maxSpeed = cfg_.speedDegPerSec > 0 ? static_cast<float>(cfg_.speedDegPerSec)
                                                   : 1000.0f;
    const float accel = cfg_.accelDegPerSec2 > 0 ? static_cast<float>(cfg_.accelDegPerSec2)
                                                 : 100000.0f;

    const float delta = target_ - angle_;
    const float dist = std::fabs(delta);
    const float dir = delta >= 0.0f ? 1.0f : -1.0f;

    // Speed we are allowed to carry so we can still stop exactly on target.
    const float stopSpeed = std::sqrt(2.0f * accel * dist);
    float speed = std::fabs(velocity_) + accel * dt;
    if (speed > maxSpeed) speed = maxSpeed;
    if (speed > stopSpeed) speed = stopSpeed;

    float step = speed * dt;
    if (step >= dist) {
        angle_ = target_;
        velocity_ = 0.0f;
        moving_ = false;
        idleMs_ = 0;
    } else {
        angle_ += dir * step;
        velocity_ = dir * speed;
    }

    // Never command outside the calibrated travel, whatever the maths says.
    if (angle_ < minAngle()) angle_ = minAngle();
    if (angle_ > maxAngle()) angle_ = maxAngle();
}

bool ServoMotion::shouldDetach() const {
    if (!cfg_.detachAfterMove) return false;
    return !moving_ && idleMs_ >= cfg_.detachDelayMs;
}

uint16_t ServoMotion::pulseUs() const {
    float a = angle_;
    if (cfg_.invert) a = 180.0f - a;
    a = clampValue(a, 0.0f, 180.0f);
    const float span = static_cast<float>(cfg_.maxPulseUs - cfg_.minPulseUs);
    return static_cast<uint16_t>(cfg_.minPulseUs + (a / 180.0f) * span + 0.5f);
}

}  // namespace ot
