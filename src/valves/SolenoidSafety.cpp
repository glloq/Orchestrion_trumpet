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

// Integer square root, so the thermal integral can be converted back into an
// equivalent duty without pulling <cmath> into the valve task.
uint32_t isqrt(uint32_t value) {
    uint32_t result = 0;
    uint32_t bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}
}  // namespace

void SolenoidSafety::configure(const ValveConfig& cfg) {
    cfg_ = cfg;
    duty_ = 0;
    requested_ = false;
    cooldown_ = false;
    windowStarted_ = false;
    fault_ = SolenoidFault::NONE;
    cooldownUntilMs_ = 0;
    loadAccumulatorPct2Ms_ = 0;
    loadSinceMs_ = 0;
    windowStartMs_ = 0;
    continuousOnSinceMs_ = 0;
    lastUpdateMs_ = 0;
    // Observe the thermal load over roughly four maximum-on periods: long
    // enough to be meaningful, short enough to react before the coil saturates.
    windowLengthMs_ = cfg.maxOnMs ? static_cast<uint32_t>(cfg.maxOnMs) * 4u : 10000u;
    windowLengthMs_ = clampValue(windowLengthMs_, kMinWindowMs, kMaxWindowMs);
}

void SolenoidSafety::startWindowIfNeeded(uint32_t nowMs) {
    if (windowStarted_) return;
    windowStarted_ = true;
    windowStartMs_ = nowMs;
    loadSinceMs_ = nowMs;
}

uint32_t SolenoidSafety::loadSince(uint32_t nowMs) const {
    if (duty_ == 0) return 0;
    const uint32_t d = duty_;
    // Clamped to the window: nothing older than that counts anyway, and it
    // keeps the percent^2 * ms product inside 32 bits even if update() is
    // starved for minutes.
    uint32_t elapsed = nowMs - loadSinceMs_;
    if (elapsed > windowLengthMs_) elapsed = windowLengthMs_;
    return d * d * elapsed;
}

void SolenoidSafety::accumulate(uint32_t nowMs) {
    loadAccumulatorPct2Ms_ += loadSince(nowMs);
    loadSinceMs_ = nowMs;
}

void SolenoidSafety::setDuty(uint32_t nowMs, uint8_t percent) {
    if (percent > 100) percent = 100;
    accumulate(nowMs);
    // The continuous-ON clock starts on the off -> on edge only.  Changing the
    // PWM level (pull-in -> hold) is not a new press and must not restart it,
    // otherwise `maxOnMs` would never be reached on a held note.
    if (duty_ == 0 && percent > 0) continuousOnSinceMs_ = nowMs;
    duty_ = percent;
}

void SolenoidSafety::trip(SolenoidFault fault, uint32_t nowMs) {
    setDuty(nowMs, 0);
    fault_ = fault;
    cooldown_ = true;
    cooldownUntilMs_ = nowMs + (cfg_.cooldownMs ? cfg_.cooldownMs : 1000u);
}

void SolenoidSafety::request(bool pressed, uint32_t nowMs) {
    startWindowIfNeeded(nowMs);
    lastUpdateMs_ = nowMs;

    if (!pressed) {
        requested_ = false;
        setDuty(nowMs, 0);
        return;
    }

    if (requested_) return;   // already held, nothing to re-arm
    requested_ = true;

    // A pending cooldown or a latched fault must win over the MIDI request:
    // the note is remembered and applied when the coil has had time to cool.
    if (cooldown_) {
        setDuty(nowMs, 0);
        return;
    }
    setDuty(nowMs, cfg_.pullInPwm);
}

void SolenoidSafety::update(uint32_t nowMs) {
    startWindowIfNeeded(nowMs);
    lastUpdateMs_ = nowMs;

    // Slide the thermal observation window, keeping half of the previous one so
    // the measurement does not jump at every boundary.  Only the *window*
    // bookkeeping moves here: `continuousOnSinceMs_` is deliberately untouched,
    // because a coil that has been down for 25 s has been down for 25 s no
    // matter how often the measurement window rolled over.
    if (nowMs - windowStartMs_ >= windowLengthMs_) {
        accumulate(nowMs);
        loadAccumulatorPct2Ms_ /= 2;
        windowStartMs_ = nowMs - windowLengthMs_ / 2;
    }

    if (cooldown_) {
        if (static_cast<int32_t>(nowMs - cooldownUntilMs_) < 0) return;
        cooldown_ = false;
        // The note may still be held: allow one fresh pull-in.
        if (requested_) setDuty(nowMs, cfg_.pullInPwm);
        return;
    }

    if (!requested_ || duty_ == 0) return;

    const uint32_t heldMs = nowMs - continuousOnSinceMs_;

    // Hard mechanical/thermal limit on continuous drive.  This must fire even
    // if the MIDI note never ends, which is exactly the case a stuck note or a
    // dead controller produces.
    if (cfg_.maxOnMs > 0 && heldMs >= cfg_.maxOnMs) {
        trip(SolenoidFault::OVER_TIME, nowMs);
        return;
    }

    // Long term thermal ceiling, once the window holds enough history to mean
    // anything.  Compared against the equivalent continuous duty, so a note
    // held at the hold level is judged on the hold level.
    if (cfg_.maxDutyPercent > 0 && cfg_.maxDutyPercent < 100 &&
        (nowMs - windowStartMs_) >= kMinObservationMs &&
        measuredDutyPercent() > cfg_.maxDutyPercent) {
        trip(SolenoidFault::OVER_DUTY, nowMs);
        return;
    }

    // Pull-in finished -> drop to the hold level.  A zero hold level means
    // "pulse only": release cleanly instead of buzzing at 0%.
    if (heldMs >= cfg_.pullInMs) setDuty(nowMs, cfg_.holdPwm);
}

uint32_t SolenoidSafety::continuousOnMs(uint32_t nowMs) const {
    return duty_ ? nowMs - continuousOnSinceMs_ : 0;
}

uint8_t SolenoidSafety::measuredDutyPercent() const {
    if (!windowStarted_) return 0;
    const uint32_t elapsed = lastUpdateMs_ - windowStartMs_;
    if (elapsed == 0) return 0;
    const uint32_t load = loadAccumulatorPct2Ms_ + loadSince(lastUpdateMs_);
    // load is in percent^2 * ms; the mean is a percent^2, so the equivalent
    // continuous duty is its square root.
    uint32_t meanSquare = load / elapsed;
    if (meanSquare > 10000u) meanSquare = 10000u;
    return static_cast<uint8_t>(isqrt(meanSquare));
}

}  // namespace ot
