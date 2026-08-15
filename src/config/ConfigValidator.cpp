#include "config/ConfigValidator.h"

#include <cstdarg>
#include <cstdio>

#include "audio/Limiter.h"

namespace ot {

namespace {

// One entry per GPIO claimed by the configuration, so a duplicate can be
// reported with both owners named.
struct PinClaim {
    int8_t pin = -1;
    char owner[24] = "";
};

struct PinTable {
    PinClaim claims[24];
    uint8_t count = 0;

    const char* claim(int8_t pin, const char* owner) {
        if (pin < 0) return nullptr;
        for (uint8_t i = 0; i < count; ++i) {
            if (claims[i].pin == pin) return claims[i].owner;
        }
        if (count < 24) {
            claims[count].pin = pin;
            copyString(claims[count].owner, sizeof(claims[count].owner), owner);
            ++count;
        }
        return nullptr;
    }
};

void formatIssue(ValidationReport& out, Severity sev, const char* field, const char* fmt, ...) {
    char buffer[kIssueTextLen];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    out.add(sev, field, buffer);
}

void checkPin(int8_t pin, const char* owner, const char* field, bool needsOutput,
              const BoardCapabilities& caps, PinTable& table, ValidationReport& out) {
    if (pin < 0) return;

    if (!caps.isValidGpio(pin)) {
        formatIssue(out, Severity::ERROR, field, "GPIO %d does not exist on this board", pin);
        return;
    }
    if (caps.isReserved(static_cast<uint8_t>(pin))) {
        formatIssue(out, Severity::ERROR, field,
                    "GPIO %d is wired to the flash/PSRAM and cannot be used", pin);
        return;
    }
    if (needsOutput && caps.isInputOnly(static_cast<uint8_t>(pin))) {
        formatIssue(out, Severity::ERROR, field, "GPIO %d is input only and cannot drive %s", pin,
                    owner);
        return;
    }
    if (caps.isWarned(static_cast<uint8_t>(pin))) {
        formatIssue(out, Severity::WARNING, field,
                    "GPIO %d is a strapping/USB pin: check the boot level", pin);
    }

    const char* previous = table.claim(pin, owner);
    if (previous) {
        formatIssue(out, Severity::ERROR, field, "GPIO %d is already used by %s", pin, previous);
    }
}

}  // namespace

void ValidationReport::add(Severity severity, const char* field, const char* message) {
    if (count_ >= kMaxValidationIssues) {
        truncated_ = true;
        return;
    }
    ValidationIssue& i = issues_[count_++];
    i.severity = severity;
    copyString(i.field, sizeof(i.field), field);
    copyString(i.message, sizeof(i.message), message);
}

bool ValidationReport::hasErrors() const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (issues_[i].severity == Severity::ERROR) return true;
    }
    return false;
}

bool ValidationReport::hasWarnings() const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (issues_[i].severity == Severity::WARNING) return true;
    }
    return false;
}

const char* ConfigValidator::toString(Severity s) {
    switch (s) {
        case Severity::ERROR:
            return "ERROR";
        case Severity::WARNING:
            return "WARNING";
        case Severity::INFO:
        default:
            return "INFO";
    }
}

