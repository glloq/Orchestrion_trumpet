#include "valves/Pca9685Valve.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <Wire.h>
#endif

namespace ot {

namespace {
constexpr uint8_t kRegMode1 = 0x00;
constexpr uint8_t kRegMode2 = 0x01;
constexpr uint8_t kRegLed0OnL = 0x06;
constexpr uint8_t kRegPrescale = 0xFE;

constexpr uint8_t kMode1Restart = 0x80;
constexpr uint8_t kMode1Sleep = 0x10;
constexpr uint8_t kMode1AutoInc = 0x20;
constexpr uint8_t kMode2OutDrive = 0x04;  // totem pole outputs
}  // namespace

void Pca9685Valve::configureBus(const I2cPins& pins, uint8_t address, int8_t oePin,
                                uint16_t frequencyHz) {
    bus_ = pins;
    address_ = address;
    oePin_ = oePin;
    frequencyHz_ = frequencyHz ? frequencyHz : 50;
}

bool Pca9685Valve::addValve(uint8_t valve, const ValveConfig& cfg) {
    if (valve >= kMaxValves) return false;
    if (cfg.channel < 0 || cfg.channel > 15) return false;
    Slot& s = slots_[valve];
    s.used = true;
    s.channel = static_cast<uint8_t>(cfg.channel);
    s.motion.configure(cfg);
    return true;
}

bool Pca9685Valve::writeRegister(uint8_t reg, uint8_t value) {
#if defined(OT_HOST_BUILD)
    (void)reg;
    (void)value;
    return true;
#else
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
#endif
}

bool Pca9685Valve::setPwm(uint8_t channel, uint16_t on, uint16_t off) {
#if defined(OT_HOST_BUILD)
    (void)channel;
    (void)on;
    (void)off;
    return true;
#else
    Wire.beginTransmission(address_);
    Wire.write(static_cast<uint8_t>(kRegLed0OnL + 4 * channel));
    Wire.write(static_cast<uint8_t>(on & 0xFF));
    Wire.write(static_cast<uint8_t>(on >> 8));
    Wire.write(static_cast<uint8_t>(off & 0xFF));
    Wire.write(static_cast<uint8_t>(off >> 8));
    return Wire.endTransmission() == 0;
#endif
}

uint16_t Pca9685Valve::pulseToTicks(uint16_t pulseUs) const {
    // 12 bit counter over one PWM period.
    const uint32_t periodUs = 1000000UL / (frequencyHz_ ? frequencyHz_ : 50);
    uint32_t ticks = (static_cast<uint32_t>(pulseUs) * 4096UL) / periodUs;
    if (ticks > 4095UL) ticks = 4095UL;
    return static_cast<uint16_t>(ticks);
}

bool Pca9685Valve::begin() {
    started_ = true;
    stopped_ = false;
    lastUpdateMs_ = OT_MILLIS();

#if !defined(OT_HOST_BUILD)
    if (oePin_ >= 0) {
        // OE is active low: drive it high (outputs disabled) before the bus is
        // even configured so nothing twitches at power-up.
        pinMode(static_cast<uint8_t>(oePin_), OUTPUT);
        digitalWrite(static_cast<uint8_t>(oePin_), HIGH);
    }
    Wire.begin(bus_.sda, bus_.scl, bus_.frequency);

    Wire.beginTransmission(address_);
    if (Wire.endTransmission() != 0) {
        OT_LOGE("pca9685", "no device at 0x%02X (SDA %d SCL %d)", address_, bus_.sda, bus_.scl);
        present_ = false;
        return false;
    }
#endif
    present_ = true;

    // Prescale can only be written while the oscillator sleeps.
    const uint32_t prescale =
        (25000000UL / (4096UL * (frequencyHz_ ? frequencyHz_ : 50))) - 1UL;
    bool ok = writeRegister(kRegMode1, kMode1Sleep);
    ok = writeRegister(kRegPrescale, static_cast<uint8_t>(prescale)) && ok;
    ok = writeRegister(kRegMode1, kMode1AutoInc) && ok;
#if !defined(OT_HOST_BUILD)
    delayMicroseconds(600);  // oscillator restart, 500 us per datasheet
#endif
    ok = writeRegister(kRegMode1, kMode1AutoInc | kMode1Restart) && ok;
    ok = writeRegister(kRegMode2, kMode2OutDrive) && ok;

    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        s.motion.forceTo(false);
        writePulse(s);
    }

#if !defined(OT_HOST_BUILD)
    if (oePin_ >= 0) digitalWrite(static_cast<uint8_t>(oePin_), LOW);  // enable
#endif
    if (!ok) OT_LOGW("pca9685", "initialisation reported an I2C error");
    return ok;
}

void Pca9685Valve::writePulse(Slot& slot) {
    if (!present_) return;
    setPwm(slot.channel, 0, pulseToTicks(slot.motion.pulseUs()));
}

void Pca9685Valve::setValve(uint8_t valve, bool pressed) {
    if (valve >= kMaxValves || stopped_) return;
    Slot& s = slots_[valve];
    if (!s.used) return;
    s.motion.setTarget(pressed);
    writePulse(s);
}

void Pca9685Valve::releaseAll() {
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        if (slots_[i].used) setValve(i, false);
    }
}

void Pca9685Valve::update() {
    if (!started_ || !present_) return;
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
        if (!stopped_ && s.motion.shouldDetach()) {
            // Zero length pulse: the channel stays configured but the servo
            // receives no command, so it stops holding torque.
            setPwm(s.channel, 0, 0);
        }
    }
}

void Pca9685Valve::emergencyStop() {
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        Slot& s = slots_[i];
        if (!s.used) continue;
        s.motion.forceTo(false);
        writePulse(s);
        setPwm(s.channel, 0, 0);
    }
#if !defined(OT_HOST_BUILD)
    if (oePin_ >= 0) digitalWrite(static_cast<uint8_t>(oePin_), HIGH);  // outputs off
#endif
    stopped_ = true;
}

void Pca9685Valve::enable() {
#if !defined(OT_HOST_BUILD)
    if (oePin_ >= 0) digitalWrite(static_cast<uint8_t>(oePin_), LOW);
#endif
    stopped_ = false;
    releaseAll();
}

bool Pca9685Valve::isPressed(uint8_t valve) const {
    if (valve >= kMaxValves) return false;
    return slots_[valve].used && slots_[valve].motion.targetPressed();
}

bool Pca9685Valve::hasFault(uint8_t valve) const {
    return valve < kMaxValves && slots_[valve].used && !present_;
}

const char* Pca9685Valve::faultText(uint8_t valve) const {
    return hasFault(valve) ? "PCA9685 not responding on I2C" : nullptr;
}

void Pca9685Valve::previewAngle(uint8_t valve, uint16_t angle) {
    if (valve >= kMaxValves || stopped_ || !present_) return;
    Slot& s = slots_[valve];
    if (!s.used) return;
    ValveConfig tmp;
    tmp.pressedAngle = angle;
    tmp.releasedAngle = angle;
    ServoMotion probe;
    probe.configure(tmp);
    probe.forceTo(true);
    setPwm(s.channel, 0, pulseToTicks(probe.pulseUs()));
}

float Pca9685Valve::currentAngle(uint8_t valve) const {
    if (valve >= kMaxValves) return 0.0f;
    return slots_[valve].motion.angle();
}

}  // namespace ot
