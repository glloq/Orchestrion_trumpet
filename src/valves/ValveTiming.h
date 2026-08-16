// ============================================================================
//  ValveTiming.h - how long a piston needs before the note is worth playing.
//
//  On a real trumpet the pistons and the lips move together.  Here the sound
//  engine and the actuators receive the same Note-On at the same instant, but
//  the actuators are mechanical: a servo swinging 48 degrees at 900 deg/s and
//  6000 deg/s^2 needs 179 ms - it never even reaches its top speed over a throw
//  that short - and a solenoid does not have its plunger home until its pull-in
//  burst is over.  Starting the attack immediately means the first
//  tens of milliseconds of every note are played through the *previous*
//  fingering - through a bore that is physically the wrong length.
//
//  This file is the arithmetic only: no hardware, no timers, so it is unit
//  tested on the host.  AudioEngine uses it to delay the attack, and only for
//  the valves that actually have to move.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

// Time for one valve to complete a full released <-> pressed transition.
// A measured figure on the valve always wins over the estimate.
uint16_t valveSettleMs(const ValveConfig& valve);

// The trapezoidal (or, for a short throw, triangular) profile ServoMotion
// actually runs.  Exposed so it can be tested on its own.
uint32_t servoTravelMs(uint32_t travelDegrees, uint16_t speedDegPerSec,
                       uint16_t accelDegPerSec2);

// Time before the fingering `toMask` is actually in place, coming from
// `fromMask`.  Only the valves whose state differs are considered, so a
// repeated note on the same combination costs nothing.
uint16_t fingeringSettleMs(const ValvesConfig& valves, uint8_t fromMask, uint8_t toMask);

// The delay the sound engine should apply, after the sync policy (enabled,
// only-when-changing, trim, ceiling) has been taken into account.
uint16_t noteAttackDelayMs(const ValvesConfig& valves, uint8_t fromMask, uint8_t toMask);

}  // namespace ot