void ConfigValidator::validate(const InstrumentConfiguration& cfg, const BoardCapabilities& caps,
                               ValidationReport& out) {
    out.clear();
    PinTable pins;

    // ---------------------------------------------------------------- audio
    const bool usesI2s = cfg.audio.backend != AudioBackendType::NONE &&
                         cfg.audio.backend != AudioBackendType::ESP32_INTERNAL_DAC;

    if (!caps.supportsBackend(cfg.audio.backend)) {
        formatIssue(out, Severity::ERROR, "audio.backend",
                    "%s is not available on this board", ot::toString(cfg.audio.backend));
    }

    if (usesI2s) {
        checkPin(cfg.audio.i2s.bclk, "I2S BCLK", "audio.i2s.bclk", true, caps, pins, out);
        checkPin(cfg.audio.i2s.ws, "I2S WS", "audio.i2s.ws", true, caps, pins, out);
        checkPin(cfg.audio.i2s.dout, "I2S DOUT", "audio.i2s.dout", true, caps, pins, out);
        checkPin(cfg.audio.i2s.din, "I2S DIN", "audio.i2s.din", false, caps, pins, out);
        checkPin(cfg.audio.i2s.mclk, "I2S MCLK", "audio.i2s.mclk", true, caps, pins, out);
        checkPin(cfg.audio.sdModePin, "the DAC mute pin", "audio.sdModePin", true, caps, pins,
                 out);
    }

    const bool usesCodecI2c = cfg.audio.backend == AudioBackendType::ES8388 ||
                              cfg.audio.backend == AudioBackendType::WM8960 ||
                              cfg.audio.backend == AudioBackendType::TAS5760M;
    if (usesCodecI2c) {
        checkPin(cfg.audio.i2c.sda, "codec I2C SDA", "audio.i2c.sda", true, caps, pins, out);
        checkPin(cfg.audio.i2c.scl, "codec I2C SCL", "audio.i2c.scl", true, caps, pins, out);
    }

    if (cfg.audio.sampleRate < 8000 || cfg.audio.sampleRate > 96000) {
        formatIssue(out, Severity::ERROR, "audio.sampleRate",
                    "sample rate %u Hz is outside the supported 8000..96000 range",
                    static_cast<unsigned>(cfg.audio.sampleRate));
    }
    if (cfg.audio.blockSize < 32 || cfg.audio.blockSize > 512) {
        formatIssue(out, Severity::WARNING, "audio.blockSize",
                    "block size %u is unusual; 64..256 keeps the latency low and the CPU calm",
                    static_cast<unsigned>(cfg.audio.blockSize));
    }
    if (cfg.audio.backend == AudioBackendType::MAX98357A && cfg.audio.bitDepth > 16) {
        formatIssue(out, Severity::INFO, "audio.bitDepth",
                    "the MAX98357A runs at 16 bit; the request will be reduced");
    }
    if (cfg.audio.backend == AudioBackendType::ESP32_INTERNAL_DAC) {
        formatIssue(out, Severity::WARNING, "audio.backend",
                    "the internal DAC is 8 bit: prototype quality only");
    }

    // ------------------------------------------------------------ amplifier
    if (cfg.amplifier.type == AmplifierType::MAX98357_INTERNAL &&
        cfg.audio.backend != AudioBackendType::MAX98357A) {
        formatIssue(out, Severity::ERROR, "amplifier.type",
                    "the MAX98357A amplifier only exists together with the MAX98357A backend");
    }
    if (cfg.amplifier.type == AmplifierType::TAS5760_INTERNAL &&
        cfg.audio.backend != AudioBackendType::TAS5760M) {
        formatIssue(out, Severity::ERROR, "amplifier.type",
                    "the TAS5760 amplifier only exists together with the TAS5760M backend");
    }
    if (cfg.amplifier.type != AmplifierType::NONE && cfg.speaker.impedanceOhm > 0.5f &&
        cfg.amplifier.speakerImpedanceOhm > 0.5f) {
        const float ratio = cfg.speaker.impedanceOhm / cfg.amplifier.speakerImpedanceOhm;
        if (ratio < 0.9f) {
            formatIssue(out, Severity::ERROR, "speaker.impedance",
                        "a %.0f ohm speaker is below the %.0f ohm the amplifier is rated for",
                        static_cast<double>(cfg.speaker.impedanceOhm),
                        static_cast<double>(cfg.amplifier.speakerImpedanceOhm));
        }
    }

    // -------------------------------------------------------------- speaker
    if (cfg.speaker.powerLimitW > cfg.speaker.powerRmsW && cfg.speaker.powerRmsW > 0.05f) {
        formatIssue(out, Severity::ERROR, "speaker.powerLimit",
                    "the protection limit (%.1f W) is above the speaker RMS rating (%.1f W)",
                    static_cast<double>(cfg.speaker.powerLimitW),
                    static_cast<double>(cfg.speaker.powerRmsW));
    }
    if (cfg.amplifier.maxPowerW > cfg.speaker.powerRmsW && cfg.speaker.powerRmsW > 0.05f) {
        formatIssue(out, Severity::WARNING, "amplifier.maxPower",
                    "the amplifier can deliver %.0f W into a %.0f W speaker: the limiter will cap it",
                    static_cast<double>(cfg.amplifier.maxPowerW),
                    static_cast<double>(cfg.speaker.powerRmsW));
    }
    const float peakScale = SpeakerProtection::safePeakScale(cfg.speaker, cfg.amplifier);
    if (peakScale < 0.2f) {
        formatIssue(out, Severity::INFO, "speaker.powerLimit",
                    "the protection stage will keep the output below %.0f%% of full scale",
                    static_cast<double>(peakScale * 100.0f));
    }
    if (cfg.acoustic.coupling != AcousticCouplingType::OPEN_AIR &&
        cfg.acoustic.highPassHz < cfg.speaker.minFrequencyHz) {
        formatIssue(out, Severity::WARNING, "acoustic.highPassHz",
                    "the chamber high pass (%.0f Hz) is below what the speaker can reproduce",
                    static_cast<double>(cfg.acoustic.highPassHz));
    }

    // --------------------------------------------------------------- valves
    if (cfg.valves.count > kMaxValves) {
        formatIssue(out, Severity::ERROR, "valves.count", "at most %u valves are supported",
                    static_cast<unsigned>(kMaxValves));
    }
    bool usesPca = false;
    for (uint8_t i = 0; i < cfg.valves.count && i < kMaxValves; ++i) {
        const ValveConfig& v = cfg.valves.items[i];
        char field[24];
        snprintf(field, sizeof(field), "valves.%u", static_cast<unsigned>(i + 1));

        if (v.type == ValveActuatorType::SERVO) {
            if (v.driver == ServoDriverType::PCA9685) {
                usesPca = true;
                if (v.channel < 0 || v.channel > 15) {
                    formatIssue(out, Severity::ERROR, field,
                                "valve %u: PCA9685 channel %d is outside 0..15",
                                static_cast<unsigned>(i + 1), static_cast<int>(v.channel));
                }
                for (uint8_t j = 0; j < i; ++j) {
                    const ValveConfig& w = cfg.valves.items[j];
                    if (w.type == ValveActuatorType::SERVO &&
                        w.driver == ServoDriverType::PCA9685 && w.channel == v.channel) {
                        formatIssue(out, Severity::ERROR, field,
                                    "valve %u shares PCA9685 channel %d with valve %u",
                                    static_cast<unsigned>(i + 1), static_cast<int>(v.channel),
                                    static_cast<unsigned>(j + 1));
                    }
                }
            } else {
                char owner[24];
                snprintf(owner, sizeof(owner), "servo %u", static_cast<unsigned>(i + 1));
                if (v.gpio < 0) {
                    formatIssue(out, Severity::ERROR, field, "valve %u has no GPIO assigned",
                                static_cast<unsigned>(i + 1));
                }
                checkPin(v.gpio, owner, field, true, caps, pins, out);
            }
            if (v.pressedAngle > 180 || v.releasedAngle > 180) {
                formatIssue(out, Severity::ERROR, field,
                            "valve %u: servo angles must stay within 0..180 degrees",
                            static_cast<unsigned>(i + 1));
            }
            if (v.pressedAngle == v.releasedAngle) {
                formatIssue(out, Severity::WARNING, field,
                            "valve %u: pressed and released angles are identical",
                            static_cast<unsigned>(i + 1));
            }
            if (v.minPulseUs >= v.maxPulseUs) {
                formatIssue(out, Severity::ERROR, field,
                            "valve %u: the servo pulse range is inverted",
                            static_cast<unsigned>(i + 1));
            }
        } else if (v.type == ValveActuatorType::SOLENOID) {
            char owner[24];
            snprintf(owner, sizeof(owner), "solenoid %u", static_cast<unsigned>(i + 1));
            if (v.gpio < 0) {
                formatIssue(out, Severity::ERROR, field, "valve %u has no GPIO assigned",
                            static_cast<unsigned>(i + 1));
            }
            checkPin(v.gpio, owner, field, true, caps, pins, out);

            if (v.maxOnMs == 0) {
                formatIssue(out, Severity::ERROR, field,
                            "valve %u: a solenoid must have a maximum ON time",
                            static_cast<unsigned>(i + 1));
            } else if (v.maxOnMs > 20000) {
                formatIssue(out, Severity::WARNING, field,
                            "valve %u: %u ms of continuous drive will overheat most coils",
                            static_cast<unsigned>(i + 1), static_cast<unsigned>(v.maxOnMs));
            }
            if (v.holdPwm > v.pullInPwm) {
                formatIssue(out, Severity::WARNING, field,
                            "valve %u: the hold level is above the pull-in level",
                            static_cast<unsigned>(i + 1));
            }
            if (v.pullInPwm > 100 || v.holdPwm > 100) {
                formatIssue(out, Severity::ERROR, field,
                            "valve %u: PWM levels are percentages (0..100)",
                            static_cast<unsigned>(i + 1));
            }
            if (v.holdPwm > 60) {
                formatIssue(out, Severity::WARNING, field,
                            "valve %u: a hold level above 60%% runs hot on most solenoids",
                            static_cast<unsigned>(i + 1));
            }
            if (v.cooldownMs == 0) {
                formatIssue(out, Severity::WARNING, field,
                            "valve %u: no cooldown configured after a thermal fault",
                            static_cast<unsigned>(i + 1));
            }
        }
    }
    if (usesPca) {
        checkPin(cfg.valves.pca9685I2c.sda, "PCA9685 SDA", "valves.pca.sda", true, caps, pins,
                 out);
        checkPin(cfg.valves.pca9685I2c.scl, "PCA9685 SCL", "valves.pca.scl", true, caps, pins,
                 out);
        checkPin(cfg.valves.pca9685OePin, "PCA9685 OE", "valves.pca.oe", true, caps, pins, out);
        if (usesCodecI2c && cfg.valves.pca9685I2c.sda == cfg.audio.i2c.sda &&
            cfg.valves.pca9685I2c.scl == cfg.audio.i2c.scl &&
            cfg.valves.pca9685Address == cfg.audio.codecAddress) {
            formatIssue(out, Severity::ERROR, "valves.pca.address",
                        "the PCA9685 and the codec share the same I2C address");
        }
    }

    // ----------------------------------------------------------------- MIDI
    if (cfg.midi.usb.inEnabled || cfg.midi.usb.outEnabled) {
        if (!caps.hasNativeUsb) {
            formatIssue(out, Severity::ERROR, "midi.usb",
                        "this board has no native USB: class compliant USB-MIDI is unavailable");
        }
    }
    if ((cfg.midi.ble.inEnabled || cfg.midi.ble.outEnabled) && !caps.hasBle) {
        formatIssue(out, Severity::ERROR, "midi.ble", "this board has no BLE radio");
    }
    if (cfg.midi.din.inEnabled) {
        checkPin(cfg.midi.din.rxGpio, "DIN MIDI IN", "midi.din.rxGpio", false, caps, pins, out);
    }
    if (cfg.midi.din.outEnabled || cfg.midi.din.thruEnabled) {
        checkPin(cfg.midi.din.txGpio, "DIN MIDI OUT", "midi.din.txGpio", true, caps, pins, out);
    }
    if (cfg.midi.din.uartNum > 2) {
        formatIssue(out, Severity::ERROR, "midi.din.uart", "UART %u does not exist",
                    static_cast<unsigned>(cfg.midi.din.uartNum));
    }
    if (cfg.midi.din.uartNum == 0) {
        formatIssue(out, Severity::WARNING, "midi.din.uart",
                    "UART0 is the debug console: DIN MIDI will fight with the log output");
    }
    if (cfg.midi.globalChannelMask == 0) {
        formatIssue(out, Severity::ERROR, "midi.channelMask",
                    "every MIDI channel is filtered out: nothing will ever play");
    }

    bool anyEngineRoute = false;
    for (uint8_t i = 0; i < cfg.midi.routeCount && i < kMaxRoutes; ++i) {
        const MidiRoute& r = cfg.midi.routes[i];
        if (!r.enabled) continue;
        if (r.destination == MidiPort::SOUND_ENGINE || r.destination == MidiPort::VALVE_ENGINE) {
            anyEngineRoute = true;
        }
        if (r.noteMin > r.noteMax) {
            formatIssue(out, Severity::ERROR, "midi.routes",
                        "route %u has an inverted note range", static_cast<unsigned>(i + 1));
        }
        if (!cfg.midi.suppressLoops && r.source == r.destination) {
            formatIssue(out, Severity::WARNING, "midi.routes",
                        "route %u loops a port back onto itself", static_cast<unsigned>(i + 1));
        }
    }
    if (!anyEngineRoute) {
        formatIssue(out, Severity::WARNING, "midi.routes",
                    "no route reaches the sound or valve engine: the instrument stays silent");
    }

    // ----------------------------------------------------------- instrument
    if (cfg.instrument.noteMin > cfg.instrument.noteMax) {
        formatIssue(out, Severity::ERROR, "instrument.range",
                    "the instrument note range is inverted");
    }
}

