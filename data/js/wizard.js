/* ===========================================================================
   wizard.js - the ten step first-run assistant.
   Board -> audio backend -> amplifier -> speaker -> valves -> MIDI ->
   GPIO validation -> audio test -> valve calibration -> save.
   =========================================================================== */
'use strict';

const Wizard = (() => {
  const STEPS = [
    'Board', 'Audio backend', 'Amplifier', 'Speaker', 'Valve actuator',
    'MIDI interfaces', 'GPIO validation', 'Audio test', 'Valve calibration', 'Save'
  ];

  let step = 0;
  let host = null;

  const cfg = () => Pages.state.config;
  const hw = () => Pages.state.hardware;

  function render(container) {
    host = container || host;
    UI.clear(host);

    host.appendChild(UI.el('h1', { text: 'Setup wizard' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'Ten steps from a bare board to a playable trumpet. Nothing is written to the '
          + 'instrument until the last one.' }));

    host.appendChild(UI.el('div', { class: 'steps' }, STEPS.map((label, index) =>
      UI.el('div', {
        class: 'step-dot' + (index < step ? ' done' : index === step ? ' current' : ''),
        title: label, text: String(index + 1),
        onclick: () => { if (index <= step) { step = index; render(); } }
      }))));

    const body = UI.el('div');
    host.appendChild(UI.card(STEPS[step], [body]));
    (RENDERERS[step] || (() => {}))(body);

    host.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-top:16px' }, [
      UI.el('button', { class: 'btn ghost', text: 'Back', disabled: step === 0,
        onclick: () => { if (step > 0) { step--; render(); } } }),
      step < STEPS.length - 1
        ? UI.el('button', { class: 'btn primary', text: 'Next',
            onclick: () => { step++; render(); } })
        : UI.el('button', { class: 'btn primary', text: 'Save and reboot', onclick: save })
    ]));
  }

  const RENDERERS = [
    // 1 - board
    (body) => {
      const b = hw() ? hw().board : null;
      if (!b) { body.appendChild(UI.el('div', { class: 'hint', text: 'Loading…' })); return; }
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'The board is detected, not chosen: the firmware only offers what this chip can '
            + 'really do.' }));
      body.appendChild(UI.el('dl', { class: 'kv' }, [
        UI.el('dt', { text: 'Chip' }), UI.el('dd', { text: b.chip }),
        UI.el('dt', { text: 'Flash' }), UI.el('dd', { text: (b.flash / 1048576).toFixed(1) + ' MB' }),
        UI.el('dt', { text: 'PSRAM' }),
        UI.el('dd', { text: b.psram ? (b.psram / 1048576).toFixed(1) + ' MB' : 'none' }),
        UI.el('dt', { text: 'USB-MIDI' }),
        UI.el('dd', {}, [UI.badge(b.hasNativeUsb ? 'available' : 'not available on this board',
          b.hasNativeUsb ? 'ok' : 'neutral')]),
        UI.el('dt', { text: 'Internal DAC' }),
        UI.el('dd', {}, [UI.badge(b.hasInternalDac ? 'available' : 'not available on this board',
          b.hasInternalDac ? 'ok' : 'neutral')]),
        UI.el('dt', { text: 'BLE MIDI' }),
        UI.el('dd', {}, [UI.badge(b.hasBle ? 'available' : 'not available', b.hasBle ? 'ok' : 'neutral')])
      ]));

      body.appendChild(UI.el('h3', { text: 'Start from a documented bundle' }));
      body.appendChild(UI.el('div', { class: 'presets' }, hw().presets.map((p) =>
        UI.el('div', {
          class: 'preset' + (cfg().system.preset === p.key ? ' selected' : '')
                          + (p.available ? '' : ' disabled'),
          onclick: () => { applyPresetLocally(p.key); render(); }
        }, [
          p.recommended ? UI.el('div', { class: 'reco', text: 'RECOMMENDED' }) : null,
          UI.el('div', { class: 't', text: p.title }),
          UI.el('div', { class: 's', text: p.summary }),
          p.stars ? UI.el('div', { class: 'stars', text: UI.stars(p.stars) }) : null
        ]))));
    },

    // 2 - audio backend
    (body) => {
      const backends = hw() ? hw().audioBackends : [];
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'The DAC, the amplifier and the speaker are three separate choices, so any of them '
            + 'can be swapped later without touching the rest.' }));
      body.appendChild(UI.el('div', { class: 'presets' }, backends.map((b) =>
        UI.el('div', {
          class: 'preset' + (cfg().audio.backend === b.key ? ' selected' : '')
                          + (b.available ? '' : ' disabled'),
          onclick: () => { cfg().audio.backend = b.key; App.setDirty(true); render(); }
        }, [
          UI.el('div', { class: 't' }, [
            document.createTextNode(b.title + ' '),
            b.maturity !== 'STABLE' ? UI.badge(b.maturity.toLowerCase(),
              b.maturity === 'PROTOTYPE' ? 'warn' : 'info') : null
          ]),
          UI.el('div', { class: 's', text: b.summary })
        ]))));
    },

    // 3 - amplifier
    (body) => {
      const amps = hw() ? hw().amplifiers : [];
      body.appendChild(UI.field('Amplifier', UI.select(
        amps.map((a) => ({ value: a.key, label: a.key })), cfg().amplifier.type, (v) => {
          const preset = amps.find((a) => a.key === v);
          cfg().amplifier.type = v;
          if (preset && v !== 'CUSTOM') {
            cfg().amplifier.maxPower = preset.maxPower;
            cfg().amplifier.gainDb = preset.gainDb;
            cfg().amplifier.speakerImpedance = preset.impedance;
          }
          App.setDirty(true);
          render();
        })));
      body.appendChild(UI.el('div', { class: 'row' }, [
        UI.field('Maximum power (W)', UI.number(cfg().amplifier.maxPower,
          (v) => { cfg().amplifier.maxPower = v; App.setDirty(true); }, { min: 0, max: 200 })),
        UI.field('Gain (dB)', UI.number(cfg().amplifier.gainDb,
          (v) => { cfg().amplifier.gainDb = v; App.setDirty(true); }, { min: 0, max: 40 })),
        UI.field('Speaker impedance (ohm)', UI.number(cfg().amplifier.speakerImpedance,
          (v) => { cfg().amplifier.speakerImpedance = v; App.setDirty(true); },
          { min: 2, max: 32, step: 0.1 }))
      ]));
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'These numbers feed the protection stage: the maximum safe level is computed from '
            + 'them and from the speaker, never guessed.' }));
    },

    // 4 - speaker
    (body) => {
      const speakers = hw() ? hw().speakers : [];
      body.appendChild(UI.el('div', { class: 'presets' }, speakers.map((s) =>
        UI.el('div', {
          class: 'preset' + (cfg().speaker.profile === s.key ? ' selected' : ''),
          onclick: () => {
            cfg().speaker.profile = s.key;
            if (s.key !== 'CUSTOM') {
              cfg().speaker.name = s.name;
              cfg().speaker.impedance = s.impedance;
              cfg().speaker.powerRms = s.powerRms;
              cfg().speaker.recommendedHighPass = s.recommendedHighPass;
              cfg().speaker.powerLimit = s.powerLimit;
            }
            App.setDirty(true);
            render();
          }
        }, [
          UI.el('div', { class: 't', text: s.name }),
          UI.el('div', { class: 's',
            text: s.impedance + ' Ω · ' + s.powerRms + ' W RMS · from ' + s.minFrequency + ' Hz' })
        ]))));
      body.appendChild(UI.field('Acoustic coupling', UI.select([
        { value: 'SEALED_CHAMBER', label: 'Sealed chamber into the leadpipe (reference)' },
        { value: 'OPEN', label: 'Open' },
        { value: 'CUSTOM_CHAMBER', label: 'Custom chamber' }
      ], cfg().acoustic.coupling, (v) => { cfg().acoustic.coupling = v; App.setDirty(true); })));
    },

    // 5 - valve actuator
    (body) => {
      body.appendChild(UI.field('Number of valves', UI.select(
        [1, 2, 3, 4].map((n) => ({ value: String(n), label: String(n) })),
        String(cfg().valves.count),
        (v) => { cfg().valves.count = parseInt(v, 10); App.setDirty(true); render(); })));
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'Each valve is independent: a mixed servo / solenoid instrument is a normal '
            + 'configuration, not a special case.' }));
      body.appendChild(UI.el('div', { class: 'grid' },
        cfg().valves.items.slice(0, cfg().valves.count).map((item, index) =>
          UI.card('Valve ' + (index + 1), [
            UI.field('Actuator', UI.select([
              { value: 'SERVO', label: 'Servo' },
              { value: 'SOLENOID', label: 'Solenoid' },
              { value: 'DISABLED', label: 'Not fitted' }
            ], item.type, (v) => { item.type = v; App.setDirty(true); render(); })),
            item.type === 'SERVO' ? UI.field('Driver', UI.select([
              { value: 'ESP32_PWM', label: 'ESP32 PWM' },
              { value: 'PCA9685', label: 'PCA9685' }
            ], item.driver, (v) => { item.driver = v; App.setDirty(true); render(); })) : null,
            (item.type === 'SERVO' && item.driver === 'PCA9685')
              ? UI.field('Channel', UI.number(item.channel,
                  (v) => { item.channel = v; App.setDirty(true); }, { min: 0, max: 15 }))
              : (item.type !== 'DISABLED'
                  ? UI.field('GPIO', UI.number(item.gpio,
                      (v) => { item.gpio = v; App.setDirty(true); }, { min: -1, max: 48 }))
                  : null)
          ]))));
    },

    // 6 - MIDI interfaces
    (body) => {
      const caps = hw() ? hw().midi : {};
      const line = (label, path, available, note) => {
        const parts = path.split('.');
        const node = parts.slice(0, -1).reduce((o, k) => o[k], cfg());
        const key = parts[parts.length - 1];
        return UI.el('div', { class: 'field' }, [
          available
            ? UI.toggle(label, node[key], (v) => { node[key] = v; App.setDirty(true); })
            : UI.el('div', { class: 'row' }, [
                UI.toggle(label, false, () => {}, true),
                UI.badge('not available on this board', 'neutral')
              ]),
          note ? UI.el('div', { class: 'hint', text: note }) : null
        ]);
      };
      body.appendChild(UI.el('h3', { text: 'Inputs' }));
      body.appendChild(line('USB-MIDI', 'midi.usb.in', caps.usbAvailable,
        'Class compliant: no driver on Windows, macOS, Linux or a Raspberry Pi.'));
      body.appendChild(line('BLE MIDI', 'midi.ble.in', caps.bleAvailable));
      body.appendChild(line('RTP-MIDI over Wi-Fi', 'midi.rtp.in', caps.rtpAvailable));
      body.appendChild(line('DIN MIDI', 'midi.din.in', true,
        'The DIN input must be opto-isolated on the hardware side.'));
      body.appendChild(line('Web keyboard', 'midi.web.in', true));
      body.appendChild(UI.el('h3', { text: 'Outputs' }));
      body.appendChild(line('DIN OUT', 'midi.din.out', true));
      body.appendChild(line('BLE OUT', 'midi.ble.out', caps.bleAvailable));
      body.appendChild(line('USB OUT', 'midi.usb.out', caps.usbAvailable));
      body.appendChild(UI.field('Bluetooth name', UI.text(cfg().midi.ble.name,
        (v) => { cfg().midi.ble.name = v; App.setDirty(true); }, { maxlength: 31 })));
    },

    // 7 - GPIO validation
    (body) => {
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'Every pin is checked against the board: duplicates, flash and PSRAM lines, '
            + 'input-only pins and strapping pins.' }));
      const result = UI.el('div');
      body.appendChild(result);
      body.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-top:10px' }, [
        UI.el('button', { class: 'btn small', text: 'Check again', onclick: () => check(result) })
      ]));
      check(result);
    },

    // 8 - audio test
    (body) => {
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'These play through the backend that is currently running. If you changed the '
            + 'backend in this wizard, save and reboot first.' }));
      body.appendChild(UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn', text: '440 Hz tone',
          onclick: () => API.audioTest({ type: 'tone', frequency: 440, durationMs: 2000,
                                         amplitude: 0.2 }) }),
        UI.el('button', { class: 'btn', text: 'Sweep',
          onclick: () => API.audioTest({ type: 'sweep', startHz: 100, endHz: 8000,
                                         durationMs: 6000, amplitude: 0.2 }) }),
        UI.el('button', { class: 'btn ghost', text: 'Stop',
          onclick: () => API.audioTest({ type: 'stop' }) })
      ]));
      body.appendChild(UI.el('div', { class: 'field', style: 'margin-top:16px' }, [
        UI.el('label', { text: 'Output level' }),
        UI.el('div', { class: 'meter' }, [UI.el('i')])
      ]));
    },

    // 9 - valve calibration
    (body) => {
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'Moving a slider drives the servo straight away. Set the released position first, '
            + 'then the pressed one, with the linkage attached.' }));
      const items = cfg().valves.items.slice(0, cfg().valves.count);
      body.appendChild(UI.el('div', { class: 'grid' }, items.map((item, index) => {
        if (item.type !== 'SERVO') {
          return UI.card('Valve ' + (index + 1), [
            UI.el('div', { class: 'hint', text: item.type === 'SOLENOID'
              ? 'A solenoid has no angle: test it with a pulse instead.' : 'Not fitted.' }),
            item.type === 'SOLENOID' ? UI.el('button', { class: 'btn small', text: 'Test pulse',
              onclick: () => API.valveTest(index, 300).catch((e) => UI.toast(e.message, 'bad'))
            }) : null
          ]);
        }
        return UI.card('Valve ' + (index + 1), [
          UI.slider('Released', item.releasedAngle, 0, 180, 1, (v) => {
            item.releasedAngle = v; App.setDirty(true); API.valveCalibrate(index, v);
          }, (v) => v + '°'),
          UI.slider('Pressed', item.pressedAngle, 0, 180, 1, (v) => {
            item.pressedAngle = v; App.setDirty(true); API.valveCalibrate(index, v);
          }, (v) => v + '°'),
          UI.el('div', { class: 'btn-row' }, [
            UI.el('button', { class: 'btn small', text: 'Test released',
              onclick: () => API.valveCalibrate(index, item.releasedAngle) }),
            UI.el('button', { class: 'btn small', text: 'Test pressed',
              onclick: () => API.valveCalibrate(index, item.pressedAngle) })
          ])
        ]);
      })));
    },

    // 10 - save
    (body) => {
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'The configuration is validated once more, written to the flash and the instrument '
            + 'reboots with it.' }));
      const result = UI.el('div');
      body.appendChild(result);
      check(result);
      body.appendChild(UI.el('dl', { class: 'kv', style: 'margin-top:14px' }, [
        UI.el('dt', { text: 'Audio' }),
        UI.el('dd', { text: cfg().audio.backend + ' → ' + cfg().amplifier.type + ' → '
                          + cfg().speaker.name }),
        UI.el('dt', { text: 'Coupling' }), UI.el('dd', { text: cfg().acoustic.coupling }),
        UI.el('dt', { text: 'Valves' }),
        UI.el('dd', { text: cfg().valves.items.slice(0, cfg().valves.count)
          .map((v, i) => (i + 1) + ':' + v.type).join('  ') }),
        UI.el('dt', { text: 'MIDI in' }),
        UI.el('dd', { text: [['USB', cfg().midi.usb.in], ['BLE', cfg().midi.ble.in],
                             ['RTP', cfg().midi.rtp.in], ['DIN', cfg().midi.din.in],
                             ['Web', cfg().midi.web.in]]
          .filter(([, on]) => on).map(([name]) => name).join(', ') || 'none' })
      ]));
    }
  ];

  async function check(container) {
    UI.clear(container);
    container.appendChild(UI.el('div', { class: 'hint', text: 'Checking…' }));
    try {
      const result = await API.validate(cfg());
      UI.clear(container);
      if (!result.issues.length) {
        container.appendChild(UI.badge('No problem found', 'ok'));
      } else {
        container.appendChild(UI.issues(result.issues));
        if (result.issues.some((i) => i.severity === 'ERROR')) {
          container.appendChild(UI.el('div', { class: 'hint',
            text: 'Errors must be fixed before the configuration can be saved.' }));
        }
      }
    } catch (err) {
      UI.clear(container);
      container.appendChild(UI.el('div', { class: 'hint', text: err.message }));
    }
  }

  function applyPresetLocally(key) {
    // Applied on the device so the numbers come from one single source of
    // truth (the C++ preset table), then reloaded into the working copy.
    API.applyPreset(key)
      .then((result) => {
        if (!result.ok) { UI.toast('The preset was refused', 'bad'); return; }
        return App.reload().then(() => { render(); });
      })
      .catch((err) => UI.toast(err.message, 'bad'));
  }

  async function save() {
    try {
      cfg().system.wizardCompleted = true;
      const result = await API.putConfig(cfg());
      if (!result.ok) {
        UI.toast('The configuration was refused', 'bad');
        App.showIssues(result.issues);
        return;
      }
      UI.toast('Saved. Rebooting…', 'ok');
      App.setDirty(false);
      await API.reboot();
    } catch (err) { UI.toast(err.message, 'bad'); }
  }

  function page(container) {
    step = 0;
    render(container);
    container.updateLive = (t) => {
      if (!t || !t.audio) return;
      const meter = container.querySelector('.meter');
      if (!meter) return;
      const peak = Math.min(1, t.audio.peak || 0);
      meter.querySelector('i').style.width = (peak * 100).toFixed(1) + '%';
    };
  }

  return { page };
})();
