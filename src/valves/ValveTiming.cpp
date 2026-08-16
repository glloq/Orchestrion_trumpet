#include "valves/ValveTiming.h"

namespace ot {

namespace {
// Linkage slop, stiction and the last few degrees a servo creeps through are
// not in any datasheet.  This margin is what the bench trim exists to correct;
// it is deliberately small so the default errs towards being late rather than
// towards a wildly padded delay.
constexpr uint16_t kMechanicalMarginMs = 12;
// A single valve is never allowed to claim more than this on its own: past it
// the configuration is wrong, not slow.
constexpr uint16_t kPerValveCeilingMs = 400;
}  // namespace

// The same profile ServoMotion actually runs: ramp up at `accel`, cruise at
// `speed` if there is room for it, ramp down so the servo stops exactly on
// target.  Dividing the travel by the top speed - which is what this used to do
// - assumes the servo is already at full speed when it starts, and for a short
// throw it never gets there at all: 48 deg at 900 deg/s and 6000 deg/s^2 takes
// 179 ms, not the 65 ms the simple quotient predicts.  Under-estimating here
// means the note speaks before the piston has landed, which is precisely what
// the synchronisation is for.
//
//   short throw (dist <= v^2/a)   triangular   t = 2 * sqrt(dist / a)
//   long throw                    trapezoidal  t = dist / v + v / a
//
// Integer millisecond arithmetic throughout: this runs at every Note On, on the
// audio task.
uint32_t servoTravelMs(uint32_t travelDegrees, uint16_t speedDegPerSec,
                       uint16_t accelDegPerSec2) {
    if (travelDegrees == 0) return 0;
    if (speedDegPerSec == 0) return kPerValveCeilingMs;
    const uint32_t v = speedDegPerSec;
    // No acceleration limit configured: the old model is then the right one.
    if (accelDegPerSec2 == 0) return (travelDegrees * 1000u) / v;
    const uint32_t a = accelDegPerSec2;

    // Distance needed to reach the top speed and come back down again.
    const uint32_t rampDistance = (v * v) / a;
    if (travelDegrees <= rampDistance) {
        // sqrt(dist/a) seconds -> 1000 * sqrt(dist/a) ms = sqrt(1e6 * dist / a).
        return 2u * isqrt32((1000000u / a) * travelDegrees +
                            ((1000000u % a) * travelDegrees) / a);
    }
    return (travelDegrees * 1000u) / v + (v * 1000u) / a;
}

uint16_t valveSettleMs(const ValveConfig& valve) {
    if (valve.measuredSettleMs > 0) return valve.measuredSettleMs;

    switch (valve.type) {
        case ValveActuatorType::SERVO: {
            if (valve.speedDegPerSec == 0) return kPerValveCeilingMs;
            const uint32_t travel = valve.pressedAngle > valve.releasedAngle
                                        ? static_cast<uint32_t>(valve.pressedAngle -
                                                                valve.releasedAngle)
                                        : static_cast<uint32_t>(valve.releasedAngle -
                                                                valve.pressedAngle);
            const uint32_t total = servoTravelMs(travel, valve.speedDegPerSec,
                                                 valve.accelDegPerSec2) +
                                   kMechanicalMarginMs;
            return static_cast<uint16_t>(total > kPerValveCeilingMs ? kPerValveCeilingMs : total);
        }
        case ValveActuatorType::SOLENOID: {
            // The plunger is home by the end of the pull-in burst: that burst
            // is sized for exactly that.
            const uint32_t total = static_cast<uint32_t>(valve.pullInMs) + kMechanicalMarginMs;
            return static_cast<uint16_t>(total > kPerValveCeilingMs ? kPerValveCeilingMs : total);
        }
        case ValveActuatorType::OFF:
        default:
            return 0;
    }
}

uint16_t fingeringSettleMs(const ValvesConfig& valves, uint8_t fromMask, uint8_t toMask) {
    const uint8_t moving = static_cast<uint8_t>(fromMask ^ toMask);
    if (moving == 0) return 0;

    uint16_t worst = 0;
    const uint8_t count = valves.count > kMaxValves ? kMaxValves : valves.count;
    for (uint8_t i = 0; i < count; ++i) {
        if (((moving >> i) & 1u) == 0) continue;
        const uint16_t ms = valveSettleMs(valves.items[i]);
        if (ms > worst) worst = ms;
    }
    // The valves move together, so the note waits for the slowest one, not for
    // the sum.
    return worst;
}

uint16_t noteAttackDelayMs(const ValvesConfig& valves, uint8_t fromMask, uint8_t toMask) {
    const ValveSyncConfig& sync = valves.sync;
    if (!sync.enabled) return 0;
    if (valves.mode != ValveMode::AUTO) return 0;   // nothing is following the notes

    const bool changing = fromMask != toMask;
    if (sync.onlyWhenFingeringChanges && !changing) return 0;

    // When the policy says "always wait", an unchanged fingering still waits
    // for the slowest valve of the chord so every note has the same feel.
    uint16_t base = changing ? fingeringSettleMs(valves, fromMask, toMask)
                             : fingeringSettleMs(valves, 0, toMask);
    if (base == 0) return 0;

    int32_t total = static_cast<int32_t>(base) + sync.trimMs;
    if (total < 0) total = 0;
    if (total > sync.maxDelayMs) total = sync.maxDelayMs;
    return static_cast<uint16_t>(total);
}

}  // namespace ot
