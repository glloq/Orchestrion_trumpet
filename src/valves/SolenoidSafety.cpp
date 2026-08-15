#include "valves/SolenoidSafety.h"

namespace ot {

void SolenoidSafety::configure(const ValveConfig& cfg) {
    cfg_ = cfg;
    duty_ = 0;
    requested_ = false;
    fault_ = SolenoidFault::NONE;
    cooldownUntilMs_ = 0;
    onAccumulatorMs_ = 0;
    windowStartMs_ = 0;
    // Observe the duty cycle over roughly four maximum-on periods: long enough
    // to be meaningful, short enough to react before the coil saturates.
    windowLengthMs_ = cfg.maxOnMs ? static_cast<uint32_t>(cfg.maxOnMs) * 4u : 10000u;
    if (windowLengthMs_ < 2000u) windowLengthMs_ = 2000u;
}

void SolenoidSafety::energiseOff(uint32_t nowMs) {
    if (duty_ > 0 && onSinceMs_ != 0) {
        onAccumulatorMs_ += nowMs - onSinceMs_;
    }
    duty_ = 0;
    onSinceMs_ = 0;
}

void SolenoidSafety::request(bool pressed, uint32_t nowMs) {
    if (windowStartMs_ == 0) windowStartMs_ = nowMs;
    lastUpdateMs_ = nowMs;

    if (!pressed) {
        requested_ = false;
        energiseOff(nowMs);
        return;
    }

    if (requested_) return;  // already held, nothing to re-arm

    // A pending cooldown or a latched fault must win over the MIDI request.
    if (cooldownUntilMs_ != 0 && nowMs < cooldownUntilMs_) {
        requested_ = true;   // remembered, applied when the cooldown expires
        duty_ = 0;
        return;
    }

    requested_ = true;
    onSinceMs_ = nowMs;
    duty_ = cfg_.pullInPwm;
    if (duty_ > 100) duty_ = 100;
}

void SolenoidSafety::update(uint32_t nowMs) {
    if (windowStartMs_ == 0) windowStartMs_ = nowMs;
    lastUpdateMs_ = nowMs;

    // Slide the duty observation window.
    if (nowMs - windowStartMs_ >= windowLengthMs_) {
        uint32_t on = onAccumulatorMs_;
        if (duty_ > 0 && onSinceMs_ != 0) on += nowMs - onSinceMs_;
        // Keep half of the previous window so the measure does not jump.
        onAccumulatorMs_ = on / 2;
        windowStartMs_ = nowMs - windowLengthMs_ / 2;
        if (duty_ > 0) onSinceMs_ = nowMs;
    }

    if (cooldownUntilMs_ != 0) {
        if (nowMs >= cooldownUntilMs_) {
            cooldownUntilMs_ = 0;
            if (requested_) {
                // The note is still held: allow one fresh pull-in.
                onSinceMs_ = nowMs;
                duty_ = cfg_.pullInPwm > 100 ? 100 : cfg_.pullInPwm;
            }
        }
        return;
    }

    if (!requested_ || duty_ == 0) return;

    const uint32_t heldMs = nowMs - onSinceMs_;

    // Hard thermal limit: this must fire even if the MIDI note never ends.
    if (cfg_.maxOnMs > 0 && heldMs >= cfg_.maxOnMs) {
        energiseOff(nowMs);
        fault_ = SolenoidFault::OVER_TIME;
        cooldownUntilMs_ = nowMs + (cfg_.cooldownMs ? cfg_.cooldownMs : 1000u);
        return;
    }

    // Long term duty ceiling.
    if (cfg_.maxDutyPercent > 0 && cfg_.maxDutyPercent < 100 &&
        measuredDutyPercent() > cfg_.maxDutyPercent) {
        energiseOff(nowMs);
        fault_ = SolenoidFault::OVER_DUTY;
        cooldownUntilMs_ = nowMs + (cfg_.cooldownMs ? cfg_.cooldownMs : 1000u);
        return;
    }

    // Pull-in finished -> drop to the hold level.
    if (heldMs >= cfg_.pullInMs) {
        duty_ = cfg_.holdPwm > 100 ? 100 : cfg_.holdPwm;
        if (duty_ == 0) {
            // A zero hold level means "pulse only": release cleanly.
            energiseOff(nowMs);
        }
    }
}

uint8_t SolenoidSafety::measuredDutyPercent() const {
    uint32_t elapsed = lastUpdateMs_ - windowStartMs_;
    if (elapsed == 0) return 0;
    uint32_t on = onAccumulatorMs_;
    if (duty_ > 0 && onSinceMs_ != 0 && lastUpdateMs_ >= onSinceMs_) {
        on += lastUpdateMs_ - onSinceMs_;
    }
    if (on > elapsed) on = elapsed;
    return static_cast<uint8_t>((on * 100u) / elapsed);
}

}  // namespace ot
