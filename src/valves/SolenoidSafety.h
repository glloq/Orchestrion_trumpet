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
//    * a hard maximum continuous ON time -> release + a LATCHED fault: the
//      coil is not re-energised when the cooldown expires, because the only
//      thing that produces a note longer than the maximum is a stuck note, a
//      crashed sequencer or an unplugged cable, and cycling 5 s on / 3 s off
//      for ever is not a safe answer to any of them.  It takes a Note Off or an
//      explicit Clear fault;
//    * a long term *thermal* ceiling -> forced cooldown;
//    * the guard keeps running even if MIDI stops arriving.
//
//  Three quantities are tracked separately, because conflating them is exactly
//  how this guard gets it wrong:
//
//    1. continuous mechanical ON time  - how long the plunger has been down,
//       whatever PWM level holds it there.  Compared against `maxOnMs`.
//    2. applied PWM duty               - the electrical drive, 100% during
//       pull-in and `holdPwm` afterwards.
//    3. thermal load                   - the heating the coil actually sees.
//       A coil is an inductor: the PWM current is smoothed, so I is roughly
//       proportional to the duty and the dissipation to I^2*R.  The load is
//       therefore integrated as duty^2 * dt and reported back as the
//       equivalent *continuous* duty that would heat the coil the same way.
//       Holding at 35% is 35% of thermal duty, not 100% because the plunger
//       happens to be down - the hold level exists precisely so a long note
//       does not have to trip the ceiling.
//
//  Note on time handling: every state is carried by an explicit boolean, never
//  by a "0 means unset" timestamp.  Zero is a perfectly legal value of the
//  millisecond clock - right after boot, and again every time the 32 bit
//  counter wraps - and using it as a sentinel would silently disable the duty
//  accounting at exactly those moments.  All the differences are computed on
//  unsigned arithmetic so they stay correct across a wrap.
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
    bool inCooldown() const { return cooldown_; }
    // Clears the fault AND the latch, so an operator pressing "clear" on the
    // diagnostics page really does re-arm the valve.
    void clearFault() {
        fault_ = SolenoidFault::NONE;
        latched_ = false;
    }
    // True while a maximum-on-time fault is holding the coil off: the note is
    // still being requested and the guard is deliberately refusing it.
    bool latched() const { return latched_; }

    // Equivalent continuous duty over the observation window, 0..100: the
    // steady drive level that would heat the coil as much as what it has
    // actually seen.  This is what `maxDutyPercent` is compared against.
    uint8_t measuredDutyPercent() const;

    // How long the plunger has been continuously down, in ms.  Independent of
    // the PWM level and never reset by the observation window sliding.
    uint32_t continuousOnMs(uint32_t nowMs) const;

private:
    void startWindowIfNeeded(uint32_t nowMs);
    // Integrates the load produced since the last change and moves to `percent`.
    void setDuty(uint32_t nowMs, uint8_t percent);
    void accumulate(uint32_t nowMs);
    void trip(SolenoidFault fault, uint32_t nowMs);
    uint32_t loadSince(uint32_t nowMs) const;

    ValveConfig cfg_;
    // 1. mechanical: set when the coil goes from off to on, never touched again
    //    until it goes off.
    uint32_t continuousOnSinceMs_ = 0;
    // 2./3. thermal: `loadSinceMs_` marks the start of the current duty step,
    //    `loadAccumulatorPct2Ms_` the integral of duty^2*dt over the window.
    uint32_t loadSinceMs_ = 0;
    uint32_t loadAccumulatorPct2Ms_ = 0;
    uint32_t lastUpdateMs_ = 0;
    uint32_t cooldownUntilMs_ = 0;
    uint32_t windowStartMs_ = 0;
    uint32_t windowLengthMs_ = 10000;
    uint8_t duty_ = 0;
    SolenoidFault fault_ = SolenoidFault::NONE;
    bool requested_ = false;
    bool cooldown_ = false;
    bool latched_ = false;
    bool windowStarted_ = false;
};

}  // namespace ot
