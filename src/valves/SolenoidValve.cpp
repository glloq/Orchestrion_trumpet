#include "valves/SolenoidValve.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#endif

namespace ot {

namespace {
constexpr uint8_t kSolenoidPwmBits = 10;  // 1024 steps at 20 kHz: inaudible
}

bool SolenoidValve::addValve(uint8_t valve, const ValveConfig& cfg, uint16_t pwmFrequencyHz) {
    if (valve >= kMaxValves) return false;
    if (cfg.gpio < 0) return false;
    Slot& s = slots_[valve];
    s.used = true;
    s.pin = cfg.gpio;
    s.activeHigh = cfg.activeHigh;
    s.safety.configure(cfg);
    pwmFrequencyHz_ = pwmFrequencyHz ? pwmFrequencyHz : 20000;
    return true;
}

bool SolenoidValve::begin() {
    started_ = true;
    stopped_ = false;
    bool ok = true;
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
#if !defined(OT_HOST_BUILD)
        // Force the gate to the inactive level BEFORE enabling the PWM unit,
        // so a reset never leaves a coil energised.
        pinMode(static_cast<uint8_t>(s.pin), OUTPUT);
        digitalWrite(static_cast<uint8_t>(s.pin), s.activeHigh ? LOW : HIGH);
        if (!ledcAttach(static_cast<uint8_t>(s.pin), pwmFrequencyHz_, kSolenoidPwmBits)) {
            OT_LOGE("solenoid", "LEDC attach failed on GPIO %d", s.pin);
            s.used = false;
            ok = false;
            continue;
        }
#endif
        s.attached = true;
        applyDuty(s);
    }
    return ok;
}

void SolenoidValve::applyDuty(Slot& slot) {
    if (!slot.attached) return;
    uint8_t percent = stopped_ ? 0 : slot.safety.dutyPercent();
    if (percent > 100) percent = 100;
    const uint32_t maxDuty = (1UL << kSolenoidPwmBits) - 1UL;
    uint32_t duty = (static_cast<uint32_t>(percent) * maxDuty) / 100UL;
    if (!slot.activeHigh) duty = maxDuty - duty;
#if !defined(OT_HOST_BUILD)
    ledcWrite(static_cast<uint8_t>(slot.pin), duty);
#else
    (void)duty;
#endif
}

void SolenoidValve::setValve(uint8_t valve, bool pressed) {
    if (valve >= kMaxValves || stopped_) return;
    Slot& s = slots_[valve];
    if (!s.used) return;
    s.safety.request(pressed, OT_MILLIS());
    applyDuty(s);
}

void SolenoidValve::releaseAll() {
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        s.safety.request(false, OT_MILLIS());
        applyDuty(s);
    }
}

void SolenoidValve::update() {
    if (!started_) return;
    const uint32_t now = OT_MILLIS();
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        const uint8_t before = s.safety.dutyPercent();
        s.safety.update(now);
        if (before != s.safety.dutyPercent()) {
            if (s.safety.fault() != SolenoidFault::NONE && before != 0) {
                OT_LOGW("solenoid", "valve %u released by the thermal guard", i + 1);
            }
            applyDuty(s);
        }
    }
}

void SolenoidValve::emergencyStop() {
    stopped_ = true;
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        s.safety.request(false, OT_MILLIS());
#if !defined(OT_HOST_BUILD)
        // Bypass the PWM unit entirely: the gate is driven to the inactive
        // level by the GPIO matrix and stays there.
        ledcDetach(static_cast<uint8_t>(s.pin));
        pinMode(static_cast<uint8_t>(s.pin), OUTPUT);
        digitalWrite(static_cast<uint8_t>(s.pin), s.activeHigh ? LOW : HIGH);
#endif
        s.attached = false;
    }
}

void SolenoidValve::enable() {
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used || s.attached) continue;
#if !defined(OT_HOST_BUILD)
        if (!ledcAttach(static_cast<uint8_t>(s.pin), pwmFrequencyHz_, kSolenoidPwmBits)) continue;
#endif
        s.attached = true;
    }
    stopped_ = false;
    releaseAll();
}

bool SolenoidValve::isPressed(uint8_t valve) const {
    if (valve >= kMaxValves) return false;
    return slots_[valve].used && slots_[valve].safety.isEnergised();
}

bool SolenoidValve::hasFault(uint8_t valve) const {
    return valve < kMaxValves && slots_[valve].used &&
           slots_[valve].safety.fault() != SolenoidFault::NONE;
}

const char* SolenoidValve::faultText(uint8_t valve) const {
    if (!hasFault(valve)) return nullptr;
    switch (slots_[valve].safety.fault()) {
        case SolenoidFault::OVER_TIME:
            return "Maximum continuous ON time exceeded - coil released";
        case SolenoidFault::OVER_DUTY:
            return "Duty cycle limit exceeded - cooling down";
        default:
            return nullptr;
    }
}

void SolenoidValve::clearFaults() {
    for (auto& s : slots_) s.safety.clearFault();
}

uint8_t SolenoidValve::dutyPercent(uint8_t valve) const {
    if (valve >= kMaxValves) return 0;
    return slots_[valve].safety.dutyPercent();
}

}  // namespace ot
