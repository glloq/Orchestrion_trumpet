#include "valves/ValveController.h"

#include "diagnostics/Logger.h"

namespace ot {

bool ValveController::begin(const ValvesConfig& valves, const InstrumentConfig& instrument) {
    cfg_ = valves;
    if (cfg_.count > kMaxValves) cfg_.count = kMaxValves;
    fingering_.configure(instrument);
    notes_.setPriority(instrument.notePriority);
    notes_.clear();
    currentMask_ = 0;
    manualMask_ = 0;
    ccMask_ = 0;
    pulseMask_ = 0;
    stopped_ = false;

    bool needServo = false, needPca = false, needSolenoid = false;
    pca_.configureBus(cfg_.pca9685I2c, cfg_.pca9685Address, cfg_.pca9685OePin,
                      cfg_.servoFrequencyHz);

    for (uint8_t i = 0; i < cfg_.count; ++i) {
        const ValveConfig& v = cfg_.items[i];
        drivers_[i] = nullptr;
        switch (v.type) {
            case ValveActuatorType::SERVO:
                if (v.driver == ServoDriverType::PCA9685) {
                    if (pca_.addValve(i, v)) {
                        drivers_[i] = &pca_;
                        needPca = true;
                    }
                } else {
                    if (servo_.addValve(i, v, cfg_.servoFrequencyHz)) {
                        drivers_[i] = &servo_;
                        needServo = true;
                    }
                }
                break;
            case ValveActuatorType::SOLENOID:
                if (solenoid_.addValve(i, v, cfg_.solenoidPwmFrequencyHz)) {
                    drivers_[i] = &solenoid_;
                    needSolenoid = true;
                }
                break;
            case ValveActuatorType::OFF:
            default:
                break;
        }
        if (v.type != ValveActuatorType::OFF && drivers_[i] == nullptr) {
            OT_LOGE("valves", "valve %u could not be bound to a driver", i + 1);
        }
    }

    bool ok = true;
    if (needServo) ok = servo_.begin() && ok;
    if (needPca) ok = pca_.begin() && ok;
    if (needSolenoid) ok = solenoid_.begin() && ok;

    started_ = true;
    releaseAll();
    return ok;
}

void ValveController::end() {
    releaseAll();
    started_ = false;
}

IValveActuator* ValveController::driverFor(uint8_t valve) const {
    return valve < kMaxValves ? drivers_[valve] : nullptr;
}

void ValveController::setMode(ValveMode mode) {
    if (cfg_.mode == mode) return;
    cfg_.mode = mode;
    manualMask_ = 0;
    ccMask_ = 0;
    if (mode == ValveMode::OFF) {
        releaseAll();
    } else {
        applyMask(0);
    }
}

uint8_t ValveController::desiredMask() const {
    switch (cfg_.mode) {
        case ValveMode::AUTO:
            return notes_.hasNote() ? maskForNote(notes_.activeNote()) : 0;
        case ValveMode::MANUAL:
            return manualMask_;
        case ValveMode::MIDI_CC:
            return ccMask_;
        case ValveMode::OFF:
        default:
            return 0;
    }
}

uint8_t ValveController::maskForNote(uint8_t midiNote) const {
    const uint8_t mask = fingering_.fingeringForMidiNote(midiNote);
    if (mask == kNoFingering) return 0;
    // Only keep the valves this instrument actually has.
    const uint8_t available = static_cast<uint8_t>((1u << cfg_.count) - 1u);
    return static_cast<uint8_t>(mask & available);
}

void ValveController::applyMask(uint8_t mask) {
    if (!started_ || stopped_) return;
    const uint8_t available = static_cast<uint8_t>((1u << cfg_.count) - 1u);
    mask &= available;
    mask |= pulseMask_;
    if (mask == currentMask_) return;

    for (uint8_t i = 0; i < cfg_.count; ++i) {
        const bool want = (mask >> i) & 1u;
        const bool have = (currentMask_ >> i) & 1u;
        if (want == have) continue;
        IValveActuator* d = driverFor(i);
        if (d) d->setValve(i, want);
    }
    currentMask_ = mask;
}

void ValveController::onMidi(const MidiMessage& msg) {
    if (!started_ || cfg_.mode == ValveMode::OFF) return;

    if (msg.type == MidiType::ControlChange) {
        switch (msg.data1) {
            case cc::AllNotesOff:
            case cc::AllSoundOff:
                notes_.clear();
                ccMask_ = 0;
                applyMask(0);
                return;
            case cc::ResetControllers:
                ccMask_ = 0;
                if (cfg_.mode == ValveMode::MIDI_CC) applyMask(0);
                return;
            default:
                break;
        }
        if (cfg_.mode == ValveMode::MIDI_CC) {
            for (uint8_t i = 0; i < cfg_.count; ++i) {
                if (cfg_.items[i].ccNumber != msg.data1) continue;
                if (msg.data2 >= 64) {
                    ccMask_ |= static_cast<uint8_t>(1u << i);
                } else {
                    ccMask_ &= static_cast<uint8_t>(~(1u << i));
                }
            }
            applyMask(ccMask_);
        }
        return;
    }

    if (cfg_.mode != ValveMode::AUTO) return;

    if (msg.isNoteOn()) {
        notes_.noteOn(msg.data1, msg.data2);
    } else if (msg.isNoteOff()) {
        notes_.noteOff(msg.data1);
    } else {
        return;
    }

    if (notes_.hasNote()) {
        applyMask(maskForNote(notes_.activeNote()));
    } else {
        applyMask(0);
    }
}

void ValveController::update() {
    if (!started_) return;

    if (pulseMask_) {
        const uint32_t now = OT_MILLIS();
        for (uint8_t i = 0; i < cfg_.count; ++i) {
            if (((pulseMask_ >> i) & 1u) == 0) continue;
            if (static_cast<int32_t>(now - pulseUntilMs_[i]) < 0) continue;
            pulseMask_ &= static_cast<uint8_t>(~(1u << i));

            // A test pulse must leave the valve exactly where the current mode
            // wants it, not simply released: pulsing a valve while a note is
            // held would otherwise silently lift it for the rest of the note.
            const bool stillWanted = ((desiredMask() >> i) & 1u) != 0;
            IValveActuator* d = driverFor(i);
            if (d && !stillWanted) {
                d->setValve(i, false);
                currentMask_ &= static_cast<uint8_t>(~(1u << i));
            }
        }
    }

    servo_.update();
    pca_.update();
    solenoid_.update();
}

void ValveController::releaseAll() {
    notes_.clear();
    manualMask_ = 0;
    ccMask_ = 0;
    pulseMask_ = 0;
    currentMask_ = 0;
    servo_.releaseAll();
    pca_.releaseAll();
    solenoid_.releaseAll();
}

void ValveController::panic() {
    releaseAll();
    servo_.emergencyStop();
    pca_.emergencyStop();
    solenoid_.emergencyStop();
    stopped_ = true;
}

void ValveController::enableAfterPanic() {
    solenoid_.clearFaults();
    servo_.enable();
    pca_.enable();
    solenoid_.enable();
    stopped_ = false;
    currentMask_ = 0;
}

bool ValveController::manualSet(uint8_t valve, bool pressed) {
    if (valve >= cfg_.count) return false;
    if (cfg_.mode != ValveMode::MANUAL) return false;
    if (pressed) {
        manualMask_ |= static_cast<uint8_t>(1u << valve);
    } else {
        manualMask_ &= static_cast<uint8_t>(~(1u << valve));
    }
    applyMask(manualMask_);
    return true;
}

bool ValveController::testPulse(uint8_t valve, uint16_t durationMs) {
    if (valve >= cfg_.count || stopped_) return false;
    IValveActuator* d = driverFor(valve);
    if (!d) return false;
    if (durationMs > 2000) durationMs = 2000;   // never let a test cook a coil
    pulseUntilMs_[valve] = OT_MILLIS() + durationMs;
    pulseMask_ |= static_cast<uint8_t>(1u << valve);
    d->setValve(valve, true);
    currentMask_ |= static_cast<uint8_t>(1u << valve);
    return true;
}

bool ValveController::calibrationPreview(uint8_t valve, uint16_t angle) {
    if (valve >= cfg_.count || stopped_) return false;
    if (cfg_.items[valve].type != ValveActuatorType::SERVO) return false;
    if (cfg_.items[valve].driver == ServoDriverType::PCA9685) {
        pca_.previewAngle(valve, angle);
    } else {
        servo_.previewAngle(valve, angle);
    }
    return true;
}

ValveStatus ValveController::status(uint8_t valve) const {
    ValveStatus s;
    if (valve >= cfg_.count) return s;
    s.type = cfg_.items[valve].type;
    IValveActuator* d = driverFor(valve);
    if (!d) return s;
    s.pressed = d->isPressed(valve);
    s.fault = d->hasFault(valve);
    s.faultText = d->faultText(valve);
    if (s.type == ValveActuatorType::SERVO) {
        s.angle = cfg_.items[valve].driver == ServoDriverType::PCA9685
                      ? pca_.currentAngle(valve)
                      : servo_.currentAngle(valve);
    } else if (s.type == ValveActuatorType::SOLENOID) {
        s.dutyPercent = solenoid_.dutyPercent(valve);
    }
    return s;
}

bool ValveController::anyFault() const {
    for (uint8_t i = 0; i < cfg_.count; ++i) {
        IValveActuator* d = driverFor(i);
        if (d && d->hasFault(i)) return true;
    }
    return false;
}

}  // namespace ot
