#include "valves/SolenoidSafety.h"

namespace ot {

namespace {
// A solenoid's thermal time constant is seconds, so the duty ceiling is about
// a *sustained* load.  Observing it over a window derived from maxOnMs alone
// would either be far too long (a 100 s maximum gives a 400 s window) or far
// too short, so the window is clamped to a physically sensible range.
constexpr uint32_t kMinWindowMs = 2000;
constexpr uint32_t kMaxWindowMs = 20000;
// And the ceiling is only enforced once enough time has been observed: a coil
// that has just been energised is legitimately at 100% duty, and firing on the
// very first note would make the instrument unplayable.
constexpr uint32_t kMinObservationMs = 2000;
}  // namespace

void SolenoidSafety::configure(const ValveConfig& cfg) {
    cfg_ = cfg;
    duty_ = 0;
    requested_ = false;
    energised_ = false;
    cooldown_ = false;
    windowStarted_ = false;
    fault_ = SolenoidFault::NONE;
    cooldownUntilMs_ = 0;
    onAccumulatorMs_ = 0;
    windowStartMs_ = 0;
    onSinceMs_ = 0;
    lastUpdateMs_ = 0;
    // Observe the duty cycle over roughly four maximum-on periods: long enough
    // to be meaningful, short enough to react before the coil saturates.
    windowLengthMs_ = cfg.maxOnMs ? static_cast<uint32_t>(cfg.maxOnMs) * 4u : 10000u;
    windowLengthMs_ = clampValue(windowLengthMs_, kMinWindowMs, kMaxWindowMs);
}

void SolenoidSafety::startWindowIfNeeded(uint32_t nowMs) {
    if (windowStarted_) return;
    windowStarted_ = true;
    windowStartMs_ = nowMs;
}

void SolenoidSafety::energise(uint32_t nowMs, uint8_t percent) {
    duty_ = percent > 100 ? 100 : percent;
    if (duty_ == 0) {
        deEnergise(nowMs);
        return;
    }
    if (!energised_) {
        energised_ = true;
        onSinceMs_ = nowMs;
    }
}

void SolenoidSafety::deEnergise(uint32_t nowMs) {
    if (energised_) onAccumulatorMs_ += nowMs - onSinceMs_;
    energised_ = false;
    duty_ = 0;
}

void SolenoidSafety::trip(SolenoidFault fault, uint32_t nowMs) {
    deEnergise(nowMs);
    fault_ = fault;
    cooldown_ = true;
    cooldownUntilMs_ = nowMs + (cfg_.cooldownMs ? cfg_.cooldownMs : 1000u);
}

void SolenoidSafety::request(bool pressed, uint32_t nowMs) {
    startWindowIfNeeded(nowMs);
    lastUpdateMs_ = nowMs;

    if (!pressed) {
        requested_ = false;
        deEnergise(nowMs);
        return;
    }

    if (requested_) return;   // already held, nothing to re-arm
    requested_ = true;

    // A pending cooldown or a latched fault must win over the MIDI request:
    // the note is remembered and applied when the coil has had time to cool.
    if (cooldown_) {
        duty_ = 0;
        return;
    }
    energise(nowMs, cfg_.pullInPwm);
}

void SolenoidSafety::update(uint32_t nowMs) {
    startWindowIfNeeded(nowMs);
    lastUpdateMs_ = nowMs;

    // Slide the duty observation window, keeping half of the previous one so
    // the measurement does not jump at every boundary.
    if (nowMs - windowStartMs_ >= windowLengthMs_) {
        uint32_t on = onAccumulatorMs_;
        if (energised_) on += nowMs - onSinceMs_;
        onAccumulatorMs_ = on / 2;
        windowStartMs_ = nowMs - windowLengthMs_ / 2;
        if (energised_) onSinceMs_ = nowMs;
    }

    if (cooldown_) {
        if (static_cast<int32_t>(nowMs - cooldownUntilMs_) < 0) return;
        cooldown_ = false;
        // The note may still be held: allow one fresh pull-in.
        if (requested_) energise(nowMs, cfg_.pullInPwm);
        return;
    }

    if (!requested_ || !energised_) return;

    const uint32_t heldMs = nowMs - onSinceMs_;

    // Hard thermal limit.  This must fire even if the MIDI note never ends,
    // which is exactly the case a stuck note or a dead controller produces.
    if (cfg_.maxOnMs > 0 && heldMs >= cfg_.maxOnMs) {
        trip(SolenoidFault::OVER_TIME, nowMs);
        return;
    }

    // Long term duty ceiling, once the window holds enough history to mean
    // anything.
    if (cfg_.maxDutyPercent > 0 && cfg_.maxDutyPercent < 100 &&
        (nowMs - windowStartMs_) >= kMinObservationMs &&
        measuredDutyPercent() > cfg_.maxDutyPercent) {
        trip(SolenoidFault::OVER_DUTY, nowMs);
        return;
    }

    // Pull-in finished -> drop to the hold level.  A zero hold level means
    // "pulse only": release cleanly instead of buzzing at 0%.
    if (heldMs >= cfg_.pullInMs) energise(nowMs, cfg_.holdPwm);
}

uint8_t SolenoidSafety::measuredDutyPercent() const {
    if (!windowStarted_) return 0;
    const uint32_t elapsed = lastUpdateMs_ - windowStartMs_;
    if (elapsed == 0) return 0;
    uint32_t on = onAccumulatorMs_;
    if (energised_) on += lastUpdateMs_ - onSinceMs_;
    if (on > elapsed) on = elapsed;
    return static_cast<uint8_t>((on * 100u) / elapsed);
}

}  // namespace ot