bool ConfigValidator::sanitise(InstrumentConfiguration& cfg, const BoardCapabilities& caps) {
    bool changed = false;

    if (!caps.supportsBackend(cfg.audio.backend)) {
        cfg.audio.backend = AudioBackendType::NONE;
        cfg.amplifier.type = AmplifierType::NONE;
        changed = true;
    }
    if (!caps.hasNativeUsb && (cfg.midi.usb.inEnabled || cfg.midi.usb.outEnabled)) {
        cfg.midi.usb.inEnabled = false;
        cfg.midi.usb.outEnabled = false;
        changed = true;
    }
    if (!caps.hasBle && (cfg.midi.ble.inEnabled || cfg.midi.ble.outEnabled)) {
        cfg.midi.ble.inEnabled = false;
        cfg.midi.ble.outEnabled = false;
        changed = true;
    }
    if (cfg.valves.count > kMaxValves) {
        cfg.valves.count = kMaxValves;
        changed = true;
    }
    if (cfg.audio.sampleRate < 8000 || cfg.audio.sampleRate > 96000) {
        cfg.audio.sampleRate = 48000;
        changed = true;
    }
    if (cfg.midi.globalChannelMask == 0) {
        cfg.midi.globalChannelMask = 0xFFFF;
        changed = true;
    }
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        ValveConfig& v = cfg.valves.items[i];
        if (v.type != ValveActuatorType::SOLENOID) continue;
        // Never allow a stored file to disable the thermal guard.
        if (v.maxOnMs == 0 || v.maxOnMs > 20000) {
            v.maxOnMs = 5000;
            changed = true;
        }
        if (v.pullInPwm > 100) {
            v.pullInPwm = 100;
            changed = true;
        }
        if (v.holdPwm > 100) {
            v.holdPwm = 100;
            changed = true;
        }
        if (v.cooldownMs == 0) {
            v.cooldownMs = 3000;
            changed = true;
        }
    }
    return changed;
}

}  // namespace ot
