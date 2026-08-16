#include "config/ConfigValidator.h"

#include <cstdarg>
#include <cstdio>

#include "audio/AcousticModel.h"
#include "audio/Limiter.h"
#include "valves/ValveTiming.h"

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
    if (cfg.speaker.powerMaxW > 0.05f && cfg.speaker.powerRmsW > cfg.speaker.powerMaxW) {
        formatIssue(out, Severity::ERROR, "speaker.powerRms",
                    "the continuous rating (%.1f W) is above the maximum rating (%.1f W)",
                    static_cast<double>(cfg.speaker.powerRmsW),
                    static_cast<double>(cfg.speaker.powerMaxW));
    }

    // ------------------------------------------------------------- acoustic
    if (cfg.acoustic.coupling != AcousticCouplingType::OPEN_AIR) {
        const AcousticModel model = computeAcousticModel(cfg.acoustic, cfg.speaker);

        if (model.highPassHz < cfg.speaker.minFrequencyHz) {
            formatIssue(out, Severity::WARNING, "acoustic.measuredHighPassHz",
                        "the chamber high pass (%.0f Hz) is below what the speaker can reproduce",
                        static_cast<double>(model.highPassHz));
        }
        // A compression ratio much under 2:1 wastes the chamber; much over
        // 10:1 and the throat is where the distortion is made.
        if (model.compressionRatio > 0.0f &&
            (model.compressionRatio < 2.0f || model.compressionRatio > 12.0f)) {
            formatIssue(out, Severity::WARNING, "acoustic.leadpipeDiameterMm",
                        "compression ratio %.1f:1 - outside the 2:1..12:1 range a cone works in",
                        static_cast<double>(model.compressionRatio));
        }
        // A cone steeper than this reflects instead of transforming.
        if (model.stage1HalfAngleDeg > 30.0f || model.stage2HalfAngleDeg > 30.0f) {
            formatIssue(out, Severity::WARNING, "acoustic.stage1",
                        "a cone half angle of %.0f deg is too abrupt: lengthen the stage",
                        static_cast<double>(model.stage1HalfAngleDeg > model.stage2HalfAngleDeg
                                                ? model.stage1HalfAngleDeg
                                                : model.stage2HalfAngleDeg));
        }
        // The front cavity is a low pass on everything the bore receives.  A
        // trumpet needs its harmonics well past 4 kHz to sound like one.
        if (model.frontChamberCornerHz > 0.0f && model.frontChamberCornerHz < 4000.0f) {
            formatIssue(out, Severity::WARNING, "acoustic.frontChamberVolumeMl",
                        "the front chamber starts loading the throat at %.0f Hz: reduce its "
                        "volume or shorten the cone",
                        static_cast<double>(model.frontChamberCornerHz));
        }
        if (model.highPassSource != AcousticSource::MEASURED) {
            formatIssue(out, Severity::INFO, "acoustic.measuredHighPassHz",
                        "the %.0f Hz high pass is %s, not measured - confirm it at the bench",
                        static_cast<double>(model.highPassHz),
                        model.highPassSource == AcousticSource::DERIVED
                            ? "derived from the geometry"
                            : "the driver's own recommendation");
        }
    }

    // ---------------------------------------------------- valve/audio sync
    if (cfg.valves.sync.enabled && cfg.valves.mode == ValveMode::AUTO) {
        uint16_t worst = 0;
        for (uint8_t i = 0; i < cfg.valves.count && i < kMaxValves; ++i) {
            const uint16_t ms = valveSettleMs(cfg.valves.items[i]);
            if (ms > worst) worst = ms;
        }
        if (worst > cfg.valves.sync.maxDelayMs) {
            formatIssue(out, Severity::WARNING, "valves.sync.maxDelayMs",
                        "the slowest piston needs %u ms but the attack delay is capped at %u ms: "
                        "the note will still start early",
                        static_cast<unsigned>(worst),
                        static_cast<unsigned>(cfg.valves.sync.maxDelayMs));
        }
        if (worst > 90) {
            formatIssue(out, Severity::INFO, "valves.sync",
                        "a %u ms attack delay is audible as sluggishness: a faster servo or a "
                        "shorter travel is worth more than any DSP setting",
                        static_cast<unsigned>(worst));
        }
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

// A value that is not a number poisons every filter it touches and the only
// symptom is silence, so it is replaced rather than clamped.
static float finite(float v, float fallback) {
    return (v == v && v > -1e30f && v < 1e30f) ? v : fallback;
}

void ConfigValidator::sanitiseVoicing(VoicingConfig& v) {
    const VoicingConfig d;

    v.name[kNameLen - 1] = '\0';
    if (static_cast<uint8_t>(v.engine) > static_cast<uint8_t>(SynthEngineType::BRASS_EXCITER)) {
        v.engine = SynthEngineType::ADDITIVE;
    }

    v.envelope.attackMs = clampValue(finite(v.envelope.attackMs, d.envelope.attackMs), 0.5f, 500.0f);
    v.envelope.decayMs = clampValue(finite(v.envelope.decayMs, d.envelope.decayMs), 1.0f, 2000.0f);
    v.envelope.sustain = clampValue(finite(v.envelope.sustain, d.envelope.sustain), 0.0f, 1.0f);
    v.envelope.releaseMs =
        clampValue(finite(v.envelope.releaseMs, d.envelope.releaseMs), 1.0f, 3000.0f);
    v.envelope.attackNoise =
        clampValue(finite(v.envelope.attackNoise, d.envelope.attackNoise), 0.0f, 1.0f);
    v.envelope.breathNoise =
        clampValue(finite(v.envelope.breathNoise, d.envelope.breathNoise), 0.0f, 0.5f);

    v.vibrato.frequencyHz =
        clampValue(finite(v.vibrato.frequencyHz, d.vibrato.frequencyHz), 0.1f, 20.0f);
    v.vibrato.depthCents = clampValue(finite(v.vibrato.depthCents, 0.0f), 0.0f, 200.0f);
    v.vibrato.delayMs = clampValue(finite(v.vibrato.delayMs, 0.0f), 0.0f, 5000.0f);
    v.vibrato.fadeInMs = clampValue(finite(v.vibrato.fadeInMs, 1.0f), 1.0f, 5000.0f);

    if (v.additive.harmonicCount == 0) v.additive.harmonicCount = 1;
    if (v.additive.harmonicCount > kMaxHarmonics) v.additive.harmonicCount = kMaxHarmonics;
    for (uint8_t i = 0; i < kMaxHarmonics; ++i) {
        v.additive.harmonicGain[i] = clampValue(finite(v.additive.harmonicGain[i], 0.0f), 0.0f, 2.0f);
    }
    v.additive.velocityBrightness = clampValue(finite(v.additive.velocityBrightness, 0.85f), 0.0f, 1.0f);
    v.additive.breathBrightness = clampValue(finite(v.additive.breathBrightness, 0.7f), 0.0f, 1.0f);
    v.additive.expressionBrightness =
        clampValue(finite(v.additive.expressionBrightness, 0.35f), 0.0f, 1.0f);
    v.additive.pitchBrightness = clampValue(finite(v.additive.pitchBrightness, 0.3f), 0.0f, 1.0f);

    v.exciter.drive = clampValue(finite(v.exciter.drive, d.exciter.drive), 0.1f, 12.0f);
    v.exciter.asymmetry = clampValue(finite(v.exciter.asymmetry, 0.0f), -1.0f, 1.0f);
    v.exciter.pressure = clampValue(finite(v.exciter.pressure, d.exciter.pressure), 0.0f, 1.0f);
    v.exciter.pressureToDrive = clampValue(finite(v.exciter.pressureToDrive, 0.0f), 0.0f, 4.0f);
    v.exciter.noiseAmount = clampValue(finite(v.exciter.noiseAmount, 0.0f), 0.0f, 1.0f);
    v.exciter.transientMs =
        clampValue(finite(v.exciter.transientMs, d.exciter.transientMs), 0.5f, 500.0f);

    v.darkTilt = clampValue(finite(v.darkTilt, d.darkTilt), 0.0f, 6.0f);
    v.brightTilt = clampValue(finite(v.brightTilt, d.brightTilt), 0.0f, 6.0f);
    v.hybridMix = clampValue(finite(v.hybridMix, d.hybridMix), 0.0f, 1.0f);
    v.velocityFloor = clampValue(finite(v.velocityFloor, d.velocityFloor), 0.0f, 1.0f);
    v.breathToVolume = clampValue(finite(v.breathToVolume, 1.0f), 0.0f, 1.0f);
    v.expressionToVolume = clampValue(finite(v.expressionToVolume, 1.0f), 0.0f, 1.0f);
    v.aftertouchToBrightness = clampValue(finite(v.aftertouchToBrightness, 0.25f), 0.0f, 1.0f);
    v.aftertouchToVolume = clampValue(finite(v.aftertouchToVolume, 0.0f), 0.0f, 1.0f);

    if (v.pitchBendRangeSemitones == 0 || v.pitchBendRangeSemitones > 48) {
        v.pitchBendRangeSemitones = 2;
    }

    // The trim sits before the limiter, so it cannot get past the protection
    // stage - but +40 dB of it would make the limiter the only thing between
    // the DSP and the coil, which is not a state to leave an instrument in.
    v.outputTrimDb = clampValue(finite(v.outputTrimDb, 0.0f), -24.0f, 12.0f);

    for (uint8_t i = 0; i < kMaxEqBands; ++i) {
        v.eq[i].frequency = clampValue(finite(v.eq[i].frequency, 1000.0f), 20.0f, 20000.0f);
        v.eq[i].gainDb = clampValue(finite(v.eq[i].gainDb, 0.0f), -18.0f, 18.0f);
        v.eq[i].q = clampValue(finite(v.eq[i].q, 0.7f), 0.1f, 10.0f);
    }

    for (uint8_t i = 0; i < kRegisterPoints; ++i) {
        if (v.registerCurve[i].note > 127) v.registerCurve[i].note = 127;
        v.registerCurve[i].gainDb = clampValue(finite(v.registerCurve[i].gainDb, 0.0f), -18.0f, 12.0f);
        v.registerCurve[i].brightness =
            clampValue(finite(v.registerCurve[i].brightness, 0.0f), -1.0f, 1.0f);
    }
    // The curve is interpolated by walking it in order; an unsorted curve
    // would silently skip breakpoints. Sorting beats rejecting: the builder
    // dragged a point past its neighbour, they did not make a mistake.
    for (uint8_t i = 1; i < kRegisterPoints; ++i) {
        for (uint8_t j = i; j > 0 && v.registerCurve[j].note < v.registerCurve[j - 1].note; --j) {
            const RegisterPoint tmp = v.registerCurve[j];
            v.registerCurve[j] = v.registerCurve[j - 1];
            v.registerCurve[j - 1] = tmp;
        }
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
    sanitiseVoicing(cfg.voicing);
    for (uint8_t i = 0; i < cfg.voicings.count && i < kMaxVoicings; ++i) {
        sanitiseVoicing(cfg.voicings.items[i]);
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
