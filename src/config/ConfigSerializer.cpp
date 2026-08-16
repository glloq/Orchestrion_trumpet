#include "config/ConfigSerializer.h"

#include "config/ConfigMigration.h"

namespace ot {

namespace {

template <typename E>
void readEnum(JsonVariantConst v, E& out) {
    if (v.isNull()) return;
    const char* s = v.as<const char*>();
    if (!s) return;
    E parsed;
    if (parseEnum(s, parsed)) out = parsed;
}

void readString(JsonVariantConst v, char* dst, size_t size) {
    if (v.isNull()) return;
    const char* s = v.as<const char*>();
    if (s) copyString(dst, size, s);
}

template <typename T>
void readNumber(JsonVariantConst v, T& out) {
    if (v.isNull()) return;
    if (v.template is<float>() || v.template is<int>()) out = v.template as<T>();
}

void readBool(JsonVariantConst v, bool& out) {
    if (v.isNull()) return;
    if (v.is<bool>()) out = v.as<bool>();
}

void readHornStage(JsonObjectConst o, HornStageConfig& h) {
    if (o.isNull()) return;
    readNumber(o["inletDiameterMm"], h.inletDiameterMm);
    readNumber(o["outletDiameterMm"], h.outletDiameterMm);
    readNumber(o["lengthMm"], h.lengthMm);
}

void writeHornStage(JsonObject o, const HornStageConfig& h) {
    o["inletDiameterMm"] = h.inletDiameterMm;
    o["outletDiameterMm"] = h.outletDiameterMm;
    o["lengthMm"] = h.lengthMm;
}

}  // namespace

// ---------------------------------------------------------------------------
// Voicing.  Written as its own object, and read from either its own object or
// - for a v3 file - from inside `audio`, where the sound used to live.
// ---------------------------------------------------------------------------
void writeVoicing(JsonObject o, const VoicingConfig& v) {
    o["name"] = v.name;
    o["engine"] = toString(v.engine);
    o["pitchBendRange"] = v.pitchBendRangeSemitones;
    o["darkTilt"] = v.darkTilt;
    o["brightTilt"] = v.brightTilt;
    o["hybridMix"] = v.hybridMix;
    o["velocityFloor"] = v.velocityFloor;
    o["breathToVolume"] = v.breathToVolume;
    o["expressionToVolume"] = v.expressionToVolume;
    o["aftertouchToBrightness"] = v.aftertouchToBrightness;
    o["aftertouchToVolume"] = v.aftertouchToVolume;
    o["outputTrimDb"] = v.outputTrimDb;

    JsonObject env = o["envelope"].to<JsonObject>();
    env["attackMs"] = v.envelope.attackMs;
    env["decayMs"] = v.envelope.decayMs;
    env["sustain"] = v.envelope.sustain;
    env["releaseMs"] = v.envelope.releaseMs;
    env["attackNoise"] = v.envelope.attackNoise;
    env["breathNoise"] = v.envelope.breathNoise;

    JsonObject vib = o["vibrato"].to<JsonObject>();
    vib["source"] = toString(v.vibrato.source);
    vib["frequencyHz"] = v.vibrato.frequencyHz;
    vib["depthCents"] = v.vibrato.depthCents;
    vib["delayMs"] = v.vibrato.delayMs;
    vib["fadeInMs"] = v.vibrato.fadeInMs;

    JsonObject add = o["additive"].to<JsonObject>();
    add["harmonicCount"] = v.additive.harmonicCount;
    JsonArray gains = add["harmonicGain"].to<JsonArray>();
    for (uint8_t i = 0; i < kMaxHarmonics; ++i) gains.add(v.additive.harmonicGain[i]);
    add["velocityBrightness"] = v.additive.velocityBrightness;
    add["breathBrightness"] = v.additive.breathBrightness;
    add["expressionBrightness"] = v.additive.expressionBrightness;
    add["pitchBrightness"] = v.additive.pitchBrightness;

    JsonObject ex = o["exciter"].to<JsonObject>();
    ex["drive"] = v.exciter.drive;
    ex["asymmetry"] = v.exciter.asymmetry;
    ex["pressure"] = v.exciter.pressure;
    ex["pressureToDrive"] = v.exciter.pressureToDrive;
    ex["noiseAmount"] = v.exciter.noiseAmount;
    ex["transientMs"] = v.exciter.transientMs;

    JsonArray eq = o["eq"].to<JsonArray>();
    for (uint8_t i = 0; i < kMaxEqBands; ++i) {
        JsonObject b = eq.add<JsonObject>();
        b["frequency"] = v.eq[i].frequency;
        b["gainDb"] = v.eq[i].gainDb;
        b["q"] = v.eq[i].q;
        b["enabled"] = v.eq[i].enabled;
    }

    JsonArray reg = o["registerCurve"].to<JsonArray>();
    for (uint8_t i = 0; i < kRegisterPoints; ++i) {
        JsonObject b = reg.add<JsonObject>();
        b["note"] = v.registerCurve[i].note;
        b["gainDb"] = v.registerCurve[i].gainDb;
        b["brightness"] = v.registerCurve[i].brightness;
    }
}

void readVoicing(JsonObjectConst o, VoicingConfig& v) {
    if (o.isNull()) return;
    readString(o["name"], v.name, sizeof(v.name));
    readEnum(o["engine"], v.engine);
    readNumber(o["pitchBendRange"], v.pitchBendRangeSemitones);
    readNumber(o["darkTilt"], v.darkTilt);
    readNumber(o["brightTilt"], v.brightTilt);
    readNumber(o["hybridMix"], v.hybridMix);
    readNumber(o["velocityFloor"], v.velocityFloor);
    readNumber(o["breathToVolume"], v.breathToVolume);
    readNumber(o["expressionToVolume"], v.expressionToVolume);
    readNumber(o["aftertouchToBrightness"], v.aftertouchToBrightness);
    readNumber(o["aftertouchToVolume"], v.aftertouchToVolume);
    readNumber(o["outputTrimDb"], v.outputTrimDb);

    JsonObjectConst env = o["envelope"];
    readNumber(env["attackMs"], v.envelope.attackMs);
    readNumber(env["decayMs"], v.envelope.decayMs);
    readNumber(env["sustain"], v.envelope.sustain);
    readNumber(env["releaseMs"], v.envelope.releaseMs);
    readNumber(env["attackNoise"], v.envelope.attackNoise);
    readNumber(env["breathNoise"], v.envelope.breathNoise);

    JsonObjectConst vib = o["vibrato"];
    readEnum(vib["source"], v.vibrato.source);
    readNumber(vib["frequencyHz"], v.vibrato.frequencyHz);
    readNumber(vib["depthCents"], v.vibrato.depthCents);
    readNumber(vib["delayMs"], v.vibrato.delayMs);
    readNumber(vib["fadeInMs"], v.vibrato.fadeInMs);

    JsonObjectConst add = o["additive"];
    readNumber(add["harmonicCount"], v.additive.harmonicCount);
    JsonArrayConst gains = add["harmonicGain"];
    for (uint8_t i = 0; i < kMaxHarmonics && i < gains.size(); ++i) {
        readNumber(gains[i], v.additive.harmonicGain[i]);
    }
    readNumber(add["velocityBrightness"], v.additive.velocityBrightness);
    readNumber(add["breathBrightness"], v.additive.breathBrightness);
    readNumber(add["expressionBrightness"], v.additive.expressionBrightness);
    readNumber(add["pitchBrightness"], v.additive.pitchBrightness);

    JsonObjectConst ex = o["exciter"];
    readNumber(ex["drive"], v.exciter.drive);
    readNumber(ex["asymmetry"], v.exciter.asymmetry);
    readNumber(ex["pressure"], v.exciter.pressure);
    readNumber(ex["pressureToDrive"], v.exciter.pressureToDrive);
    readNumber(ex["noiseAmount"], v.exciter.noiseAmount);
    readNumber(ex["transientMs"], v.exciter.transientMs);

    JsonArrayConst eq = o["eq"];
    for (uint8_t i = 0; i < kMaxEqBands && i < eq.size(); ++i) {
        JsonObjectConst b = eq[i];
        readNumber(b["frequency"], v.eq[i].frequency);
        readNumber(b["gainDb"], v.eq[i].gainDb);
        readNumber(b["q"], v.eq[i].q);
        readBool(b["enabled"], v.eq[i].enabled);
    }

    JsonArrayConst reg = o["registerCurve"];
    for (uint8_t i = 0; i < kRegisterPoints && i < reg.size(); ++i) {
        JsonObjectConst b = reg[i];
        readNumber(b["note"], v.registerCurve[i].note);
        readNumber(b["gainDb"], v.registerCurve[i].gainDb);
        readNumber(b["brightness"], v.registerCurve[i].brightness);
    }
}

namespace {

void readI2s(JsonObjectConst o, I2sPins& p) {
    if (o.isNull()) return;
    readNumber(o["bclk"], p.bclk);
    readNumber(o["ws"], p.ws);
    readNumber(o["dout"], p.dout);
    readNumber(o["din"], p.din);
    readNumber(o["mclk"], p.mclk);
}

void writeI2s(JsonObject o, const I2sPins& p) {
    o["bclk"] = p.bclk;
    o["ws"] = p.ws;
    o["dout"] = p.dout;
    o["din"] = p.din;
    o["mclk"] = p.mclk;
}

void readI2c(JsonObjectConst o, I2cPins& p) {
    if (o.isNull()) return;
    readNumber(o["sda"], p.sda);
    readNumber(o["scl"], p.scl);
    readNumber(o["frequency"], p.frequency);
}

void writeI2c(JsonObject o, const I2cPins& p) {
    o["sda"] = p.sda;
    o["scl"] = p.scl;
    o["frequency"] = p.frequency;
}

}  // namespace

void configToJson(const InstrumentConfiguration& cfg, JsonObject root, SecretPolicy secrets) {
    root["schemaVersion"] = cfg.schemaVersion;

    JsonObject board = root["board"].to<JsonObject>();
    board["type"] = toString(cfg.board);

    JsonObject system = root["system"].to<JsonObject>();
    system["deviceName"] = cfg.system.deviceName;
    system["preset"] = cfg.system.presetName;
    system["wizardCompleted"] = cfg.system.wizardCompleted;
    system["safeModeForced"] = cfg.system.safeModeForced;

    JsonObject wifi = root["wifi"].to<JsonObject>();
    wifi["mode"] = toString(cfg.wifi.mode);
    wifi["ssid"] = cfg.wifi.ssid;
    wifi["apSsid"] = cfg.wifi.apSsid;
    // The UI needs to know whether a password exists, never what it is.
    wifi["passwordSet"] = cfg.wifi.password[0] != '\0';
    wifi["apPasswordSet"] = cfg.wifi.apPassword[0] != '\0';
    if (secrets == SecretPolicy::INCLUDE) {
        wifi["password"] = cfg.wifi.password;
        wifi["apPassword"] = cfg.wifi.apPassword;
    }
    wifi["hostname"] = cfg.wifi.hostname;
    wifi["apChannel"] = cfg.wifi.apChannel;
    wifi["captivePortal"] = cfg.wifi.captivePortal;

    JsonObject audio = root["audio"].to<JsonObject>();
    audio["backend"] = toString(cfg.audio.backend);
    audio["sampleRate"] = cfg.audio.sampleRate;
    audio["bitDepth"] = cfg.audio.bitDepth;
    audio["blockSize"] = cfg.audio.blockSize;
    audio["dmaBuffers"] = cfg.audio.dmaBuffers;
    audio["masterVolume"] = cfg.audio.masterVolume;
    audio["programChangeSelectsVoicing"] = cfg.audio.programChangeSelectsVoicing;
    audio["codecAddress"] = cfg.audio.codecAddress;
    audio["sdModePin"] = cfg.audio.sdModePin;
    audio["internalDacChannel"] = cfg.audio.internalDacChannel;
    audio["highPassHz"] = cfg.audio.highPassHz;
    audio["startupMute"] = cfg.audio.startupMute;
    writeI2s(audio["i2s"].to<JsonObject>(), cfg.audio.i2s);
    writeI2c(audio["i2c"].to<JsonObject>(), cfg.audio.i2c);

    writeVoicing(root["voicing"].to<JsonObject>(), cfg.voicing);
    JsonArray voicings = root["voicings"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg.voicings.count && i < kMaxVoicings; ++i) {
        writeVoicing(voicings.add<JsonObject>(), cfg.voicings.items[i]);
    }

    JsonObject lim = audio["limiter"].to<JsonObject>();
    lim["enabled"] = cfg.audio.limiter.enabled;
    lim["thresholdDb"] = cfg.audio.limiter.thresholdDb;
    lim["attackMs"] = cfg.audio.limiter.attackMs;
    lim["releaseMs"] = cfg.audio.limiter.releaseMs;
    lim["hardCeiling"] = cfg.audio.limiter.hardCeiling;

    JsonObject amp = root["amplifier"].to<JsonObject>();
    amp["type"] = toString(cfg.amplifier.type);
    amp["maxPower"] = cfg.amplifier.maxPowerW;
    amp["gainDb"] = cfg.amplifier.gainDb;
    amp["speakerImpedance"] = cfg.amplifier.speakerImpedanceOhm;
    amp["volumeLimit"] = cfg.amplifier.volumeLimit;

    JsonObject spk = root["speaker"].to<JsonObject>();
    spk["profile"] = toString(cfg.speaker.profile);
    spk["name"] = cfg.speaker.name;
    spk["impedance"] = cfg.speaker.impedanceOhm;
    spk["powerRms"] = cfg.speaker.powerRmsW;
    spk["powerMax"] = cfg.speaker.powerMaxW;
    spk["minFrequency"] = cfg.speaker.minFrequencyHz;
    spk["maxFrequency"] = cfg.speaker.maxFrequencyHz;
    spk["recommendedHighPass"] = cfg.speaker.recommendedHighPassHz;
    spk["gainCorrectionDb"] = cfg.speaker.gainCorrectionDb;
    spk["powerLimit"] = cfg.speaker.powerLimitW;
    spk["fsHz"] = cfg.speaker.fsHz;
    spk["vasLitres"] = cfg.speaker.vasLitres;

    JsonObject aco = root["acoustic"].to<JsonObject>();
    aco["coupling"] = toString(cfg.acoustic.coupling);
    aco["rearChamberVolumeMl"] = cfg.acoustic.rearChamberVolumeMl;
    aco["frontChamberVolumeMl"] = cfg.acoustic.frontChamberVolumeMl;
    aco["frontChamberDepthMm"] = cfg.acoustic.frontChamberDepthMm;
    writeHornStage(aco["stage1"].to<JsonObject>(), cfg.acoustic.stage1);
    aco["intermediateDiameterMm"] = cfg.acoustic.intermediateDiameterMm;
    aco["intermediateLengthMm"] = cfg.acoustic.intermediateLengthMm;
    writeHornStage(aco["stage2"].to<JsonObject>(), cfg.acoustic.stage2);
    aco["leadpipeDiameterMm"] = cfg.acoustic.leadpipeDiameterMm;
    aco["measuredHighPassHz"] = cfg.acoustic.measuredHighPassHz;
    aco["eqGainDb"] = cfg.acoustic.eqGainDb;

    JsonObject valves = root["valves"].to<JsonObject>();
    valves["count"] = cfg.valves.count;
    valves["mode"] = toString(cfg.valves.mode);
    valves["pca9685Address"] = cfg.valves.pca9685Address;
    valves["pca9685OePin"] = cfg.valves.pca9685OePin;
    valves["servoFrequencyHz"] = cfg.valves.servoFrequencyHz;
    valves["solenoidPwmFrequencyHz"] = cfg.valves.solenoidPwmFrequencyHz;
    writeI2c(valves["pca9685I2c"].to<JsonObject>(), cfg.valves.pca9685I2c);

    JsonObject sync = valves["sync"].to<JsonObject>();
    sync["enabled"] = cfg.valves.sync.enabled;
    sync["onlyWhenFingeringChanges"] = cfg.valves.sync.onlyWhenFingeringChanges;
    sync["trimMs"] = cfg.valves.sync.trimMs;
    sync["maxDelayMs"] = cfg.valves.sync.maxDelayMs;

    JsonArray items = valves["items"].to<JsonArray>();
    for (uint8_t i = 0; i < kMaxValves; ++i) {
        const ValveConfig& v = cfg.valves.items[i];
        JsonObject o = items.add<JsonObject>();
        o["type"] = toString(v.type);
        o["driver"] = toString(v.driver);
        o["gpio"] = v.gpio;
        o["channel"] = v.channel;
        o["pressedAngle"] = v.pressedAngle;
        o["releasedAngle"] = v.releasedAngle;
        o["speed"] = v.speedDegPerSec;
        o["acceleration"] = v.accelDegPerSec2;
        o["invert"] = v.invert;
        o["detachAfterMove"] = v.detachAfterMove;
        o["detachDelayMs"] = v.detachDelayMs;
        o["minPulseUs"] = v.minPulseUs;
        o["maxPulseUs"] = v.maxPulseUs;
        o["activeHigh"] = v.activeHigh;
        o["pullInPwm"] = v.pullInPwm;
        o["pullInMs"] = v.pullInMs;
        o["holdPwm"] = v.holdPwm;
        o["maxOnMs"] = v.maxOnMs;
        o["cooldownMs"] = v.cooldownMs;
        o["maxDutyPercent"] = v.maxDutyPercent;
        o["cc"] = v.ccNumber;
        o["measuredSettleMs"] = v.measuredSettleMs;
    }

    JsonObject inst = root["instrument"].to<JsonObject>();
    inst["type"] = toString(cfg.instrument.type);
    inst["pitchMode"] = toString(cfg.instrument.pitchMode);
    inst["customTranspose"] = cfg.instrument.customTransposeSemitones;
    inst["notePriority"] = toString(cfg.instrument.notePriority);
    inst["legato"] = cfg.instrument.legato;
    inst["retrigger"] = cfg.instrument.retrigger;
    inst["portamentoMs"] = cfg.instrument.portamentoMs;
    inst["noteMin"] = cfg.instrument.noteMin;
    inst["noteMax"] = cfg.instrument.noteMax;

    JsonObject midi = root["midi"].to<JsonObject>();
    JsonObject usb = midi["usb"].to<JsonObject>();
    usb["in"] = cfg.midi.usb.inEnabled;
    usb["out"] = cfg.midi.usb.outEnabled;
    JsonObject ble = midi["ble"].to<JsonObject>();
    ble["in"] = cfg.midi.ble.inEnabled;
    ble["out"] = cfg.midi.ble.outEnabled;
    ble["name"] = cfg.midi.ble.deviceName;
    JsonObject rtp = midi["rtp"].to<JsonObject>();
    rtp["in"] = cfg.midi.rtp.inEnabled;
    rtp["out"] = cfg.midi.rtp.outEnabled;
    rtp["controlPort"] = cfg.midi.rtp.controlPort;
    rtp["sessionName"] = cfg.midi.rtp.sessionName;
    JsonObject din = midi["din"].to<JsonObject>();
    din["in"] = cfg.midi.din.inEnabled;
    din["out"] = cfg.midi.din.outEnabled;
    din["thru"] = cfg.midi.din.thruEnabled;
    din["rxGpio"] = cfg.midi.din.rxGpio;
    din["txGpio"] = cfg.midi.din.txGpio;
    din["uart"] = cfg.midi.din.uartNum;
    JsonObject web = midi["web"].to<JsonObject>();
    web["in"] = cfg.midi.web.inEnabled;
    web["monitor"] = cfg.midi.web.monitorEnabled;
    midi["channelMask"] = cfg.midi.globalChannelMask;
    midi["outputChannel"] = cfg.midi.outputChannel;
    midi["suppressLoops"] = cfg.midi.suppressLoops;

    JsonArray routes = midi["routes"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg.midi.routeCount && i < kMaxRoutes; ++i) {
        const MidiRoute& r = cfg.midi.routes[i];
        JsonObject o = routes.add<JsonObject>();
        o["source"] = toString(r.source);
        o["destination"] = toString(r.destination);
        o["enabled"] = r.enabled;
        o["channelMask"] = r.channelMask;
        o["transpose"] = r.transpose;
        o["velocityCurve"] = toString(r.velocityCurve);
        o["fixedVelocity"] = r.fixedVelocity;
        o["noteMin"] = r.noteMin;
        o["noteMax"] = r.noteMax;
    }
}

bool configFromJson(JsonObjectConst root, InstrumentConfiguration& cfg) {
    if (root.isNull()) return false;
    JsonVariantConst schema = root["schemaVersion"];
    if (schema.isNull() || !schema.is<int>()) return false;

    cfg = InstrumentConfiguration();
    cfg.schemaVersion = static_cast<uint16_t>(schema.as<int>());

    readEnum(root["board"]["type"], cfg.board);

    JsonObjectConst system = root["system"];
    readString(system["deviceName"], cfg.system.deviceName, sizeof(cfg.system.deviceName));
    readString(system["preset"], cfg.system.presetName, sizeof(cfg.system.presetName));
    readBool(system["wizardCompleted"], cfg.system.wizardCompleted);
    readBool(system["safeModeForced"], cfg.system.safeModeForced);

    JsonObjectConst wifi = root["wifi"];
    readEnum(wifi["mode"], cfg.wifi.mode);
    readString(wifi["ssid"], cfg.wifi.ssid, sizeof(cfg.wifi.ssid));
    readString(wifi["password"], cfg.wifi.password, sizeof(cfg.wifi.password));
    readString(wifi["apSsid"], cfg.wifi.apSsid, sizeof(cfg.wifi.apSsid));
    readString(wifi["apPassword"], cfg.wifi.apPassword, sizeof(cfg.wifi.apPassword));
    readString(wifi["hostname"], cfg.wifi.hostname, sizeof(cfg.wifi.hostname));
    readNumber(wifi["apChannel"], cfg.wifi.apChannel);
    readBool(wifi["captivePortal"], cfg.wifi.captivePortal);

    JsonObjectConst audio = root["audio"];
    readEnum(audio["backend"], cfg.audio.backend);
    readNumber(audio["sampleRate"], cfg.audio.sampleRate);
    readNumber(audio["bitDepth"], cfg.audio.bitDepth);
    readNumber(audio["blockSize"], cfg.audio.blockSize);
    readNumber(audio["dmaBuffers"], cfg.audio.dmaBuffers);
    readNumber(audio["masterVolume"], cfg.audio.masterVolume);
    readBool(audio["programChangeSelectsVoicing"], cfg.audio.programChangeSelectsVoicing);
    readNumber(audio["codecAddress"], cfg.audio.codecAddress);
    readNumber(audio["sdModePin"], cfg.audio.sdModePin);
    readNumber(audio["internalDacChannel"], cfg.audio.internalDacChannel);
    readNumber(audio["highPassHz"], cfg.audio.highPassHz);
    readBool(audio["startupMute"], cfg.audio.startupMute);
    readI2s(audio["i2s"], cfg.audio.i2s);
    readI2c(audio["i2c"], cfg.audio.i2c);

    // v3 files kept the sound inside `audio`. Read it from there first so an
    // existing instrument keeps its voicing, then let a v4 `voicing` object
    // override it.
    readVoicing(audio, cfg.voicing);
    readVoicing(root["voicing"], cfg.voicing);
    JsonArrayConst voicings = root["voicings"];
    cfg.voicings.count = 0;
    for (uint8_t i = 0; i < kMaxVoicings && i < voicings.size(); ++i) {
        cfg.voicings.items[i] = VoicingConfig();
        readVoicing(voicings[i], cfg.voicings.items[i]);
        cfg.voicings.count = static_cast<uint8_t>(i + 1);
    }

    JsonObjectConst lim = audio["limiter"];
    readBool(lim["enabled"], cfg.audio.limiter.enabled);
    readNumber(lim["thresholdDb"], cfg.audio.limiter.thresholdDb);
    readNumber(lim["attackMs"], cfg.audio.limiter.attackMs);
    readNumber(lim["releaseMs"], cfg.audio.limiter.releaseMs);
    readNumber(lim["hardCeiling"], cfg.audio.limiter.hardCeiling);

    JsonObjectConst amp = root["amplifier"];
    readEnum(amp["type"], cfg.amplifier.type);
    readNumber(amp["maxPower"], cfg.amplifier.maxPowerW);
    readNumber(amp["gainDb"], cfg.amplifier.gainDb);
    readNumber(amp["speakerImpedance"], cfg.amplifier.speakerImpedanceOhm);
    readNumber(amp["volumeLimit"], cfg.amplifier.volumeLimit);

    JsonObjectConst spk = root["speaker"];
    readEnum(spk["profile"], cfg.speaker.profile);
    readString(spk["name"], cfg.speaker.name, sizeof(cfg.speaker.name));
    readNumber(spk["impedance"], cfg.speaker.impedanceOhm);
    readNumber(spk["powerRms"], cfg.speaker.powerRmsW);
    readNumber(spk["powerMax"], cfg.speaker.powerMaxW);
    readNumber(spk["fsHz"], cfg.speaker.fsHz);
    readNumber(spk["vasLitres"], cfg.speaker.vasLitres);
    readNumber(spk["minFrequency"], cfg.speaker.minFrequencyHz);
    readNumber(spk["maxFrequency"], cfg.speaker.maxFrequencyHz);
    readNumber(spk["recommendedHighPass"], cfg.speaker.recommendedHighPassHz);
    readNumber(spk["gainCorrectionDb"], cfg.speaker.gainCorrectionDb);
    readNumber(spk["powerLimit"], cfg.speaker.powerLimitW);

    JsonObjectConst aco = root["acoustic"];
    readEnum(aco["coupling"], cfg.acoustic.coupling);
    readNumber(aco["rearChamberVolumeMl"], cfg.acoustic.rearChamberVolumeMl);
    readNumber(aco["frontChamberVolumeMl"], cfg.acoustic.frontChamberVolumeMl);
    readNumber(aco["frontChamberDepthMm"], cfg.acoustic.frontChamberDepthMm);
    readHornStage(aco["stage1"], cfg.acoustic.stage1);
    readNumber(aco["intermediateDiameterMm"], cfg.acoustic.intermediateDiameterMm);
    readNumber(aco["intermediateLengthMm"], cfg.acoustic.intermediateLengthMm);
    readHornStage(aco["stage2"], cfg.acoustic.stage2);
    readNumber(aco["leadpipeDiameterMm"], cfg.acoustic.leadpipeDiameterMm);
    readNumber(aco["measuredHighPassHz"], cfg.acoustic.measuredHighPassHz);
    readNumber(aco["eqGainDb"], cfg.acoustic.eqGainDb);
    // v2 files: one chamber volume, one outlet, one hand-entered high pass.
    // Map them onto the new geometry rather than dropping them silently.
    readNumber(aco["chamberVolumeMl"], cfg.acoustic.rearChamberVolumeMl);
    readNumber(aco["outletDiameterMm"], cfg.acoustic.leadpipeDiameterMm);
    if (!aco["outletLengthMm"].isNull()) {
        readNumber(aco["outletLengthMm"], cfg.acoustic.stage2.lengthMm);
    }
    readNumber(aco["highPassHz"], cfg.acoustic.measuredHighPassHz);

    JsonObjectConst valves = root["valves"];
    readNumber(valves["count"], cfg.valves.count);
    readEnum(valves["mode"], cfg.valves.mode);
    readNumber(valves["pca9685Address"], cfg.valves.pca9685Address);
    readNumber(valves["pca9685OePin"], cfg.valves.pca9685OePin);
    readNumber(valves["servoFrequencyHz"], cfg.valves.servoFrequencyHz);
    readNumber(valves["solenoidPwmFrequencyHz"], cfg.valves.solenoidPwmFrequencyHz);
    readI2c(valves["pca9685I2c"], cfg.valves.pca9685I2c);

    JsonObjectConst sync = valves["sync"];
    readBool(sync["enabled"], cfg.valves.sync.enabled);
    readBool(sync["onlyWhenFingeringChanges"], cfg.valves.sync.onlyWhenFingeringChanges);
    readNumber(sync["trimMs"], cfg.valves.sync.trimMs);
    readNumber(sync["maxDelayMs"], cfg.valves.sync.maxDelayMs);
    if (cfg.valves.count > kMaxValves) cfg.valves.count = kMaxValves;

    JsonArrayConst items = valves["items"];
    for (uint8_t i = 0; i < kMaxValves && i < items.size(); ++i) {
        JsonObjectConst o = items[i];
        ValveConfig& v = cfg.valves.items[i];
        readEnum(o["type"], v.type);
        readEnum(o["driver"], v.driver);
        readNumber(o["gpio"], v.gpio);
        readNumber(o["channel"], v.channel);
        readNumber(o["pressedAngle"], v.pressedAngle);
        readNumber(o["releasedAngle"], v.releasedAngle);
        readNumber(o["speed"], v.speedDegPerSec);
        readNumber(o["acceleration"], v.accelDegPerSec2);
        readBool(o["invert"], v.invert);
        readBool(o["detachAfterMove"], v.detachAfterMove);
        readNumber(o["detachDelayMs"], v.detachDelayMs);
        readNumber(o["minPulseUs"], v.minPulseUs);
        readNumber(o["maxPulseUs"], v.maxPulseUs);
        readBool(o["activeHigh"], v.activeHigh);
        readNumber(o["pullInPwm"], v.pullInPwm);
        readNumber(o["pullInMs"], v.pullInMs);
        readNumber(o["holdPwm"], v.holdPwm);
        readNumber(o["maxOnMs"], v.maxOnMs);
        readNumber(o["cooldownMs"], v.cooldownMs);
        readNumber(o["maxDutyPercent"], v.maxDutyPercent);
        readNumber(o["cc"], v.ccNumber);
        readNumber(o["measuredSettleMs"], v.measuredSettleMs);
    }

    JsonObjectConst inst = root["instrument"];
    readEnum(inst["type"], cfg.instrument.type);
    readEnum(inst["pitchMode"], cfg.instrument.pitchMode);
    readNumber(inst["customTranspose"], cfg.instrument.customTransposeSemitones);
    readEnum(inst["notePriority"], cfg.instrument.notePriority);
    readBool(inst["legato"], cfg.instrument.legato);
    readBool(inst["retrigger"], cfg.instrument.retrigger);
    readNumber(inst["portamentoMs"], cfg.instrument.portamentoMs);
    readNumber(inst["noteMin"], cfg.instrument.noteMin);
    readNumber(inst["noteMax"], cfg.instrument.noteMax);

    JsonObjectConst midi = root["midi"];
    readBool(midi["usb"]["in"], cfg.midi.usb.inEnabled);
    readBool(midi["usb"]["out"], cfg.midi.usb.outEnabled);
    readBool(midi["ble"]["in"], cfg.midi.ble.inEnabled);
    readBool(midi["ble"]["out"], cfg.midi.ble.outEnabled);
    readString(midi["ble"]["name"], cfg.midi.ble.deviceName, sizeof(cfg.midi.ble.deviceName));
    readBool(midi["rtp"]["in"], cfg.midi.rtp.inEnabled);
    readBool(midi["rtp"]["out"], cfg.midi.rtp.outEnabled);
    readNumber(midi["rtp"]["controlPort"], cfg.midi.rtp.controlPort);
    readString(midi["rtp"]["sessionName"], cfg.midi.rtp.sessionName,
               sizeof(cfg.midi.rtp.sessionName));
    readBool(midi["din"]["in"], cfg.midi.din.inEnabled);
    readBool(midi["din"]["out"], cfg.midi.din.outEnabled);
    readBool(midi["din"]["thru"], cfg.midi.din.thruEnabled);
    readNumber(midi["din"]["rxGpio"], cfg.midi.din.rxGpio);
    readNumber(midi["din"]["txGpio"], cfg.midi.din.txGpio);
    readNumber(midi["din"]["uart"], cfg.midi.din.uartNum);
    readBool(midi["web"]["in"], cfg.midi.web.inEnabled);
    readBool(midi["web"]["monitor"], cfg.midi.web.monitorEnabled);
    readNumber(midi["channelMask"], cfg.midi.globalChannelMask);
    readNumber(midi["outputChannel"], cfg.midi.outputChannel);
    readBool(midi["suppressLoops"], cfg.midi.suppressLoops);

    JsonArrayConst routes = midi["routes"];
    cfg.midi.routeCount = 0;
    for (JsonObjectConst o : routes) {
        if (cfg.midi.routeCount >= kMaxRoutes) break;
        MidiRoute& r = cfg.midi.routes[cfg.midi.routeCount];
        r = MidiRoute();
        readEnum(o["source"], r.source);
        readEnum(o["destination"], r.destination);
        readBool(o["enabled"], r.enabled);
        readNumber(o["channelMask"], r.channelMask);
        readNumber(o["transpose"], r.transpose);
        readEnum(o["velocityCurve"], r.velocityCurve);
        readNumber(o["fixedVelocity"], r.fixedVelocity);
        readNumber(o["noteMin"], r.noteMin);
        readNumber(o["noteMax"], r.noteMax);
        if (r.source == MidiPort::NONE || r.destination == MidiPort::NONE) continue;
        ++cfg.midi.routeCount;
    }

    // Anything older than the current schema is upgraded in place before the
    // rest of the firmware ever sees it.
    migrateConfig(cfg);
    return true;
}

void preserveSecrets(InstrumentConfiguration& cfg, const InstrumentConfiguration& previous) {
    if (cfg.wifi.password[0] == '\0') {
        copyString(cfg.wifi.password, sizeof(cfg.wifi.password), previous.wifi.password);
    }
    if (cfg.wifi.apPassword[0] == '\0') {
        copyString(cfg.wifi.apPassword, sizeof(cfg.wifi.apPassword), previous.wifi.apPassword);
    }
}

size_t serializeConfig(const InstrumentConfiguration& cfg, char* out, size_t outSize,
                       SecretPolicy secrets) {
    JsonDocument doc;
    configToJson(cfg, doc.to<JsonObject>(), secrets);
    return serializeJson(doc, out, outSize);
}

bool deserializeConfig(const char* json, size_t length, InstrumentConfiguration& cfg) {
    if (!json || length == 0) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json, length) != DeserializationError::Ok) return false;
    return configFromJson(doc.as<JsonObjectConst>(), cfg);
}

}  // namespace ot
