/* ===========================================================================
   pages.js — the three views the sidebar switches between.

     Play       what the instrument is doing, and how to play it
     Configure  presets, instrument, calibration, validation, wizard
     Wiring     the generated harness and the electrical dossier

   Everything else is one gear away, in the settings modal.
   =========================================================================== */
'use strict';

const Pages = (() => {

  const cfg = () => App.config();
  const set = (path, value) => App.set(path, value);

  // =========================================================================
  // Play
  // =========================================================================
  function play(host) {
    const status = App.status() || {};
    const armed = status.mode !== 'PANIC';

    host.appendChild(UI.card(null, [
      UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn danger', text: 'STOP', onclick: () => App.panic() }),
        UI.el('button', { class: 'btn', text: 'Re-arm', onclick: async () => {
          await API.releasePanic();
          UI.toast('Instrument re-armed', 'ok');
          App.refreshStatus();
        } }),
        UI.badge(armed ? 'ARMED' : 'STOPPED', armed ? 'ok' : 'bad'),
        UI.el('span', { style: 'margin-left:auto' }),
        UI.el('span', { class: 'card-note', text: 'VALVES' }),
        UI.select([
          { value: 'AUTO', label: 'Auto' }, { value: 'MANUAL', label: 'Manual' },
          { value: 'MIDI_CC', label: 'MIDI CC' }, { value: 'DISABLED', label: 'Off' }
        ], cfg().valves.mode, (v) => {
          set('valves.mode', v);
          API.valveMode(v).catch((e) => UI.toast(e.message, 'bad'));
          App.render();
        })
      ])
    ]));

    const trumpetHost = UI.el('div', { class: 'trumpet-wrap' });
    host.appendChild(UI.card('Instrument', [
      trumpetHost,
      UI.el('p', { class: 'card-note', style: 'text-align:center',
                   text: cfg().valves.mode === 'MANUAL'
                     ? 'Press a piston to actuate it.'
                     : 'The pistons follow the fingering of the note being played.' })
    ], UI.el('span', { class: 'card-note', id: 'now-playing', text: '—' })));

    Trumpet.build(trumpetHost, {
      valveCount: cfg().valves.count,
      interactive: cfg().valves.mode === 'MANUAL',
      onPress: (index, pressed) => WS.valve(index, pressed)
    });

    const kbd = UI.el('div', { class: 'kbd' });
    const octaveLabel = UI.badge('C4', 'flat');
    host.appendChild(UI.card('Keyboard', [
      UI.el('div', { class: 'btn-row', style: 'margin-bottom:12px' }, [
        UI.el('button', { class: 'btn small', text: 'Octave −',
          onclick: () => { octaveLabel.textContent = UI.noteName(Keyboard.shiftOctave(-1)); } }),
        octaveLabel,
        UI.el('button', { class: 'btn small', text: 'Octave +',
          onclick: () => { octaveLabel.textContent = UI.noteName(Keyboard.shiftOctave(1)); } }),
        UI.el('button', { class: 'btn small', text: 'All notes off',
          onclick: () => { Keyboard.releaseAll(); WS.cc(123, 0); } })
      ]),
      UI.el('div', { class: 'kbd-wrap' }, [kbd])
    ], 'played over the WebSocket'));

    Keyboard.build(kbd, {
      baseNote: 60, octaves: 2,
      range: { min: cfg().instrument.noteMin, max: cfg().instrument.noteMax }
    });
    octaveLabel.textContent = UI.noteName(Keyboard.baseNote());

    const bend = UI.slider('Pitch bend', 0, -8192, 8191, 1, (v) => WS.pitchBend(v));
    const releaseBend = () => { bend.setValue(0); WS.pitchBend(0); };
    const bendInput = bend.querySelector('input');
    bendInput.addEventListener('pointerup', releaseBend);
    bendInput.addEventListener('pointercancel', releaseBend);

    const volume = status.audio ? Math.round(status.audio.volume * 100) : 75;
    host.appendChild(UI.card('Controllers', [
      UI.el('div', { class: 'ctrl-strip' }, [
        UI.slider('Velocity', 100, 1, 127, 1, (v) => Keyboard.setVelocity(v)),
        bend,
        UI.slider('Modulation (CC1)', 0, 0, 127, 1, (v) => WS.cc(1, v)),
        UI.slider('Breath (CC2)', 0, 0, 127, 1, (v) => WS.cc(2, v)),
        UI.slider('Expression (CC11)', 127, 0, 127, 1, (v) => WS.cc(11, v)),
        UI.slider('Volume', volume, 0, 100, 1, (v) => API.audioVolume(v / 100), (v) => v + ' %')
      ])
    ]));

    const noteCard = UI.card('Now playing', [UI.el('div', { class: 'grid' }, [
      UI.stat('Note', '—'), UI.stat('Velocity', '0'), UI.stat('Frequency', '0', 'Hz')
    ])]);
    const audioCard = UI.card('Audio', [
      UI.el('dl', { class: 'kv' }),
      UI.el('div', { class: 'field', style: 'margin:12px 0 0' }, [
        UI.el('label', { text: 'Output level' }), UI.el('div', { class: 'meter' }, [UI.el('i')])
      ])
    ]);
    const midiCard = UI.card('MIDI connections', [UI.el('div', { class: 'btn-row' })]);
    host.appendChild(UI.el('div', { class: 'grid two' }, [noteCard, audioCard, midiCard]));

    host.onLeave = () => Keyboard.releaseAll();
    host.updateLive = (t) => {
      if (!t) return;
      Trumpet.update(t.valves);
      const note = t.audio && t.audio.note ? t.audio.note : 0;
      Keyboard.setExternal(note);

      const nowPlaying = document.getElementById('now-playing');
      if (nowPlaying) {
        nowPlaying.textContent = note
          ? UI.noteName(note) + ' · ' + Math.round(t.audio.frequency) + ' Hz'
          : 'silent';
      }
      const stats = noteCard.querySelectorAll('.stat .v');
      stats[0].textContent = note ? UI.noteName(note) : '—';
      stats[1].textContent = t.audio ? t.audio.velocity : 0;
      stats[2].textContent = t.audio ? Math.round(t.audio.frequency) : 0;

      const dl = audioCard.querySelector('.kv');
      UI.clear(dl);
      const rows = [
        ['Backend', t.audio.backend],
        ['Sample rate', (t.audio.sampleRate || 0) + ' Hz'],
        ['State', t.audio.running ? (t.audio.muted ? 'muted' : 'playing') : 'stopped'],
        ['Volume', Math.round((t.audio.volume || 0) * 100) + ' %'],
        ['Limiter', t.audio.gainReduction < 0.99
          ? '−' + (-20 * Math.log10(Math.max(t.audio.gainReduction, 0.001))).toFixed(1) + ' dB'
          : 'inactive'],
        ['Underruns', t.audio.underruns],
        ['Audio CPU', (t.audio.cpu || 0).toFixed(0) + ' %']
      ];
      for (const [k, v] of rows) {
        dl.appendChild(UI.el('dt', { text: k }));
        dl.appendChild(UI.el('dd', { text: String(v) }));
      }
      const meter = audioCard.querySelector('.meter');
      const peak = Math.min(1, t.audio.peak || 0);
      meter.querySelector('i').style.width = (peak * 100).toFixed(1) + '%';
      meter.className = 'meter' + (peak > 0.95 ? ' clip' : peak > 0.75 ? ' hot' : '');

      const midiRow = midiCard.querySelector('.btn-row');
      UI.clear(midiRow);
      [['USB', t.midi.usb], ['BLE', t.midi.ble], ['DIN', t.midi.din],
       ['RTP', t.midi.rtp], ['Web', t.midi.web > 0]]
        .forEach(([name, on]) => midiRow.appendChild(UI.badge(name, on ? 'ok' : '')));
      midiRow.appendChild(UI.el('span', { class: 'card-note',
        text: t.midi.rx + ' msg/s in · ' + t.midi.tx + ' msg/s out' }));
    };
  }

  // =========================================================================
  // Configure
  // =========================================================================
  function configure(host) {
    const hw = App.hardware();
    if (!hw) { host.appendChild(UI.el('div', { class: 'help', text: 'Loading…' })); return; }

    host.appendChild(UI.card('Board', [
      UI.kv([
        ['Chip', hw.board.chip],
        ['Flash', (hw.board.flash / 1048576).toFixed(1) + ' MB'],
        ['PSRAM', hw.board.psram ? (hw.board.psram / 1048576).toFixed(1) + ' MB' : 'none'],
        ['USB-MIDI', UI.badge(hw.board.hasNativeUsb ? 'available' : 'not on this board',
                              hw.board.hasNativeUsb ? 'ok' : '')],
        ['Internal DAC', UI.badge(hw.board.hasInternalDac ? 'available' : 'not on this board',
                                  hw.board.hasInternalDac ? 'ok' : '')],
        ['BLE MIDI', UI.badge(hw.board.hasBle ? 'available' : 'not available',
                              hw.board.hasBle ? 'ok' : '')]
      ])
    ], 'detected, not chosen'));

    host.appendChild(UI.card('Hardware preset', [
      UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Fills the whole audio chain in one click. Everything else — MIDI, valves, Wi-Fi, '
            + 'calibration — is left untouched.' }),
      UI.el('div', { class: 'presets' }, hw.presets.map((p) => UI.el('div', {
        class: 'preset' + (cfg().system.preset === p.key ? ' selected' : '')
                        + (p.available ? '' : ' disabled'),
        onclick: async () => {
          try {
            const r = await API.applyPreset(p.key);
            if (!r.ok) { UI.toast('The preset was refused', 'bad'); return; }
            UI.toast('Preset “' + p.title + '” applied — reboot to use it', 'ok');
            await App.reload();
            App.render();
          } catch (err) { UI.toast(err.message, 'bad'); }
        }
      }, [
        p.recommended ? UI.el('div', { class: 'reco', text: 'RECOMMENDED' }) : null,
        UI.el('div', { class: 't', text: p.title }),
        UI.el('div', { class: 's', text: p.summary }),
        p.stars ? UI.el('div', { class: 'stars', text: UI.stars(p.stars) }) : null
      ])))
    ]));

    host.appendChild(UI.card('Instrument', [
      UI.el('div', { class: 'form-grid' }, [
        UI.field('Instrument', UI.select(
          ['BB_TRUMPET', 'C_TRUMPET', 'EB_TRUMPET', 'CUSTOM'].map((v) => ({ value: v, label: v })),
          cfg().instrument.type, (v) => { set('instrument.type', v); App.render(); }),
          'A B♭ trumpet sounds a whole tone below the written note.'),
        UI.field('Incoming MIDI is', UI.select([
          { value: 'CONCERT', label: 'Concert pitch (heard)' },
          { value: 'WRITTEN', label: 'Written pitch (read)' }
        ], cfg().instrument.pitchMode, (v) => set('instrument.pitchMode', v)),
          'Concert: the note you hear. Written: what a trumpet player reads.'),
        UI.field('Note priority', UI.select([
          { value: 'LAST', label: 'Last note' }, { value: 'HIGH', label: 'Highest' },
          { value: 'LOW', label: 'Lowest' }
        ], cfg().instrument.notePriority, (v) => set('instrument.notePriority', v)),
          'A trumpet is monophonic: this decides which note wins.'),
        UI.field('Portamento (ms)', UI.number(cfg().instrument.portamentoMs,
          (v) => set('instrument.portamentoMs', v), { min: 0, max: 400, step: 5 })),
        UI.el('div', { class: 'field' }, [
          UI.el('label', { text: 'Articulation' }),
          UI.toggle('Legato', cfg().instrument.legato, (v) => set('instrument.legato', v)),
          UI.toggle('Retrigger every note', cfg().instrument.retrigger,
                    (v) => set('instrument.retrigger', v))
        ])
      ])
    ]));

    host.appendChild(UI.disclosure('Fingering table', () => fingeringCard()));
    host.appendChild(calibrationCard());
    host.appendChild(validationCard());
    host.appendChild(UI.card('Setup wizard', [
      UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Ten steps from a bare board to a playable trumpet: board, audio backend, '
            + 'amplifier, speaker, valves, MIDI, GPIO validation, audio test, valve calibration, '
            + 'save.' }),
      UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn primary', text: 'Run the wizard',
                          onclick: () => Wizard.open() })
      ])
    ]));
  }

  // MIDI note → valve combination. Editable, savable, resettable, and it can
  // travel as a JSON file so a chart worked out on one instrument can be moved
  // to another. The chart is never hard-coded into the valve engine: this
  // table is the only thing that decides which pistons go down.
  function fingeringCard() {
    const wrap = UI.el('div');
    const table = UI.el('div', { class: 'table-wrap' });
    let data = null;

    const maskText = (mask) => mask === 0 ? 'open'
      : [1, 2, 4, 8].map((bit, i) => (mask & bit) ? String(i + 1) : '').join('');

    async function load() {
      try {
        data = await API.fingering();
        draw();
      } catch (err) {
        UI.clear(table);
        table.appendChild(UI.el('div', { class: 'help', text: err.message }));
      }
    }

    function draw() {
      UI.clear(table);
      const valveCount = data.valveCount || 3;
      const head = UI.el('tr', {}, [UI.el('th', { text: 'Note' })]
        .concat(Array.from({ length: valveCount }, (_, i) => UI.el('th', { text: 'V' + (i + 1) })))
        .concat([UI.el('th', { text: 'Alternate' })]));

      const rows = data.notes.map((row) => {
        const cells = [UI.el('td', { text: UI.noteName(row.written) })];
        for (let v = 0; v < valveCount; v++) {
          const button = UI.el('button', {
            class: 'btn small chip', text: (row.primary >= 0 && (row.primary & (1 << v))) ? '●' : '○',
            title: 'Valve ' + (v + 1)
          });
          if (row.primary >= 0 && (row.primary & (1 << v))) button.classList.add('on');
          button.addEventListener('click', () => {
            if (row.primary < 0) row.primary = 0;
            row.primary ^= (1 << v);
            const on = !!(row.primary & (1 << v));
            button.textContent = on ? '●' : '○';
            button.classList.toggle('on', on);
          });
          cells.push(UI.el('td', {}, [button]));
        }
        cells.push(UI.el('td', { text: row.alternate >= 0 ? maskText(row.alternate) : '—' }));
        const tr = UI.el('tr', {}, cells);
        // Outside the practical trumpet range, but still playable — shown
        // rather than hidden, so nothing looks like it silently disappeared.
        if (!row.inRange) tr.style.opacity = '.5';
        return tr;
      });

      table.appendChild(UI.el('table', {},
        [UI.el('thead', {}, [head]), UI.el('tbody', {}, rows)]));
    }

    async function saveTable() {
      if (!data) return;
      try {
        const result = await API.putFingering(data.notes.map((r) => ({
          written: r.written, primary: r.primary, alternate: r.alternate })));
        if (result.truncated) {
          UI.toast('Applied, but only ' + result.stored + ' edits fit in the configuration: '
                 + 'the rest will be lost at the next boot', 'bad');
        } else if (result.persisted === false) {
          UI.toast('Applied to ' + result.applied + ' notes, but the configuration could not '
                 + 'be written', 'bad');
        } else {
          UI.toast('Fingering applied and saved (' + result.applied + ' notes, '
                 + (result.stored || 0) + ' stored)', 'ok');
        }
      } catch (err) { UI.toast(err.message, 'bad'); }
    }

    function exportTable() {
      const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
      const link = UI.el('a', { href: URL.createObjectURL(blob), download: 'fingering.json' });
      link.click();
      URL.revokeObjectURL(link.href);
    }

    function importTable(event) {
      const file = event.target.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const parsed = JSON.parse(reader.result);
          if (!parsed.notes) throw new Error('no "notes" array in the file');
          data.notes = parsed.notes;
          draw();
          UI.toast('Table loaded — press Save to apply it', 'info');
        } catch (err) { UI.toast(err.message, 'bad'); }
      };
      reader.readAsText(file);
    }

    const fileInput = UI.el('input', { type: 'file', accept: '.json', style: 'display:none' });
    fileInput.addEventListener('change', importTable);

    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'Written pitch to valve combination. Click a circle to change it, then save: the '
          + 'notes that differ from the standard chart are stored in the configuration and '
          + 'reapplied at every boot.' }));
    wrap.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-bottom:10px' }, [
      UI.el('button', { class: 'btn small primary', text: 'Save to the instrument',
                        onclick: saveTable }),
      UI.el('button', { class: 'btn small', text: 'Reset to default',
        onclick: () => API.resetFingering().then(load).catch((e) => UI.toast(e.message, 'bad')) }),
      UI.el('button', { class: 'btn small', text: 'Export', onclick: exportTable }),
      UI.el('button', { class: 'btn small', text: 'Import', onclick: () => fileInput.click() }),
      fileInput
    ]));
    wrap.appendChild(table);
    load();
    return wrap;
  }

  function calibrationCard() {
    const v = cfg().valves;
    const body = [
      UI.el('div', { class: 'section-title', text: 'Audio' }),
      UI.el('div', { class: 'btn-row' },
        [100, 200, 500, 1000, 2000, 5000].map((hz) => UI.el('button', {
          class: 'btn small', text: hz >= 1000 ? (hz / 1000) + ' kHz' : hz + ' Hz',
          onclick: () => API.audioTest({ type: 'tone', frequency: hz, durationMs: 2000,
                                         amplitude: 0.2 })
        })).concat([
          UI.el('button', { class: 'btn small primary', text: 'Sweep 100 Hz → 8 kHz',
            onclick: () => API.audioTest({ type: 'sweep', startHz: 100, endHz: 8000,
                                           durationMs: 6000, amplitude: 0.2 }) }),
          UI.el('button', { class: 'btn small', text: 'Stop',
            onclick: () => API.audioTest({ type: 'stop' }) })
        ]))
    ];

    body.push(UI.el('div', { class: 'section-title', text: 'Servo travel' }));
    body.push(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'Moving a slider drives the servo immediately, so the linkage can be set by hand '
          + 'with the instrument in front of you.' }));

    for (let i = 0; i < v.count; i++) {
      const item = v.items[i];
      const path = 'valves.items.' + i + '.';
      if (item.type !== 'SERVO') {
        body.push(UI.el('div', {}, [
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
      body.push(UI.el('div', {}, [
        UI.el('div', { class: 'section-title', text: 'Valve ' + (i + 1) }),
        UI.el('div', { class: 'row' }, [
          UI.slider('Released', item.releasedAngle, 0, 180, 1, (x) => {
            set(path + 'releasedAngle', x); API.valveCalibrate(i, x);
          }, (x) => x + '°'),
          UI.slider('Pressed', item.pressedAngle, 0, 180, 1, (x) => {
            set(path + 'pressedAngle', x); API.valveCalibrate(i, x);
          }, (x) => x + '°')
        ]),
        UI.el('div', { class: 'btn-row' }, [
          UI.el('button', { class: 'btn small', text: 'Test released',
            onclick: () => API.valveCalibrate(i, item.releasedAngle) }),
          UI.el('button', { class: 'btn small', text: 'Test pressed',
            onclick: () => API.valveCalibrate(i, item.pressedAngle) })
        ])
      ]));
    }
    return UI.card('Calibration', body);
  }

  function validationCard() {
    const body = UI.el('div');
    async function run() {
      try {
        const r = await API.validate(cfg());
        UI.clear(body);
        body.appendChild(r.issues.length ? UI.issues(r.issues)
                                         : UI.badge('No problem found', 'ok'));
      } catch (err) { UI.toast(err.message, 'bad'); }
    }
    const card = UI.card('Validation', [
      UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Run before every save: duplicated GPIO, flash and strapping pins, board '
            + 'capabilities, speaker and amplifier compatibility, actuator safety.' }),
      body,
      UI.el('div', { class: 'btn-row', style: 'margin-top:10px' }, [
        UI.el('button', { class: 'btn small', text: 'Check again', onclick: run })
      ])
    ]);
    run();
    return card;
  }

  // =========================================================================
  // Wiring
  // =========================================================================
  const legendLine = (colour, label) => UI.el('span', {}, [
    UI.el('i', { style: 'background:' + colour }), document.createTextNode(label)
  ]);

  function wiring(host) {
    const c = cfg();
    const valves = c.valves.items.slice(0, c.valves.count);

    const diagram = UI.el('div');
    diagram.appendChild(Wiring.buildSvg(c));
    host.appendChild(UI.card('Harness', [
      UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Drawn from the configuration currently loaded — the real backend, the real GPIO, '
            + 'the real actuator per valve — so the picture cannot drift from the instrument.' }),
      diagram,
      UI.el('div', { class: 'legend' }, [
        legendLine('var(--w-i2s)', 'I²S'), legendLine('var(--w-audio)', 'audio chain'),
        legendLine('var(--w-i2c)', 'servo signal'), legendLine('var(--w-valve)', 'solenoid gate'),
        legendLine('var(--w-v24)', '24 V'), legendLine('var(--w-v5)', '5 V'),
        legendLine('var(--w-gnd)', 'GND'), legendLine('var(--w-3v3)', 'DIN MIDI')
      ]),
      UI.el('div', { class: 'btn-row', style: 'margin-top:14px' }, [
        UI.el('button', { class: 'btn', text: 'Download SVG',
                          onclick: () => Wiring.downloadSvg(c) })
      ])
    ], 'generated from your configuration'));

    host.appendChild(UI.card('Summary', [
      UI.el('div', { class: 'grid' }, [
        UI.stat('Audio backend', c.audio.backend),
        UI.stat('Amplifier', c.amplifier.type),
        UI.stat('Speaker', c.speaker.name),
        UI.stat('Valves', String(c.valves.count)),
        UI.stat('Servos', String(valves.filter((v) => v.type === 'SERVO').length)),
        UI.stat('Solenoids', String(valves.filter((v) => v.type === 'SOLENOID').length))
      ])
    ]));

    const audit = Wiring.audit(c);
    const rows = Wiring.CHECKS.filter((k) => Wiring.relevant(c, k)).map((check) => {
      const state = Wiring.declarations(c)[check.key] || 'unknown';
      return UI.el('div', { class: 'declare-row' }, [
        UI.el('div', {}, [
          UI.el('div', { class: 'what', text: check.label }),
          UI.el('div', { class: 'why', text: check.why })
        ]),
        UI.badge(state === 'unknown' ? 'unverified'
                 : state === 'yes' ? 'declared' : state === 'no' ? 'missing' : 'n/a',
                 state === 'yes' ? 'ok' : state === 'no' ? 'bad' : state === 'na' ? '' : 'warn'),
        UI.select(Wiring.STATES, state, (v) => {
          Wiring.declarations(cfg())[check.key] = v;
          App.markDirty();
          App.render();
        })
      ]);
    });

    host.appendChild(UI.card('Electrical protections', [
      UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'The firmware cannot see a diode, a fuse or a separate supply — it can only ask. '
            + 'Nothing here is assumed until you declare it, and what you declare missing is '
            + 'reported below rather than quietly ignored.' }),
      UI.el('div', {}, rows),
      audit.undeclared
        ? UI.el('div', { class: 'issues' }, [
            UI.el('div', { class: 'issue', 'data-sev': 'WARNING' }, [
              UI.el('span', { class: 'tag', text: 'UNVERIFIED' }),
              UI.el('div', { text: audit.undeclared + ' item(s) not checked yet. Until they are, '
                + 'this build cannot be called safe — go through them with the instrument in '
                + 'front of you.' })
            ])
          ])
        : null,
      audit.issues.length ? UI.issues(audit.issues) : null,
      !audit.undeclared && !audit.issues.length
        ? UI.el('div', { style: 'margin-top:12px' },
                [UI.badge('Every protection declared fitted', 'ok')])
        : null
    ]));

    host.appendChild(UI.card('Power', [
      UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Starting values, derived from the actuators configured. Size from the datasheet '
            + 'peak current and confirm at the bench: a powerful digital servo draws far more '
            + 'than a micro-servo.' }),
      UI.table([{ label: 'Rail' }, { label: 'Consumers', num: true },
                { label: 'Peak (estimate)', num: true }, { label: 'Bulk capacitor' }],
        Wiring.powerTable(c).map((r) => [r.rail, r.count, r.peak, r.bulk])),
      UI.el('ul', { class: 'help' }, [
        UI.el('li', { text: 'Never power a servo or a solenoid from the ESP32 regulator.' }),
        UI.el('li', { text: 'One common ground, star-wired back to the supply — audio and '
                          + 'actuator grounds must meet once, at the supply.' }),
        UI.el('li', { text: 'Keep the actuator wiring away from the I²S lines and the analogue '
                          + 'audio: servo noise in the DAC ground is the usual cause of a '
                          + 'buzzing build.' }),
        UI.el('li', { text: 'A hardware emergency stop cuts the actuator rails without going '
                          + 'through the firmware. The software STOP is a complement, never a '
                          + 'replacement.' })
      ])
    ]));
  }

  return { play, configure, wiring };
})();
