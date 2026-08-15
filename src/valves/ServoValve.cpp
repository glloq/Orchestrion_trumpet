#include "valves/ServoValve.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#endif

#include "diagnostics/Logger.h"

namespace ot {

namespace {
constexpr uint8_t kServoPwmBits = 16;
}

bool ServoValve::addValve(uint8_t valve, const ValveConfig& cfg, uint16_t frequencyHz) {
    if (valve >= kMaxValves) return false;
    if (cfg.gpio < 0) return false;
    Slot& s = slots_[valve];
    s.used = true;
    s.pin = cfg.gpio;
    s.motion.configure(cfg);
    frequencyHz_ = frequencyHz ? frequencyHz : 50;
    return true;
}

bool ServoValve::begin() {
    started_ = true;
    stopped_ = false;
    lastUpdateMs_ = OT_MILLIS();
    bool ok = true;
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        s.motion.forceTo(false);
#if !defined(OT_HOST_BUILD)
        if (!ledcAttach(static_cast<uint8_t>(s.pin), frequencyHz_, kServoPwmBits)) {
            OT_LOGE("servo", "LEDC attach failed on GPIO %d", s.pin);
            s.used = false;
            ok = false;
            continue;
        }
        s.attached = true;
#else
        s.attached = true;
#endif
        // Boot state is "released": the valves must be up before anything else
        // happens on the instrument.
        writePulse(s);
    }
    return ok;
}

void ServoValve::writePulse(Slot& slot) {
    if (!slot.attached) return;
    const uint32_t periodUs = 1000000UL / (frequencyHz_ ? frequencyHz_ : 50);
    const uint32_t maxDuty = (1UL << kServoPwmBits) - 1UL;
    uint32_t duty = (static_cast<uint32_t>(slot.motion.pulseUs()) * maxDuty) / periodUs;
    if (duty > maxDuty) duty = maxDuty;
#if !defined(OT_HOST_BUILD)
    ledcWrite(static_cast<uint8_t>(slot.pin), duty);
#else
    (void)duty;
#endif
}

void ServoValve::detach(Slot& slot) {
    if (!slot.attached) return;
#if !defined(OT_HOST_BUILD)
    ledcWrite(static_cast<uint8_t>(slot.pin), 0);
    ledcDetach(static_cast<uint8_t>(slot.pin));
    pinMode(static_cast<uint8_t>(slot.pin), INPUT);
#endif
    slot.attached = false;
}

void ServoValve::setValve(uint8_t valve, bool pressed) {
    if (valve >= kMaxValves || stopped_) return;
    Slot& s = slots_[valve];
    if (!s.used) return;
    if (!s.attached) {
#if !defined(OT_HOST_BUILD)
        if (!ledcAttach(static_cast<uint8_t>(s.pin), frequencyHz_, kServoPwmBits)) return;
#endif
        s.attached = true;
    }
    s.motion.setTarget(pressed);
    writePulse(s);
}

void ServoValve::releaseAll() {
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        if (slots_[i].used) setValve(i, false);
    }
}

void ServoValve::emergencyStop() {
    // Bring the valves up, then cut the drive entirely.
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        s.motion.forceTo(false);
        writePulse(s);
        detach(s);
    }
    stopped_ = true;
}

void ServoValve::enable() {
    stopped_ = false;
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        if (slots_[i].used) setValve(i, false);
    }
}

void ServoValve::update() {
    if (!started_) return;
    const uint32_t now = OT_MILLIS();
    const uint32_t dt = now - lastUpdateMs_;
    if (dt == 0) return;
    lastUpdateMs_ = now;

    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        const bool wasMoving = s.motion.isMoving();
        s.motion.update(dt);
        if (wasMoving || s.motion.isMoving()) writePulse(s);
        if (!stopped_ && s.motion.shouldDetach() && s.attached) detach(s);
    }
}

bool ServoValve::isPressed(uint8_t valve) const {
    if (valve >= kMaxValves) return false;
    return slots_[valve].used && slots_[valve].motion.targetPressed();
}

void ServoValve::previewAngle(uint8_t valve, uint16_t angle) {
    if (valve >= kMaxValves || stopped_) return;
    Slot& s = slots_[valve];
    if (!s.used) return;
    if (!s.attached) {
#if !defined(OT_HOST_BUILD)
        if (!ledcAttach(static_cast<uint8_t>(s.pin), frequencyHz_, kServoPwmBits)) return;
#endif
        s.attached = true;
    }
    // Calibration must be immediate so the mechanical adjustment is visible.
    ValveConfig tmp;
    tmp.pressedAngle = angle;
    tmp.releasedAngle = angle;
    tmp.minPulseUs = 500;
    tmp.maxPulseUs = 2400;
    ServoMotion probe;
    probe.configure(tmp);
    probe.forceTo(true);
    const uint32_t periodUs = 1000000UL / (frequencyHz_ ? frequencyHz_ : 50);
    const uint32_t maxDuty = (1UL << kServoPwmBits) - 1UL;
    uint32_t duty = (static_cast<uint32_t>(probe.pulseUs()) * maxDuty) / periodUs;
#if !defined(OT_HOST_BUILD)
    ledcWrite(static_cast<uint8_t>(s.pin), duty > maxDuty ? maxDuty : duty);
#else
    (void)duty;
#endif
}

float ServoValve::currentAngle(uint8_t valve) const {
    if (valve >= kMaxValves) return 0.0f;
    return slots_[valve].motion.angle();
}

}  // namespace ot
