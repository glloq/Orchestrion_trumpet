/* ===========================================================================
   api.js — REST client for the firmware, with a self-contained MOCK backend.

   When no device answers (the page opened from file://, or a development
   session with no board on the desk) every call transparently falls back to an
   in-memory mock, so the whole interface stays usable, testable and
   screenshot-able without hardware. The mock mirrors the firmware contract:
   same field names, same validation verdicts, same redaction of secrets.

   The badge in the top bar says which one is in force, so a screenshot can
   never be mistaken for a live device.
   =========================================================================== */
'use strict';

const API = (() => {
  let useMock = false;
  let probed = false;
  const listeners = [];

  function onModeChange(fn) { listeners.push(fn); }
  function setMock(value) {
    if (useMock === value) return;
    useMock = value;
    listeners.forEach((fn) => fn(useMock));
  }

  async function request(method, path, body) {
    if (useMock) return MOCK.handle(method, path, body);

    const init = { method, headers: {} };
    if (body !== undefined) {
      init.headers['Content-Type'] = 'application/json';
      init.body = typeof body === 'string' ? body : JSON.stringify(body);
    }

    let response;
    try {
      response = await fetch(path, init);
    } catch (err) {
      // Unreachable device: switch to the mock once, then stay there.
      if (!probed) {
        probed = true;
        setMock(true);
        return MOCK.handle(method, path, body);
      }
      throw new Error('The trumpet is not reachable (' + err.message + ')');
    }
    probed = true;

    const text = await response.text();
    let data = null;
    if (text) {
      try { data = JSON.parse(text); } catch (_) { data = { raw: text }; }
    }
    if (!response.ok) {
      const error = new Error((data && data.error) || ('HTTP ' + response.status));
      error.status = response.status;
      error.payload = data;
      throw error;
    }
    return data;
  }

  return {
    isMock: () => useMock,
    forceMock: () => { probed = true; setMock(true); },
    onModeChange,

    status:         () => request('GET', '/api/status'),
    diagnostics:    () => request('GET', '/api/diagnostics'),
    hardware:       () => request('GET', '/api/hardware'),
    getConfig:      () => request('GET', '/api/config'),
    putConfig:      (cfg) => request('PUT', '/api/config', cfg),
    validate:       (cfg) => request('POST', '/api/config/validate', cfg),
    exportConfig:   () => request('GET', '/api/config/export'),
    importConfig:   (json) => request('POST', '/api/config/import', json),
    applyPreset:    (key) => request('POST', '/api/config/preset', { preset: key }),
    panic:          () => request('POST', '/api/panic', {}),
    releasePanic:   () => request('POST', '/api/panic/release', {}),
    audioTest:      (opts) => request('POST', '/api/audio/test', opts),
    audioMute:      (muted) => request('POST', '/api/audio/mute', { muted }),
    audioVolume:    (volume) => request('POST', '/api/audio/volume', { volume }),
    // The live path. Preview changes the sound now and nothing else; revert
    // puts back what is stored; commit makes the live sound the stored one.
    audioPreview:   (voicing) => request('POST', '/api/audio/preview', voicing),
    audioRevert:    () => request('POST', '/api/audio/revert', {}),
    audioCommit:    () => request('POST', '/api/audio/commit', {}),
    voicings:       () => request('GET', '/api/voicings'),
    voicingSave:    (name) => request('POST', '/api/voicings/save', { name }),
    voicingLoad:    (name) => request('POST', '/api/voicings/load', { name }),
    voicingDelete:  (name) => request('POST', '/api/voicings/delete', { name }),
    valveTest:      (valve, durationMs) => request('POST', '/api/valve/test', { valve, durationMs }),
    valveMode:      (mode) => request('POST', '/api/valve/mode', { mode }),
    valveManual:    (valve, pressed) => request('POST', '/api/valve/manual', { valve, pressed }),
    valveCalibrate: (valve, angle) => request('POST', '/api/valve/calibrate', { valve, angle }),
    wifiScan:       (refresh) => request('GET', '/api/wifi/scan' + (refresh ? '?refresh=1' : '')),
    wifiHotspot:    () => request('POST', '/api/wifi/hotspot', {}),
    wifiCredentials:(body) => request('POST', '/api/wifi/credentials', body),
    midiStatus:     () => request('GET', '/api/midi/status'),
    midiMonitor:    () => request('GET', '/api/midi/monitor'),
    midiMonitorSet: (body) => request('POST', '/api/midi/monitor', body),
    midiMonitorClear: () => request('POST', '/api/midi/monitor/clear', {}),
    fingering:      () => request('GET', '/api/fingering'),
    putFingering:   (notes) => request('PUT', '/api/fingering', { notes }),
    resetFingering: () => request('POST', '/api/fingering/reset', {}),
    reboot:         () => request('POST', '/api/system/reboot', {}),
    factoryReset:   () => request('POST', '/api/system/factory-reset', {}),

    uploadFirmware(file, onProgress) {
      if (useMock) {
        return new Promise((resolve) => {
          let done = 0;
          const timer = setInterval(() => {
            done += 0.12;
            if (onProgress) onProgress(Math.min(done, 1));
            if (done >= 1) { clearInterval(timer); resolve({ ok: true }); }
          }, 120);
        });
      }
      return new Promise((resolve, reject) => {
        const form = new FormData();
        form.append('firmware', file, file.name);
        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/ota');
        xhr.upload.onprogress = (e) => {
          if (e.lengthComputable && onProgress) onProgress(e.loaded / e.total);
        };
        xhr.onload = () => {
          let payload = null;
          try { payload = JSON.parse(xhr.responseText); } catch (_) { /* ignore */ }
          if (xhr.status >= 200 && xhr.status < 300) resolve(payload);
          else reject(new Error((payload && payload.error) || ('HTTP ' + xhr.status)));
        };
        xhr.onerror = () => reject(new Error('the upload was interrupted'));
        xhr.send(form);
      });
    }
  };
})();

