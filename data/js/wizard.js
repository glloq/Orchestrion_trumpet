/* ===========================================================================
   wizard.js — the ten step first-run assistant, in a modal.

   Board → audio backend → amplifier → speaker → valves → MIDI →
   GPIO validation → audio test → valve calibration → save.

   Nothing is written to the instrument until the last step.
   =========================================================================== */
'use strict';

const Wizard = (() => {
  const STEPS = ['Board', 'Audio backend', 'Amplifier', 'Speaker', 'Valves',
                 'MIDI', 'GPIO validation', 'Audio test', 'Valve calibration', 'Save'];
  let step = 0;
  let root = null;

  const cfg = () => App.config();
  const hw = () => App.hardware();
  const set = (path, value) => App.set(path, value);

  function open(atStep) {
    step = atStep || 0;
    render();
  }

  function close() {
    UI.clear(document.getElementById('modal-root'));
    root = null;
  }

  function render() {
    const host = document.getElementById('modal-root');
    UI.clear(host);

    const body = UI.el('div', { class: 'modal-body' });
    root = UI.el('div', { class: 'overlay', onclick: (e) => { if (e.target === root) close(); } }, [
      UI.el('div', { class: 'modal' }, [
        UI.el('div', { class: 'modal-head' }, [
          UI.el('h2', { text: 'Setup wizard' }),
          UI.el('button', { class: 'icon-btn', text: '✕', onclick: close })
        ]),
        UI.el('div', { style: 'padding:14px 22px 0' }, [
          UI.el('div', { class: 'steps' }, STEPS.map((label, index) => UI.el('div', {
            class: 'step-dot' + (index < step ? ' done' : index === step ? ' current' : ''),
            title: label, text: String(index + 1),
            onclick: () => { if (index <= step) { step = index; render(); } }
          }))),
          UI.el('div', { class: 'section-title', text: 'Step ' + (step + 1) + ' — ' + STEPS[step] })
        ]),
        body,
        UI.el('div', { class: 'modal-foot' }, [
          UI.el('span', { class: 'hint', text: 'Nothing is written until the last step.' }),
          UI.el('div', { class: 'btn-row' }, [
            UI.el('button', { class: 'btn', text: 'Back', disabled: step === 0,
              onclick: () => { if (step > 0) { step--; render(); } } }),
            step < STEPS.length - 1
              ? UI.el('button', { class: 'btn primary', text: 'Next',
                  onclick: () => { step++; render(); } })
              : UI.el('button', { class: 'btn primary', text: 'Save and reboot', onclick: save })
          ])
        ])
      ])
    ]);
    host.appendChild(root);
    (BUILDERS[step] || (() => {}))(body);
  }

  const BUILDERS = [
    // 1 — board
    (body) => {
      const b = hw() && hw().board;
      if (!b) { body.appendChild(UI.el('div', { class: 'help', text: 'Loading…' })); return; }
      body.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'The board is detected, not chosen: the wizard only offers what this chip can '
            + 'really do.' }));
      body.appendChild(UI.kv([
        ['Chip', b.chip], ['Cores', String(b.cores)],
        ['Flash', (b.flash / 1048576).toFixed(1) + ' MB'],
        ['PSRAM', b.psram ? (b.psram / 1048576).toFixed(1) + ' MB' : 'none'],
        ['USB-MIDI', UI.badge(b.hasNativeUsb ? 'available' : 'not available on this board',
                              b.hasNativeUsb ? 'ok' : '')],
        ['Internal DAC', UI.badge(b.hasInternalDac ? 'available' : 'not available on this board',
                                  b.hasInternalDac ? 'ok' : '')],
        ['BLE MIDI', UI.badge(b.hasBle ? 'available' : 'not available', b.hasBle ? 'ok' : '')]
      ]));
      body.appendChild(UI.el('div', { class: 'section-title', text: 'Start from a documented bundle' }));
      body.appendChild(UI.el('div', { class: 'presets' }, hw().presets.map((p) => UI.el('div', {
        class: 'preset' + (cfg().system.preset === p.key ? ' selected' : ''),
        onclick: async () => {
          try {
            await API.applyPreset(p.key);
            await App.reload();
            render();
          } catch (err) { UI.toast(err.message, 'bad'); }
        }
      }, [
        p.recommended ? UI.el('div', { class: 'reco', text: 'RECOMMENDED' }) : null,
        UI.el('div', { class: 't', text: p.title }),
        UI.el('div', { class: 's', text: p.summary }),
        p.stars ? UI.el('div', { class: 'stars', text: UI.stars(p.stars) }) : null
      ]))));
    },

    // 2 — audio backend
    (body) => {
      body.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'The DAC, the amplifier and the speaker are three separate choices, so any of them '
            + 'can be swapped later without touching the rest.' }));
      body.appendChild(UI.el('div', { class: 'presets' },
        ((hw() && hw().audioBackends) || []).map((b) => UI.el('div', {
          class: 'preset' + (cfg().audio.backend === b.key ? ' selected' : '')
                          + (b.available ? '' : ' disabled'),
          onclick: () => { set('audio.backend', b.key); render(); }
        }, [
          UI.el('div', { class: 't' }, [
            document.createTextNode(b.title + ' '),
            b.maturity !== 'STABLE'
              ? UI.badge(b.maturity.toLowerCase(), b.maturity === 'PROTOTYPE' ? 'warn' : 'info')
              : null
          ]),
          UI.el('div', { class: 's', text: b.summary })
        ]))));
    },

    // 3 — amplifier
    (body) => {
      const amps = (hw() && hw().amplifiers) || [];
      body.appendChild(UI.field('Amplifier', UI.select(
        amps.map((a) => ({ value: a.key, label: a.key })), cfg().amplifier.type, (v) => {
          const preset = amps.find((a) => a.key === v);
          set('amplifier.type', v);
          if (preset && v !== 'CUSTOM') {
            set('amplifier.maxPower', preset.maxPower);
            set('amplifier.gainDb', preset.gainDb);
            set('amplifier.speakerImpedance', preset.impedance);
          }
          render();
        })));
      body.appendChild(UI.el('div', { class: 'form-grid' }, [
        UI.field('Maximum power (W)', UI.number(cfg().amplifier.maxPower,
          (v) => set('amplifier.maxPower', v), { min: 0, max: 200 })),
        UI.field('Gain (dB)', UI.number(cfg().amplifier.gainDb,
          (v) => set('amplifier.gainDb', v), { min: 0, max: 40 })),
        UI.field('Speaker impedance (Ω)', UI.number(cfg().amplifier.speakerImpedance,
          (v) => set('amplifier.speakerImpedance', v), { min: 2, max: 32, step: 0.1 }))
      ]));
      body.appendChild(UI.el('p', { class: 'help',
        text: 'These numbers feed the protection stage: the maximum safe level is computed from '
            + 'them and from the speaker, never guessed.' }));
    },

    // 4 — speaker
    (body) => {
      body.appendChild(UI.el('div', { class: 'presets' },
        ((hw() && hw().speakers) || []).map((s) => UI.el('div', {
          class: 'preset' + (cfg().speaker.profile === s.key ? ' selected' : ''),
          onclick: () => {
            set('speaker.profile', s.key);
            if (s.key !== 'CUSTOM') {
              set('speaker.name', s.name);
              set('speaker.impedance', s.impedance);
              set('speaker.powerRms', s.powerRms);
              set('speaker.powerMax', s.powerMax);
              set('speaker.fsHz', s.fsHz || 0);
              set('speaker.minFrequency', s.minFrequency);
              set('speaker.recommendedHighPass', s.recommendedHighPass);
              set('speaker.powerLimit', s.powerLimit);
            }
            render();
          }
        }, [
          UI.el('div', { class: 't', text: s.name }),
          UI.el('div', { class: 's', text: s.impedance + ' Ω · ' + s.powerRms + ' W RMS · from '
                                        + s.minFrequency + ' Hz' })
        ]))));
      body.appendChild(UI.field('Acoustic coupling', UI.select([
        { value: 'SEALED_CHAMBER', label: 'Sealed chamber into the leadpipe (reference)' },
        { value: 'OPEN', label: 'Open' },
        { value: 'CUSTOM_CHAMBER', label: 'Custom chamber' }
      ], cfg().acoustic.coupling, (v) => set('acoustic.coupling', v))));
    },

    // 5 — valves
    (body) => {
      body.appendChild(UI.field('Number of valves', UI.select(
        [1, 2, 3, 4].map((n) => ({ value: String(n), label: String(n) })),
        String(cfg().valves.count),
        (v) => { set('valves.count', parseInt(v, 10)); render(); })));
      body.appendChild(UI.el('p', { class: 'help',
        text: 'Each valve is independent: a mixed servo / solenoid instrument is a normal '
            + 'configuration, not a special case.' }));
      for (let i = 0; i < cfg().valves.count; i++) {
        const item = cfg().valves.items[i];
        const path = 'valves.items.' + i + '.';
        body.appendChild(UI.el('div', {}, [
          UI.el('div', { class: 'section-title', text: 'Valve ' + (i + 1) }),
          UI.el('div', { class: 'form-grid' }, [
            UI.field('Actuator', UI.select([
              { value: 'SERVO', label: 'Servo' }, { value: 'SOLENOID', label: 'Solenoid' },
              { value: 'DISABLED', label: 'Not fitted' }
            ], item.type, (v) => { set(path + 'type', v); render(); })),
            item.type === 'SERVO' ? UI.field('Driver', UI.select([
              { value: 'ESP32_PWM', label: 'ESP32 PWM' }, { value: 'PCA9685', label: 'PCA9685' }
            ], item.driver, (v) => { set(path + 'driver', v); render(); })) : null,
            (item.type === 'SERVO' && item.driver === 'PCA9685')
              ? UI.field('Channel', UI.number(item.channel, (v) => set(path + 'channel', v),
                                              { min: 0, max: 15 }))
              : (item.type !== 'DISABLED'
                  ? UI.field('GPIO', UI.number(item.gpio, (v) => set(path + 'gpio', v),
                                               { min: -1, max: 48 }))
                  : null)
          ])
        ]));
      }
    },

    // 6 — MIDI
    (body) => {
      const caps = (hw() && hw().midi) || {};
      const line = (label, path, available, note) => UI.el('div', { class: 'field' }, [
        available
          ? UI.toggle(label, !!App.get(path), (v) => set(path, v))
          : UI.el('div', { class: 'row' }, [
              UI.toggle(label, false, () => {}, true),
              UI.badge('not available on this board', 'flat')
            ]),
        note ? UI.el('div', { class: 'help', text: note }) : null
      ]);
      body.appendChild(UI.el('div', { class: 'section-title', text: 'Inputs' }));
      body.appendChild(line('USB-MIDI', 'midi.usb.in', caps.usbAvailable,
        'Class compliant: no driver on Windows, macOS, Linux or a Raspberry Pi.'));
      body.appendChild(line('BLE MIDI', 'midi.ble.in', caps.bleAvailable));
      body.appendChild(line('RTP-MIDI over Wi-Fi', 'midi.rtp.in', caps.rtpAvailable));
      body.appendChild(line('DIN MIDI', 'midi.din.in', true,
        'The DIN input must be opto-isolated on the hardware side.'));
      body.appendChild(line('Web keyboard', 'midi.web.in', true));
      body.appendChild(UI.el('div', { class: 'section-title', text: 'Outputs' }));
      body.appendChild(line('DIN OUT', 'midi.din.out', true));
      body.appendChild(line('BLE OUT', 'midi.ble.out', caps.bleAvailable));
      body.appendChild(line('USB OUT', 'midi.usb.out', caps.usbAvailable));
      body.appendChild(UI.field('Bluetooth name', UI.text(cfg().midi.ble.name,
        (v) => set('midi.ble.name', v), { maxlength: 31 })));
    },

    // 7 — GPIO validation
    (body) => {
      body.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Every pin is checked against the board: duplicates, flash and PSRAM lines, '
            + 'input-only pins and strapping pins.' }));
      const result = UI.el('div');
      body.appendChild(result);
      body.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-top:10px' }, [
        UI.el('button', { class: 'btn small', text: 'Check again',
                          onclick: () => check(result) })
      ]));
      check(result);
    },

    // 8 — audio test
    (body) => {
      body.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'These play through the backend currently running. If you changed the backend in '
            + 'this wizard, save and reboot first.' }));
      body.appendChild(UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn', text: '440 Hz tone',
          onclick: () => API.audioTest({ type: 'tone', frequency: 440, durationMs: 2000,
                                         amplitude: 0.2 }) }),
        UI.el('button', { class: 'btn', text: 'Sweep',
          onclick: () => API.audioTest({ type: 'sweep', startHz: 100, endHz: 8000,
                                         durationMs: 6000, amplitude: 0.2 }) }),
        UI.el('button', { class: 'btn', text: 'Stop',
          onclick: () => API.audioTest({ type: 'stop' }) })
      ]));
      body.appendChild(UI.el('div', { class: 'field', style: 'margin-top:18px' }, [
        UI.el('label', { text: 'Output level' }),
        UI.el('div', { class: 'meter' }, [UI.el('i')])
      ]));
    },

    // 9 — valve calibration
    (body) => {
      body.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Moving a slider drives the servo straight away. Set the released position first, '
            + 'then the pressed one, with the linkage attached.' }));
      for (let i = 0; i < cfg().valves.count; i++) {
        const item = cfg().valves.items[i];
        const path = 'valves.items.' + i + '.';
        if (item.type !== 'SERVO') {
          body.appendChild(UI.el('div', {}, [
            UI.el('div', { class: 'section-title', text: 'Valve ' + (i + 1) }),
            UI.el('div', { class: 'help', text: item.type === 'SOLENOID'
              ? 'A solenoid has no angle: test it with a pulse instead.' : 'Not fitted.' }),
            item.type === 'SOLENOID' ? UI.el('div', { class: 'btn-row', style: 'margin-top:8px' }, [
              UI.el('button', { class: 'btn small', text: 'Test pulse',
                onclick: () => API.valveTest(i, 300).catch((e) => UI.toast(e.message, 'bad')) })
            ]) : null
          ]));
          continue;
        }
        body.appendChild(UI.el('div', {}, [
          UI.el('div', { class: 'section-title', text: 'Valve ' + (i + 1) }),
          UI.el('div', { class: 'row' }, [
            UI.slider('Released', item.releasedAngle, 0, 180, 1, (v) => {
              set(path + 'releasedAngle', v); API.valveCalibrate(i, v);
            }, (v) => v + '°'),
            UI.slider('Pressed', item.pressedAngle, 0, 180, 1, (v) => {
              set(path + 'pressedAngle', v); API.valveCalibrate(i, v);
            }, (v) => v + '°')
          ])
        ]));
      }
    },

    // 10 — save
    (body) => {
      body.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'The configuration is validated once more, written to the flash and the instrument '
            + 'reboots with it.' }));
      const result = UI.el('div');
      body.appendChild(result);
      check(result);
      const valves = cfg().valves.items.slice(0, cfg().valves.count);
      body.appendChild(UI.el('div', { class: 'section-title', text: 'Summary' }));
      body.appendChild(UI.kv([
        ['Audio', cfg().audio.backend + ' → ' + cfg().amplifier.type + ' → ' + cfg().speaker.name],
        ['Coupling', cfg().acoustic.coupling],
        ['Valves', valves.map((v, i) => (i + 1) + ':' + v.type).join('   ')],
        ['MIDI in', [['USB', cfg().midi.usb.in], ['BLE', cfg().midi.ble.in],
                     ['RTP', cfg().midi.rtp.in], ['DIN', cfg().midi.din.in],
                     ['Web', cfg().midi.web.in]]
          .filter(([, on]) => on).map(([n]) => n).join(', ') || 'none']
      ]));
      body.appendChild(UI.el('p', { class: 'help',
        text: 'Next: the Wiring page walks through the electrical protections — the flyback '
            + 'diodes, the fuse and the emergency stop the firmware cannot check for you.' }));
    }
  ];

  async function check(container) {
    UI.clear(container);
    container.appendChild(UI.el('div', { class: 'help', text: 'Checking…' }));
    try {
      const r = await API.validate(cfg());
      UI.clear(container);
      if (!r.issues.length) {
        container.appendChild(UI.badge('No problem found', 'ok'));
        return;
      }
      container.appendChild(UI.issues(r.issues));
      if (r.issues.some((i) => i.severity === 'ERROR')) {
        container.appendChild(UI.el('div', { class: 'help',
          text: 'Errors must be fixed before the configuration can be saved.' }));
      }
    } catch (err) {
      UI.clear(container);
      container.appendChild(UI.el('div', { class: 'help', text: err.message }));
    }
  }

  async function save() {
    try {
      App.set('system.wizardCompleted', true);
      const r = await API.putConfig(cfg());
      if (!r.ok) {
        UI.toast('The configuration was refused', 'bad');
        return;
      }
      UI.toast('Saved. Rebooting…', 'ok');
      App.clearDirty();
      close();
      await API.reboot();
    } catch (err) { UI.toast(err.message, 'bad'); }
  }

  // Lets the screenshot tool and the tests drive the wizard.
  const gotoStep = (index) => { step = index; render(); };

  return { open, close, gotoStep, isOpen: () => !!root };
})();