/* ---------------------------------------------------------------------------
   MOCK — a plausible ESP32-S3 running the STANDARD preset.
   It reproduces the firmware's own answers, including the validation verdicts
   and the redaction of Wi-Fi passwords.
   --------------------------------------------------------------------------- */
const MOCK = (() => {

  // Everything that changes the sound and nothing that changes safety — the
  // same split the firmware makes.
  function defaultVoicing() {
    return {
      name: 'Natural', engine: 'ADDITIVE', pitchBendRange: 2,
      darkTilt: 2.6, brightTilt: 0.6, hybridMix: 0.6,
      velocityFloor: 0.25, breathToVolume: 1, expressionToVolume: 1,
      aftertouchToBrightness: 0.25, aftertouchToVolume: 0, outputTrimDb: 0,
      envelope: { attackMs: 12, decayMs: 90, sustain: 0.82, releaseMs: 70,
                  attackNoise: 0.12, breathNoise: 0.03 },
      vibrato: { source: 'CC1', frequencyHz: 5.5, depthCents: 22, delayMs: 250, fadeInMs: 350 },
      additive: { harmonicCount: 10,
                  harmonicGain: [1, .72, .55, .42, .33, .25, .19, .14, .1, .07,
                                 .05, .04, .03, .02, .015, .01],
                  velocityBrightness: 0.85, breathBrightness: 0.7,
                  expressionBrightness: 0.35, pitchBrightness: 0.3 },
      exciter: { drive: 1.6, asymmetry: 0.25, pressure: 0.7, pressureToDrive: 0.9,
                 noiseAmount: 0.05, transientMs: 18 },
      eq: [{ frequency: 220, gainDb: 0, q: 0.8, enabled: false },
           { frequency: 480, gainDb: 0, q: 0.9, enabled: false },
           { frequency: 900, gainDb: 0, q: 0.9, enabled: false },
           { frequency: 1800, gainDb: 0, q: 0.9, enabled: false },
           { frequency: 3200, gainDb: 0, q: 0.9, enabled: false },
           { frequency: 6400, gainDb: 0, q: 0.8, enabled: false }],
      registerCurve: [{ note: 52, gainDb: 0, brightness: 0 },
                      { note: 60, gainDb: 0, brightness: 0 },
                      { note: 67, gainDb: 0, brightness: 0 },
                      { note: 72, gainDb: 0, brightness: 0 },
                      { note: 86, gainDb: 0, brightness: 0 }]
    };
  }

  function defaultConfig() {
    const valve = (gpio) => ({
      type: 'SERVO', driver: 'ESP32_PWM', gpio, channel: 0,
      pressedAngle: 88, releasedAngle: 40, speed: 900, acceleration: 6000,
      invert: false, detachAfterMove: true, detachDelayMs: 220,
      minPulseUs: 500, maxPulseUs: 2400,
      activeHigh: true, pullInPwm: 100, pullInMs: 50, holdPwm: 35,
      maxOnMs: 5000, cooldownMs: 3000, maxDutyPercent: 60, cc: 20,
      measuredSettleMs: 0
    });
    const items = [valve(15), valve(16), valve(4), valve(2)];
    items[2] = Object.assign(valve(4), { type: 'SOLENOID' });
    items[3].type = 'DISABLED';

    const routes = [];
    ['USB', 'BLE', 'RTP', 'DIN', 'WEB'].forEach((src) => {
      ['SOUND_ENGINE', 'VALVE_ENGINE'].forEach((dst) => {
        routes.push({
          source: src, destination: dst, enabled: true, channelMask: 65535,
          transpose: 0, velocityCurve: 'LINEAR', fixedVelocity: 100,
          noteMin: 0, noteMax: 127
        });
      });
    });

    return {
      schemaVersion: 4,
      board: { type: 'ESP32_S3' },
      system: { deviceName: 'Orchestrion Trumpet', preset: 'STANDARD',
                wizardCompleted: true, safeModeForced: false },
      wifi: { mode: 'AP', ssid: '', apSsid: '', passwordSet: false, apPasswordSet: false,
              hostname: 'midi-trumpet', apChannel: 6, captivePortal: true },
      audio: {
        backend: 'PCM5102A', sampleRate: 48000, bitDepth: 24, blockSize: 128, dmaBuffers: 6,
        masterVolume: 0.75, codecAddress: 16,
        sdModePin: -1, internalDacChannel: 1, highPassHz: 160, startupMute: true,
        programChangeSelectsVoicing: false,
        i2s: { bclk: 5, ws: 6, dout: 7, din: -1, mclk: -1 },
        i2c: { sda: 8, scl: 9, frequency: 400000 },
        limiter: { enabled: true, thresholdDb: -3, attackMs: 1.5, releaseMs: 90, hardCeiling: 0.985 }
      },
      voicing: defaultVoicing(),
      voicings: [],
      amplifier: { type: 'TPA3118D2', maxPower: 25, gainDb: 26, speakerImpedance: 8, volumeLimit: 1 },
      speaker: { profile: 'VISATON_FRS8M', name: 'Visaton FRS 8 M', impedance: 8, powerRms: 30,
                 powerMax: 50, fsHz: 125, vasLitres: 0,
                 minFrequency: 100, maxFrequency: 20000, recommendedHighPass: 160,
                 gainCorrectionDb: 0, powerLimit: 20 },
      acoustic: { coupling: 'SEALED_CHAMBER', rearChamberVolumeMl: 120,
                  frontChamberVolumeMl: 35, frontChamberDepthMm: 8,
                  stage1: { inletDiameterMm: 60, outletDiameterMm: 28, lengthMm: 58 },
                  intermediateDiameterMm: 28, intermediateLengthMm: 20,
                  stage2: { inletDiameterMm: 28, outletDiameterMm: 12, lengthMm: 40 },
                  leadpipeDiameterMm: 11, measuredHighPassHz: 0, eqGainDb: 2 },
      valves: { count: 3, mode: 'AUTO', items,
                pca9685I2c: { sda: 8, scl: 9, frequency: 400000 },
                pca9685Address: 64, pca9685OePin: -1,
                servoFrequencyHz: 50, solenoidPwmFrequencyHz: 20000,
                sync: { enabled: true, onlyWhenFingeringChanges: true,
                        trimMs: 0, maxDelayMs: 120 } },
      instrument: { type: 'BB_TRUMPET', pitchMode: 'CONCERT', customTranspose: 0,
                    notePriority: 'LAST', legato: true, retrigger: false, portamentoMs: 0,
                    noteMin: 52, noteMax: 86 },
      midi: {
        usb: { in: true, out: false }, ble: { in: true, out: false, name: 'GMB MIDI Trumpet' },
        rtp: { in: false, out: false, controlPort: 5004, sessionName: 'MIDI Trumpet' },
        din: { in: true, out: true, thru: false, rxGpio: 18, txGpio: 17, uart: 2 },
        web: { in: true, monitor: true },
        channelMask: 65535, outputChannel: 1, suppressLoops: true, routes
      },
      electrical: {}
    };
  }

  const state = {
    config: defaultConfig(),
    mode: 'RUNNING',
    valveMode: 'AUTO',
    valves: [
      { pressed: true, type: 'SERVO', angle: 88, duty: 0, fault: false },
      { pressed: true, type: 'SERVO', angle: 88, duty: 0, fault: false },
      { pressed: false, type: 'SOLENOID', angle: 0, duty: 0, fault: false }
    ],
    liveVoicing: defaultVoicing(),
    note: 64, velocity: 96, frequency: 329.6, muted: false, volume: 0.75,
    scanned: false,
    monitor: {
      paused: false,
      filter: { sources: ['USB', 'BLE', 'RTP', 'DIN', 'WEB'],
                notes: true, controllers: true, other: true },
      items: [
        { ts: 3725100, src: 'USB', type: 'Note On', ch: 1, d1: 64, d2: 96 },
        { ts: 3725050, src: 'USB', type: 'Control Change', ch: 1, d1: 2, d2: 74 },
        { ts: 3724880, src: 'DIN', type: 'Note Off', ch: 1, d1: 60, d2: 0 },
        { ts: 3724610, src: 'DIN', type: 'Note On', ch: 1, d1: 60, d2: 104 },
        { ts: 3724400, src: 'USB', type: 'Pitch Bend', ch: 1, d1: 0, d2: 64 },
        { ts: 3724100, src: 'BLE', type: 'Note On', ch: 1, d1: 67, d2: 88 },
        { ts: 3723960, src: 'WEB', type: 'Control Change', ch: 1, d1: 11, d2: 127 }
      ]
    }
  };

  // The mock applies the filters itself, exactly as MidiMonitor does on the
  // device, so the controls in the UI are demonstrably wired to something.
  const NOTE_TYPES = ['Note On', 'Note Off'];
  const CTRL_TYPES = ['Control Change', 'Pitch Bend', 'Channel Pressure', 'Poly Pressure'];
  function monitorItems() {
    const f = state.monitor.filter;
    return state.monitor.items.filter((r) => {
      if (f.sources.indexOf(r.src) < 0) return false;
      if (NOTE_TYPES.indexOf(r.type) >= 0) return f.notes;
      if (CTRL_TYPES.indexOf(r.type) >= 0) return f.controllers;
      return f.other;
    });
  }

  const HARDWARE = {
    ok: true,
    board: { type: 'ESP32_S3', chip: 'ESP32-S3', cores: 2, gpioMax: 48,
             flash: 8388608, psram: 8388608, hasNativeUsb: true, hasInternalDac: false,
             hasBle: true, hasWifi: true,
             reservedPins: [26, 27, 28, 29, 30, 31, 32], inputOnlyPins: [],
             warnPins: [0, 3, 45, 46, 19, 20, 33, 34, 35, 36, 37, 43, 44] },
    audioBackends: [
      { key: 'NONE', title: 'No audio output', summary: 'Valves only. Also used by safe mode.',
        maturity: 'STABLE', available: true, needsI2c: false, needsSdPin: false,
        supportsCapture: false, maxBitDepth: 16 },
      { key: 'ESP32_INTERNAL_DAC', title: 'ESP32 internal DAC',
        summary: '8 bit, prototype only. Classic ESP32 (GPIO25/26).', maturity: 'PROTOTYPE',
        available: false, needsI2c: false, needsSdPin: false, supportsCapture: false, maxBitDepth: 8 },
      { key: 'MAX98357A', title: 'MAX98357A',
        summary: 'I2S class-D amplifier, speaker directly attached. 16 bit.',
        maturity: 'STABLE', available: true, needsI2c: false, needsSdPin: true,
        supportsCapture: false, maxBitDepth: 16 },
      { key: 'PCM5102A', title: 'PCM5102A',
        summary: 'Reference DAC. Line level into an external amplifier.', maturity: 'STABLE',
        available: true, needsI2c: false, needsSdPin: true, supportsCapture: false, maxBitDepth: 32 },
      { key: 'ES8388', title: 'ES8388 codec',
        summary: 'DAC + ADC. Microphone input reserved for acoustic calibration.',
        maturity: 'EXPERIMENTAL', available: true, needsI2c: true, needsSdPin: false,
        supportsCapture: true, maxBitDepth: 32 },
      { key: 'WM8960', title: 'WM8960 codec', summary: 'DAC + ADC into an external amplifier.',
        maturity: 'EXPERIMENTAL', available: true, needsI2c: true, needsSdPin: false,
        supportsCapture: true, maxBitDepth: 32 },
      { key: 'TAS5760M', title: 'TAS5760M',
        summary: 'I2S class-D amplifier, target of the integrated PCB.',
        maturity: 'EXPERIMENTAL', available: true, needsI2c: true, needsSdPin: true,
        supportsCapture: false, maxBitDepth: 32 }
    ],
    amplifiers: [
      { key: 'NONE', maxPower: 0, gainDb: 0, impedance: 8 },
      { key: 'MAX98357_INTERNAL', maxPower: 3.2, gainDb: 9, impedance: 4 },
      { key: 'TPA3118D2', maxPower: 25, gainDb: 26, impedance: 8 },
      { key: 'TAS5760_INTERNAL', maxPower: 20, gainDb: 22, impedance: 8 },
      { key: 'CUSTOM', maxPower: 25, gainDb: 26, impedance: 8 }
    ],
    speakers: [
      { key: 'VISATON_FRS5_XTS', name: 'Visaton FRS 5 XTS', impedance: 8, powerRms: 5,
        powerMax: 8, minFrequency: 120, recommendedHighPass: 200, powerLimit: 4, fsHz: 0 },
      { key: 'DAYTON_CE70PR4', name: 'Dayton CE70PR-4', impedance: 4, powerRms: 20,
        powerMax: 30, minFrequency: 85, recommendedHighPass: 170, powerLimit: 8, fsHz: 0 },
      { key: 'VISATON_FRS8M', name: 'Visaton FRS 8 M', impedance: 8, powerRms: 30,
        powerMax: 50, minFrequency: 100, recommendedHighPass: 160, powerLimit: 20, fsHz: 125 },
      { key: 'MONACOR_SPX30M', name: 'Monacor SPX-30M', impedance: 8, powerRms: 20,
        powerMax: 40, minFrequency: 100, recommendedHighPass: 150, powerLimit: 15, fsHz: 100 },
      { key: 'CUSTOM', name: 'Custom speaker', impedance: 8, powerRms: 10,
        powerMax: 10, minFrequency: 150, recommendedHighPass: 200, powerLimit: 6, fsHz: 0 }
    ],
    presets: [
      { key: 'LOW_COST', title: 'Low cost', summary: 'ESP32 → MAX98357A → Dayton CE70P-4.',
        stars: 6, recommended: false, available: true },
      { key: 'COMPACT', title: 'Compact', summary: 'ESP32 → MAX98357A → Visaton FRS 5 XTS.',
        stars: 6, recommended: false, available: true },
      { key: 'STANDARD', title: 'Standard',
        summary: 'PCM5102A → TPA3118D2 → Visaton FRS 8 M → sealed chamber → trumpet.',
        stars: 9, recommended: true, available: true },
      { key: 'QUALITY', title: 'Quality', summary: 'PCM5102A → TPA3118D2 → Monacor SPX-30M.',
        stars: 10, recommended: false, available: true },
      { key: 'FEEDBACK', title: 'Feedback (measurement)',
        summary: 'ES8388 codec with microphone input, prepared for calibration.',
        stars: 9, recommended: false, available: true },
      { key: 'INTEGRATED', title: 'Integrated PCB', summary: 'ESP32 → TAS5760M → speaker.',
        stars: 9, recommended: false, available: true },
      { key: 'CUSTOM', title: 'Custom', summary: 'Configure every element by hand.',
        stars: 0, recommended: false, available: true }
    ],
    midi: { usbAvailable: true, bleAvailable: true, rtpAvailable: true, dinAvailable: true }
  };

  // Mirrors ConfigValidator: duplicated GPIO, reserved pins, board capability,
  // speaker/amplifier power. Enough to exercise the UI honestly.
  function validate(cfg) {
    const issues = [];
    const claimed = new Map();
    const claim = (pin, owner, field) => {
      if (pin === undefined || pin === null || pin < 0) return;
      if (HARDWARE.board.reservedPins.indexOf(pin) !== -1) {
        issues.push({ severity: 'ERROR', field,
                      message: 'GPIO ' + pin + ' is wired to the flash/PSRAM and cannot be used' });
        return;
      }
      if (claimed.has(pin)) {
        issues.push({ severity: 'ERROR', field,
                      message: 'GPIO ' + pin + ' is already used by ' + claimed.get(pin) });
        return;
      }
      claimed.set(pin, owner);
      if (HARDWARE.board.warnPins.indexOf(pin) !== -1) {
        issues.push({ severity: 'WARNING', field,
                      message: 'GPIO ' + pin + ' is a strapping/USB pin: check the boot level' });
      }
    };

    if (cfg.audio.backend !== 'NONE' && cfg.audio.backend !== 'ESP32_INTERNAL_DAC') {
      claim(cfg.audio.i2s.bclk, 'I2S BCLK', 'audio.i2s.bclk');
      claim(cfg.audio.i2s.ws, 'I2S WS', 'audio.i2s.ws');
      claim(cfg.audio.i2s.dout, 'I2S DOUT', 'audio.i2s.dout');
      claim(cfg.audio.sdModePin, 'the DAC mute pin', 'audio.sdModePin');
    }
    const backend = HARDWARE.audioBackends.find((b) => b.key === cfg.audio.backend);
    if (backend && !backend.available) {
      issues.push({ severity: 'ERROR', field: 'audio.backend',
                    message: cfg.audio.backend + ' is not available on this board' });
    }
    if (cfg.midi.din.in) claim(cfg.midi.din.rxGpio, 'DIN MIDI IN', 'midi.din.rxGpio');
    if (cfg.midi.din.out || cfg.midi.din.thru) claim(cfg.midi.din.txGpio, 'DIN MIDI OUT', 'midi.din.txGpio');
    if ((cfg.midi.usb.in || cfg.midi.usb.out) && !HARDWARE.board.hasNativeUsb) {
      issues.push({ severity: 'ERROR', field: 'midi.usb',
                    message: 'this board has no native USB: class compliant USB-MIDI is unavailable' });
    }

    cfg.valves.items.slice(0, cfg.valves.count).forEach((v, i) => {
      const field = 'valves.' + (i + 1);
      if (v.type === 'SERVO' && v.driver === 'ESP32_PWM') claim(v.gpio, 'servo ' + (i + 1), field);
      if (v.type === 'SOLENOID') {
        claim(v.gpio, 'solenoid ' + (i + 1), field);
        if (!v.maxOnMs) {
          issues.push({ severity: 'ERROR', field,
                        message: 'valve ' + (i + 1) + ': a solenoid must have a maximum ON time' });
        }
      }
    });

    if (cfg.speaker.powerLimit > cfg.speaker.powerRms) {
      issues.push({ severity: 'ERROR', field: 'speaker.powerLimit',
                    message: 'the protection limit is above the speaker RMS rating' });
    }
    if (cfg.amplifier.maxPower > cfg.speaker.powerRms) {
      issues.push({ severity: 'WARNING', field: 'amplifier.maxPower',
                    message: 'the amplifier can deliver ' + cfg.amplifier.maxPower + ' W into a '
                           + cfg.speaker.powerRms + ' W speaker: the limiter will cap it' });
    }
    if (cfg.speaker.powerMax > 0 && cfg.speaker.powerRms > cfg.speaker.powerMax) {
      issues.push({ severity: 'ERROR', field: 'speaker.powerRms',
                    message: 'the continuous rating is above the maximum rating' });
    }
    const scale = peakScale(cfg);
    if (scale < 0.9) {
      issues.push({ severity: 'INFO', field: 'speaker.powerLimit',
                    message: 'the protection stage will keep the output below '
                           + Math.round(scale * 100) + '% of full scale' });
    }

    // Acoustics: same rules as ConfigValidator, computed from the geometry.
    const m = acousticModel(cfg);
    if (m) {
      if (m.compressionRatio < 2 || m.compressionRatio > 12) {
        issues.push({ severity: 'WARNING', field: 'acoustic.leadpipeDiameterMm',
                      message: 'compression ratio ' + m.compressionRatio.toFixed(1)
                             + ':1 — outside the 2:1..12:1 range a cone works in' });
      }
      if (m.frontChamberCornerHz > 0 && m.frontChamberCornerHz < 4000) {
        issues.push({ severity: 'WARNING', field: 'acoustic.frontChamberVolumeMl',
                      message: 'the front chamber starts loading the throat at '
                             + Math.round(m.frontChamberCornerHz) + ' Hz' });
      }
      if (m.highPassSource !== 'MEASURED') {
        issues.push({ severity: 'INFO', field: 'acoustic.measuredHighPassHz',
                      message: 'the ' + Math.round(m.highPassHz) + ' Hz high pass is '
                             + (m.highPassSource === 'DERIVED' ? 'derived from the geometry'
                                                              : "the driver's own recommendation")
                             + ', not measured — confirm it at the bench' });
      }
    }
    return issues;
  }

  // Mirrors src/audio/AcousticModel.cpp so the offline UI shows the same
  // numbers the firmware computes. Geometry only — nothing is invented here
  // either: with no fs/Vas the sealed resonance stays unknown.
  function acousticModel(cfg) {
    const a = cfg.acoustic;
    if (!a || a.coupling === 'OPEN' || a.coupling === 'OPEN_AIR') return null;
    const area = (d) => (d > 0 ? Math.PI * (d / 2) * (d / 2) : 0);
    const c = 343;

    const coneArea = area(a.stage1.inletDiameterMm);
    const throatArea = area(a.leadpipeDiameterMm);
    const path = a.stage1.lengthMm + a.intermediateLengthMm + a.stage2.lengthMm;
    const halfAngle = (st) => st.lengthMm > 0
      ? Math.atan(((st.inletDiameterMm - st.outletDiameterMm) / 2) / st.lengthMm) * 180 / Math.PI
      : 90;

    const volumeM3 = a.frontChamberVolumeMl * 1e-6;
    const areaM2 = throatArea * 1e-6;
    const lengthM = (path + 0.85 * a.leadpipeDiameterMm / 2) * 1e-3;
    const frontChamberCornerHz = (volumeM3 > 0 && areaM2 > 0 && lengthM > 0)
      ? (c / (2 * Math.PI)) * Math.sqrt(areaM2 / (volumeM3 * lengthM)) : 0;

    const vb = a.rearChamberVolumeMl / 1000;
    const known = cfg.speaker.fsHz > 0 && cfg.speaker.vasLitres > 0 && vb > 0;
    const sealedResonanceHz = known
      ? cfg.speaker.fsHz * Math.sqrt(1 + cfg.speaker.vasLitres / vb) : 0;

    let highPassHz, highPassSource;
    if (a.measuredHighPassHz > 0) {
      highPassHz = a.measuredHighPassHz;
      highPassSource = 'MEASURED';
    } else if (known) {
      highPassHz = sealedResonanceHz;
      highPassSource = 'DERIVED';
    } else {
      highPassHz = cfg.speaker.recommendedHighPass;
      highPassSource = 'SPEAKER_PROFILE';
    }

    return {
      coneAreaMm2: coneArea, throatAreaMm2: throatArea,
      compressionRatio: throatArea > 0 ? coneArea / throatArea : 0,
      stage1HalfAngleDeg: halfAngle(a.stage1), stage2HalfAngleDeg: halfAngle(a.stage2),
      totalPathLengthMm: path, frontChamberCornerHz,
      sealedResonanceHz, sealedResonanceKnown: known,
      highPassHz, highPassSource
    };
  }

  // Mirrors ConfigValidator::sanitiseVoicing so the offline UI clamps exactly
  // where the firmware clamps.
  const CLAMPS = {
    'envelope.attackMs': [0.5, 500], 'envelope.decayMs': [1, 2000],
    'envelope.sustain': [0, 1], 'envelope.releaseMs': [1, 3000],
    'envelope.attackNoise': [0, 1], 'envelope.breathNoise': [0, 0.5],
    'vibrato.frequencyHz': [0.1, 20], 'vibrato.depthCents': [0, 200],
    'vibrato.delayMs': [0, 5000], 'vibrato.fadeInMs': [1, 5000],
    'additive.velocityBrightness': [0, 1], 'additive.breathBrightness': [0, 1],
    'additive.expressionBrightness': [0, 1], 'additive.pitchBrightness': [0, 1],
    'exciter.drive': [0.1, 12], 'exciter.asymmetry': [-1, 1], 'exciter.pressure': [0, 1],
    'exciter.pressureToDrive': [0, 4], 'exciter.noiseAmount': [0, 1],
    'exciter.transientMs': [0.5, 500],
    darkTilt: [0, 6], brightTilt: [0, 6], hybridMix: [0, 1], velocityFloor: [0, 1],
    breathToVolume: [0, 1], expressionToVolume: [0, 1],
    aftertouchToBrightness: [0, 1], aftertouchToVolume: [0, 1], outputTrimDb: [-24, 12]
  };

  function sanitiseVoicing(v) {
    const clamp = (x, lo, hi) => (typeof x !== 'number' || !isFinite(x)) ? lo
                                 : Math.min(hi, Math.max(lo, x));
    for (const [path, [lo, hi]] of Object.entries(CLAMPS)) {
      const parts = path.split('.');
      const host = parts.length === 1 ? v : v[parts[0]];
      const key = parts[parts.length - 1];
      if (host && host[key] !== undefined) host[key] = clamp(host[key], lo, hi);
    }
    v.additive.harmonicCount = Math.min(16, Math.max(1, v.additive.harmonicCount | 0));
    v.additive.harmonicGain = v.additive.harmonicGain.map((g) => clamp(g, 0, 2));
    v.pitchBendRange = [1, 2, 3, 12].indexOf(v.pitchBendRange) >= 0 ? v.pitchBendRange : 2;
    v.eq.forEach((b) => {
      b.frequency = clamp(b.frequency, 20, 20000);
      b.gainDb = clamp(b.gainDb, -18, 18);
      b.q = clamp(b.q, 0.1, 10);
    });
    v.registerCurve.forEach((pt) => {
      pt.note = Math.min(127, Math.max(0, pt.note | 0));
      pt.gainDb = clamp(pt.gainDb, -18, 12);
      pt.brightness = clamp(pt.brightness, -1, 1);
    });
    v.registerCurve.sort((a, b) => a.note - b.note);
    return v;
  }

  function peakScale(cfg) {
    const z = cfg.speaker.impedance || 8;
    const limit = cfg.speaker.powerLimit || cfg.speaker.powerRms * 0.5 || 1;
    const amp = cfg.amplifier.maxPower;
    if (amp <= 0.05) return Math.min(1, cfg.amplifier.volumeLimit || 1);
    const scale = Math.sqrt(limit * z) / Math.sqrt(amp * z);
    return Math.max(0.02, Math.min(1, scale * (cfg.amplifier.volumeLimit || 1)));
  }

  const PRESET_CHAINS = {
    LOW_COST: ['MAX98357A', 'MAX98357_INTERNAL', 'DAYTON_CE70PR4'],
    COMPACT: ['MAX98357A', 'MAX98357_INTERNAL', 'VISATON_FRS5_XTS'],
    STANDARD: ['PCM5102A', 'TPA3118D2', 'VISATON_FRS8M'],
    QUALITY: ['PCM5102A', 'TPA3118D2', 'MONACOR_SPX30M'],
    FEEDBACK: ['ES8388', 'TPA3118D2', 'VISATON_FRS8M'],
    INTEGRATED: ['TAS5760M', 'TAS5760_INTERNAL', 'VISATON_FRS8M'],
    CUSTOM: null
  };

  function applyPreset(key) {
    const chain = PRESET_CHAINS[key];
    if (chain === undefined) return false;
    state.config.system.preset = key;
    if (!chain) return true;
    const [backend, amp, speaker] = chain;
    state.config.audio.backend = backend;
    state.config.audio.bitDepth = backend === 'MAX98357A' ? 16 : 24;
    const a = HARDWARE.amplifiers.find((x) => x.key === amp);
    Object.assign(state.config.amplifier,
                  { type: amp, maxPower: a.maxPower, gainDb: a.gainDb, speakerImpedance: a.impedance });
    const s = HARDWARE.speakers.find((x) => x.key === speaker);
    Object.assign(state.config.speaker, {
      profile: speaker, name: s.name, impedance: s.impedance, powerRms: s.powerRms,
      powerMax: s.powerMax, fsHz: s.fsHz, vasLitres: 0,
      minFrequency: s.minFrequency, recommendedHighPass: s.recommendedHighPass,
      powerLimit: s.powerLimit
    });
    state.config.audio.highPassHz = s.recommendedHighPass;
    return true;
  }

  const NETWORKS = [
    { ssid: 'Atelier', rssi: -46, channel: 6, secured: true },
    { ssid: 'Atelier-5G', rssi: -58, channel: 36, secured: true },
    { ssid: 'Livebox-3A2F', rssi: -67, channel: 11, secured: true },
    { ssid: 'Invite', rssi: -74, channel: 1, secured: false },
    { ssid: 'FreeWifi_secure', rssi: -81, channel: 3, secured: true }
  ];

  function statusPayload() {
    return {
      ok: true, firmware: '1.0.0', mode: state.mode, uptime: 3725,
      deviceName: state.config.system.deviceName,
      wizardCompleted: state.config.system.wizardCompleted,
      preset: state.config.system.preset,
      audio: {
        backend: state.config.audio.backend, running: true, muted: state.muted,
        sampleRate: state.config.audio.sampleRate, bitDepth: state.config.audio.bitDepth,
        underruns: 0, cpu: 18.4, volume: state.volume, peakScale: peakScale(state.config),
        note: state.note, velocity: state.velocity, frequency: state.frequency
      },
      midi: { rxPerSecond: 6, txPerSecond: 0, usb: true, ble: false, din: true, rtp: false },
      valves: state.valves.map((v) => ({ pressed: v.pressed, type: v.type, fault: v.fault })),
      valveMode: state.valveMode,
      network: { ap: true, sta: false, ssid: 'MIDI-Trumpet-A1B2', ip: '192.168.4.1',
                 hostname: state.config.wifi.hostname, rssi: 0, clients: 1,
                 apSecured: state.config.wifi.apPasswordSet, apForced: false, mdns: false },
      otaAvailable: true, faults: 0
    };
  }

  function telemetry() {
    return {
      t: 'telemetry', mode: state.mode, uptime: 3725,
      audio: { running: true, muted: state.muted, underruns: 0, cpu: 18.4, peak: 0.42,
               gainReduction: 1, note: state.note, velocity: state.velocity,
               frequency: state.frequency, backend: state.config.audio.backend,
               sampleRate: state.config.audio.sampleRate, volume: state.volume },
      midi: { rx: 6, tx: 0, usb: true, ble: false, din: true, rtp: false, web: 1 },
      valves: state.valves.slice(0, state.config.valves.count),
      valveMode: state.valveMode, faults: 0
    };
  }

  function fingering() {
    const V1 = 1, V2 = 2, V3 = 4;
    const chart = [V1|V2|V3, 0, V2|V3, V1|V2, V1, V2, 0, V1|V2|V3, V1|V3, V2|V3, V1|V2, V1, V2, 0,
                   V2|V3, V1|V2, V1, V2, 0, V1|V2, V1, V2, 0, V1, V2, 0, V2|V3, V1|V2, V1, V2, 0];
    const notes = [];
    for (let n = 40; n <= 96; n++) {
      const inRange = n >= 54 && n <= 84;
      let idx = n - 54;
      while (idx < 0) idx += 12;
      while (idx >= chart.length) idx -= 12;
      const primary = chart[idx];
      notes.push({ written: n, primary,
                   alternate: primary === (V1 | V2) ? V3 : (primary === V3 ? V1 | V2 : -1),
                   inRange });
    }
    return { ok: true, transpose: 2, pitchMode: state.config.instrument.pitchMode,
             instrument: state.config.instrument.type,
             valveCount: state.config.valves.count, notes };
  }

  function issuesResponse(issues) {
    const ok = !issues.some((i) => i.severity === 'ERROR');
    return { ok, rebootRequired: ok, issues };
  }

  // Redacts exactly like the firmware: the UI must never see a password.
  function exportedConfig() {
    const copy = JSON.parse(JSON.stringify(state.config));
    delete copy.wifi.password;
    delete copy.wifi.apPassword;
    return copy;
  }

  function handle(method, path, body) {
    const parsed = typeof body === 'string' ? JSON.parse(body || '{}') : (body || {});
    const route = method + ' ' + path.split('?')[0];

    switch (route) {
      case 'GET /api/status': return Promise.resolve(statusPayload());
      case 'GET /api/hardware': return Promise.resolve(HARDWARE);
      case 'GET /api/config':
      case 'GET /api/config/export': return Promise.resolve(exportedConfig());
      case 'PUT /api/config':
      case 'POST /api/config': {
        const issues = validate(parsed);
        if (!issues.some((i) => i.severity === 'ERROR')) {
          state.config = JSON.parse(JSON.stringify(parsed));
        }
        return Promise.resolve(issuesResponse(issues));
      }
      case 'POST /api/config/validate':
        return Promise.resolve({ ok: true, truncated: false, issues: validate(parsed) });
      case 'POST /api/config/import': {
        const issues = validate(parsed);
        if (!issues.some((i) => i.severity === 'ERROR')) state.config = parsed;
        return Promise.resolve(issuesResponse(issues));
      }
      case 'POST /api/config/preset':
        applyPreset(parsed.preset);
        return Promise.resolve(issuesResponse(validate(state.config)));

      case 'GET /api/diagnostics':
        return Promise.resolve({
          ok: true, firmware: '1.0.0', board: 'ESP32-S3', cores: 2,
          freeHeap: 221400, minFreeHeap: 192800, psram: 8388608, freePsram: 8130560,
          flash: 8388608, sketchSize: 1854000, freeSketchSpace: 3342336, uptime: 3725,
          resetReason: 1, cpuLoad: 18.4, audioUnderruns: 0, audioBlocks: 1397812, attackDelayMs: 65,
          audioSampleRate: 48000, audioBackend: state.config.audio.backend,
          audioMaturity: 'STABLE', wifiRssi: 0, ble: false, midiRx: 4210, midiTx: 12,
          midiRxPerSecond: 6, midiTxPerSecond: 0, monitorOverflow: 0, otaAvailable: true,
          router: { droppedByFilter: 2, loopsSuppressed: 0, rtpRejectedSources: 0 },
          valves: state.valves.slice(0, state.config.valves.count).map((v) => ({
            type: v.type, pressed: v.pressed, angle: v.angle, duty: v.duty, fault: v.fault
          })),
          faults: 0
        });

      case 'GET /api/midi/status':
        return Promise.resolve({
          ok: true,
          ports: [
            { port: 'USB', in: true, out: false, connected: true, rx: 4180, tx: 0 },
            { port: 'BLE', in: true, out: false, connected: false, rx: 0, tx: 0 },
            { port: 'RTP', in: false, out: false, connected: false, rx: 0, tx: 0 },
            { port: 'DIN', in: true, out: true, connected: true, rx: 30, tx: 12 },
            { port: 'WEB', in: true, out: true, connected: true, rx: 0, tx: 0 }
          ],
          routes: state.config.midi.routes, rtpPeer: '',
          droppedByFilter: 2, loopsSuppressed: 0
        });
      case 'GET /api/midi/monitor':
        return Promise.resolve({ ok: true, paused: state.monitor.paused, overflow: 0,
                                 perSecond: state.monitor.paused ? 0 : 6,
                                 filter: state.monitor.filter, items: monitorItems() });
      case 'POST /api/midi/monitor': {
        const m = state.monitor;
        if (parsed.paused !== undefined) m.paused = !!parsed.paused;
        if (parsed.sources !== undefined) m.filter.sources = parsed.sources;
        for (const k of ['notes', 'controllers', 'other']) {
          if (parsed[k] !== undefined) m.filter[k] = !!parsed[k];
        }
        return Promise.resolve({ ok: true });
      }
      case 'POST /api/audio/preview': {
        // The mock applies the same clamps the firmware does, so a value the
        // device would refuse is visibly refused here too.
        const next = Object.assign(JSON.parse(JSON.stringify(state.liveVoicing)), parsed);
        sanitiseVoicing(next);
        state.liveVoicing = next;
        return Promise.resolve({ ok: true, voicing: next });
      }
      case 'POST /api/audio/revert':
        state.liveVoicing = JSON.parse(JSON.stringify(state.config.voicing));
        return Promise.resolve({ ok: true });
      case 'POST /api/audio/commit':
        state.config.voicing = JSON.parse(JSON.stringify(state.liveVoicing));
        return Promise.resolve({ ok: true });
      case 'GET /api/voicings':
        return Promise.resolve({ ok: true, capacity: 4,
                                 live: state.liveVoicing, saved: state.config.voicing,
                                 items: state.config.voicings });
      case 'POST /api/voicings/save': {
        if (!parsed.name) return Promise.reject(new Error('a voicing needs a name'));
        const entry = JSON.parse(JSON.stringify(state.liveVoicing));
        entry.name = parsed.name;
        const at = state.config.voicings.findIndex((v) => v.name === parsed.name);
        if (at >= 0) state.config.voicings[at] = entry;
        else if (state.config.voicings.length >= 4) {
          return Promise.reject(new Error('no free voicing slot: delete one first'));
        } else state.config.voicings.push(entry);
        state.config.voicing = JSON.parse(JSON.stringify(entry));
        state.liveVoicing = JSON.parse(JSON.stringify(entry));
        return Promise.resolve({ ok: true });
      }
      case 'POST /api/voicings/load': {
        const found = state.config.voicings.find((v) => v.name === parsed.name);
        if (!found) return Promise.reject(new Error('no voicing by that name'));
        state.liveVoicing = JSON.parse(JSON.stringify(found));
        return Promise.resolve({ ok: true });
      }
      case 'POST /api/voicings/delete': {
        const at = state.config.voicings.findIndex((v) => v.name === parsed.name);
        if (at < 0) return Promise.reject(new Error('no voicing by that name'));
        state.config.voicings.splice(at, 1);
        return Promise.resolve({ ok: true });
      }

      case 'POST /api/midi/monitor/clear':
        state.monitor.items = [];
        return Promise.resolve({ ok: true });

      case 'GET /api/wifi/scan': {
        // First call reports the scan running, later ones return the list, so
        // the spinner in the UI is exercised too.
        const scanning = !state.scanned;
        state.scanned = true;
        return Promise.resolve({ ok: true, scanning, generation: 1,
                                 networks: scanning ? [] : NETWORKS });
      }
      case 'POST /api/wifi/hotspot':
        return Promise.resolve({ ok: true, ssid: 'MIDI-Trumpet-A1B2',
                                 note: 'the hotspot appears within a few seconds' });
      case 'POST /api/wifi/credentials':
        if (parsed.ssid !== undefined) state.config.wifi.ssid = parsed.ssid;
        if (parsed.mode) state.config.wifi.mode = parsed.mode;
        if (parsed.password) state.config.wifi.passwordSet = true;
        if (parsed.apPassword !== undefined) {
          state.config.wifi.apPasswordSet = parsed.apPassword.length > 0;
        }
        return Promise.resolve({ ok: true, rebootRequired: true });

      case 'GET /api/fingering': return Promise.resolve(fingering());
      case 'PUT /api/fingering': {
        // Same accounting as the firmware: only what differs from the standard
        // chart is stored, and there is room for a bounded number of edits.
        const chart = fingering().notes;
        const standard = {};
        chart.forEach((r) => { standard[r.written] = r.primary + ':' + r.alternate; });
        const edits = (parsed.notes || []).filter(
          (r) => standard[r.written] !== undefined &&
                 standard[r.written] !== r.primary + ':' + r.alternate);
        return Promise.resolve({ ok: true, applied: (parsed.notes || []).length,
                                 persisted: true, stored: Math.min(edits.length, 48),
                                 truncated: edits.length > 48 });
      }
      case 'POST /api/fingering/reset': return Promise.resolve({ ok: true });

      case 'POST /api/panic':
        state.mode = 'PANIC';
        state.valves.forEach((v) => { v.pressed = false; });
        state.muted = true;
        return Promise.resolve({ ok: true });
      case 'POST /api/panic/release':
        state.mode = 'RUNNING';
        state.muted = false;
        return Promise.resolve({ ok: true });
      case 'POST /api/valve/mode':
        state.valveMode = parsed.mode;
        return Promise.resolve({ ok: true });
      case 'POST /api/valve/manual':
        if (state.valves[parsed.valve]) state.valves[parsed.valve].pressed = parsed.pressed;
        return Promise.resolve({ ok: true });
      case 'POST /api/audio/volume':
        state.volume = parsed.volume;
        return Promise.resolve({ ok: true });
      case 'POST /api/audio/mute':
        state.muted = parsed.muted;
        return Promise.resolve({ ok: true });

      default:
        return Promise.resolve({ ok: true });
    }
  }

  return { handle, telemetry, state, defaultConfig, defaultVoicing,
           sanitiseVoicing, acousticModel };
})();
