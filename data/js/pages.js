/* ===========================================================================
   pages.js - every page of the interface.
   The main pages stay deliberately simple; anything rare or dangerous lives on
   the Advanced page.
   =========================================================================== */
'use strict';

const Pages = (() => {

  // Shared state, filled by app.js
  const S = {
    config: null,      // working copy, edited by the forms
    saved: null,       // last version confirmed by the trumpet
    hardware: null,
    telemetry: null,
    dirty: false
  };

  const cfg = () => S.config;
  const markDirty = () => App.setDirty(true);

  function set(path, value) {
    const parts = path.split('.');
    let node = S.config;
    for (let i = 0; i < parts.length - 1; i++) node = node[parts[i]];
    node[parts[parts.length - 1]] = value;
    markDirty();
  }
  function get(path) {
    return path.split('.').reduce((node, key) => (node ? node[key] : undefined), S.config);
  }

  const opts = (values) => values.map((v) => ({ value: v, label: v }));

  // =========================================================================
  // Dashboard
  // =========================================================================
  function dashboard(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Dashboard' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'What the instrument is doing right now. Everything else lives in the side menu.' }));

    const noteCard = UI.card('Current note', [
      UI.el('div', { class: 'grid' }, [
        UI.stat('Note', '—', ''), UI.stat('Velocity', '0', ''), UI.stat('Frequency', '0', 'Hz')
      ])
    ]);
    const audioCard = UI.card('Audio', [
      UI.el('dl', { class: 'kv' }, []),
      UI.el('div', { class: 'field', style: 'margin-top:12px' }, [
        UI.el('label', { text: 'Output level' }),
        UI.el('div', { class: 'meter' }, [UI.el('i')])
      ])
    ]);
    const midiCard = UI.card('MIDI connections', [UI.el('div', { class: 'row' }, [])]);
    const valveCard = UI.card('Valves', [UI.el('div', { class: 'valves' }, [])]);
    const sysCard = UI.card('System', [UI.el('dl', { class: 'kv' }, [])]);

    host.appendChild(UI.el('div', { class: 'grid wide' },
      [noteCard, audioCard, valveCard, midiCard, sysCard]));

    const faults = UI.el('div', { class: 'card', hidden: true });
    host.appendChild(faults);

    host.updateLive = (t) => {
      if (!t) return;
      const stats = noteCard.querySelectorAll('.stat .v');
      const note = t.audio && t.audio.note ? t.audio.note : 0;
      stats[0].textContent = note ? UI.noteName(note) : '—';
      stats[1].textContent = t.audio ? t.audio.velocity : 0;
      stats[2].textContent = t.audio ? Math.round(t.audio.frequency) : 0;

      const dl = audioCard.querySelector('.kv');
      UI.clear(dl);
      const rows = [
        ['Backend', t.audio.backend],
        ['Sample rate', (t.audio.sampleRate || 0) + ' Hz'],
        ['State', t.audio.running ? (t.audio.muted ? 'Muted' : 'Playing') : 'Stopped'],
        ['Volume', Math.round((t.audio.volume || 0) * 100) + ' %'],
        ['Limiter', t.audio.gainReduction < 0.99
          ? ('-' + (20 * Math.log10(Math.max(t.audio.gainReduction, 0.001))).toFixed(1) + ' dB')
          : 'inactive'],
        ['Buffer underruns', t.audio.underruns],
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

      const midiRow = midiCard.querySelector('.row');
      UI.clear(midiRow);
      const links = [['USB', t.midi.usb], ['BLE', t.midi.ble], ['DIN', t.midi.din],
                     ['RTP', t.midi.rtp], ['Web', t.midi.web > 0]];
      for (const [name, connected] of links) {
        midiRow.appendChild(UI.badge(name, connected ? 'ok' : 'neutral'));
      }
      midiRow.appendChild(UI.el('span', { class: 'hint',
        text: t.midi.rx + ' msg/s in · ' + t.midi.tx + ' msg/s out' }));

      renderValves(valveCard.querySelector('.valves'), t.valves, false);

      const sys = sysCard.querySelector('.kv');
      UI.clear(sys);
      const sysRows = [
        ['Mode', t.mode],
        ['Uptime', formatUptime(t.uptime)],
        ['Valve mode', t.valveMode],
        ['Web clients', (S.telemetry && S.telemetry.midi ? S.telemetry.midi.web : 0)]
      ];
      for (const [k, v] of sysRows) {
        sys.appendChild(UI.el('dt', { text: k }));
        sys.appendChild(UI.el('dd', { text: String(v) }));
      }

      if (t.faults) {
        faults.hidden = false;
        UI.clear(faults);
        faults.appendChild(UI.el('h2', { text: 'Faults' }));
        faults.appendChild(UI.el('div', {}, [UI.badge(t.faultText || 'Fault active', 'bad')]));
        faults.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-top:10px' }, [
          UI.el('button', { class: 'btn', text: 'Clear and resume',
            onclick: () => API.releasePanic().then(() => UI.toast('Instrument released', 'ok')) })
        ]));
      } else {
        faults.hidden = true;
      }
    };
  }

  function formatUptime(seconds) {
    const s = seconds | 0;
    const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600);
    const m = Math.floor(s % 3600 / 60), sec = s % 60;
    if (d) return d + 'd ' + h + 'h';
    if (h) return h + 'h ' + m + 'm';
    if (m) return m + 'm ' + sec + 's';
    return sec + 's';
  }

  function renderValves(host, valves, interactive) {
    UI.clear(host);
    (valves || []).forEach((v, index) => {
      const node = UI.el('div', {
        class: 'valve' + (v.pressed ? ' pressed' : '') + (v.fault ? ' fault' : '')
      }, [
        UI.el('div', { class: 'piston' }),
        UI.el('div', { class: 'name', text: 'Valve ' + (index + 1) }),
        UI.el('div', { class: 'meta',
          text: v.type + (v.type === 'SERVO' ? ' · ' + Math.round(v.angle) + '°'
                                             : v.duty ? ' · ' + v.duty + ' %' : '') }),
        v.fault ? UI.badge('Fault', 'bad') : null,
        interactive ? UI.el('div', { class: 'btn-row', style: 'margin-top:10px;justify-content:center' }, [
          UI.el('button', { class: 'btn small', text: 'Test',
            onclick: () => API.valveTest(index, 350).catch((e) => UI.toast(e.message, 'bad')) })
        ]) : null
      ]);
      if (interactive) {
        node.addEventListener('pointerdown', () => WS.valve(index, true));
        node.addEventListener('pointerup', () => WS.valve(index, false));
        node.addEventListener('pointerleave', (e) => { if (e.buttons) WS.valve(index, false); });
      }
      host.appendChild(node);
    });
    if (!valves || !valves.length) {
      host.appendChild(UI.el('div', { class: 'hint', text: 'No valve configured.' }));
    }
  }

  // =========================================================================
  // Play
  // =========================================================================
  function play(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Play' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'The keyboard talks to the instrument over the WebSocket, so it reacts immediately.' }));

    const kbd = UI.el('div', { class: 'kbd' });
    const octaveLabel = UI.el('span', { class: 'badge plain', 'data-tone': 'neutral', text: 'C4' });

    const controls = UI.el('div', { class: 'ctrl-strip' }, [
      UI.slider('Velocity', 100, 1, 127, 1, (v) => Keyboard.setVelocity(v)),
      UI.slider('Pitch bend', 0, -8192, 8191, 1, (v) => WS.pitchBend(v)),
      UI.slider('Modulation (CC1)', 0, 0, 127, 1, (v) => WS.cc(1, v)),
      UI.slider('Breath (CC2)', 0, 0, 127, 1, (v) => WS.cc(2, v)),
      UI.slider('Expression (CC11)', 127, 0, 127, 1, (v) => WS.cc(11, v)),
      UI.slider('Volume (CC7)', 100, 0, 127, 1, (v) => WS.cc(7, v))
    ]);
    // Pitch bend must snap back when released, exactly like a real wheel.
    const bendInput = controls.children[1].querySelector('input');
    const releaseBend = () => {
      controls.children[1].setValue(0);
      WS.pitchBend(0);
    };
    bendInput.addEventListener('pointerup', releaseBend);
    bendInput.addEventListener('pointercancel', releaseBend);

    host.appendChild(UI.card('Keyboard', [
      UI.el('div', { class: 'btn-row', style: 'margin-bottom:12px;align-items:center' }, [
        UI.el('button', { class: 'btn small', text: 'Octave −',
          onclick: () => { octaveLabel.textContent = UI.noteName(Keyboard.shiftOctave(-1)); } }),
        octaveLabel,
        UI.el('button', { class: 'btn small', text: 'Octave +',
          onclick: () => { octaveLabel.textContent = UI.noteName(Keyboard.shiftOctave(1)); } }),
        UI.el('button', { class: 'btn small ghost', text: 'All notes off',
          onclick: () => { Keyboard.releaseAll(); WS.cc(123, 0); } })
      ]),
      UI.el('div', { class: 'kbd-wrap' }, [kbd])
    ]));
    host.appendChild(UI.card('Controllers', [controls]));

    const valveHost = UI.el('div', { class: 'valves' });
    host.appendChild(UI.card('Valves', [valveHost]));

    const range = cfg()
      ? { min: cfg().instrument.noteMin, max: cfg().instrument.noteMax }
      : { min: 0, max: 127 };
    Keyboard.build(kbd, { baseNote: 60, octaves: 2, range });
    octaveLabel.textContent = UI.noteName(Keyboard.baseNote());

    host.updateLive = (t) => { if (t) renderValves(valveHost, t.valves, false); };
    host.onLeave = () => Keyboard.releaseAll();
  }

  // =========================================================================
  // MIDI
  // =========================================================================
  function midi(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'MIDI' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'Which interfaces are active, and where their messages go.' }));

    const caps = S.hardware ? S.hardware.midi : {};
    const m = cfg().midi;

    const inputs = UI.card('MIDI inputs', [
      portToggle('USB', 'midi.usb.in', caps.usbAvailable,
        'Class compliant USB-MIDI. Requires a board with native USB.'),
      portToggle('BLE', 'midi.ble.in', caps.bleAvailable),
      portToggle('RTP-MIDI (Wi-Fi)', 'midi.rtp.in', caps.rtpAvailable),
      portToggle('DIN', 'midi.din.in', true),
      portToggle('Web UI', 'midi.web.in', true),
      UI.field('Channel', UI.select(
        [{ value: '65535', label: 'OMNI (all channels)' }].concat(
          Array.from({ length: 16 }, (_, i) => ({ value: String(1 << i), label: 'Channel ' + (i + 1) }))),
        String(m.channelMask), (v) => set('midi.channelMask', parseInt(v, 10))))
    ]);

    const outputs = UI.card('MIDI outputs', [
      portToggle('DIN OUT', 'midi.din.out', true),
      portToggle('DIN THRU', 'midi.din.thru', true,
        'Echoes every received byte straight back out, without interpreting it.'),
      portToggle('BLE OUT', 'midi.ble.out', caps.bleAvailable),
      portToggle('RTP-MIDI OUT', 'midi.rtp.out', caps.rtpAvailable),
      portToggle('USB OUT', 'midi.usb.out', caps.usbAvailable),
      UI.toggle('Suppress MIDI loops', m.suppressLoops,
        (v) => set('midi.suppressLoops', v))
    ]);

    host.appendChild(UI.el('div', { class: 'grid wide' }, [inputs, outputs]));
    host.appendChild(routingCard());

    const statusCard = UI.card('Live status', [UI.el('div', { class: 'table-wrap' })]);
    host.appendChild(statusCard);
    refreshMidiStatus(statusCard.querySelector('.table-wrap'));

    host.appendChild(monitorCard());
  }

  function portToggle(label, path, available, hint) {
    const value = !!get(path);
    if (!available) {
      return UI.el('div', { class: 'field' }, [
        UI.el('div', { class: 'row' }, [
          UI.toggle(label, false, () => {}, true),
          UI.badge('Not available on this board', 'neutral')
        ]),
        hint ? UI.el('div', { class: 'hint', text: hint }) : null
      ]);
    }
    return UI.el('div', { class: 'field' }, [
      UI.toggle(label, value, (v) => set(path, v)),
      hint ? UI.el('div', { class: 'hint', text: hint }) : null
    ]);
  }

  const ROUTE_SOURCES = ['USB', 'BLE', 'RTP', 'DIN', 'WEB'];
  const ROUTE_DESTS = ['SOUND_ENGINE', 'VALVE_ENGINE', 'USB', 'BLE', 'RTP', 'DIN'];
  const DEST_LABEL = {
    SOUND_ENGINE: 'Sound', VALVE_ENGINE: 'Valves',
    USB: 'USB out', BLE: 'BLE out', RTP: 'RTP out', DIN: 'DIN out'
  };

  function findRoute(source, destination) {
    return cfg().midi.routes.find((r) => r.source === source && r.destination === destination);
  }

  function routingCard() {
    const body = UI.el('div', { class: 'table-wrap' });
    const rebuild = () => {
      UI.clear(body);
      const head = UI.el('tr', {}, [UI.el('th', { text: 'Source' })].concat(
        ROUTE_DESTS.map((d) => UI.el('th', { text: DEST_LABEL[d] }))));
      const rows = ROUTE_SOURCES.map((source) => {
        const cells = [UI.el('td', { text: source })];
        for (const dest of ROUTE_DESTS) {
          const route = findRoute(source, dest);
          const input = UI.el('input', { type: 'checkbox' });
          input.checked = !!(route && route.enabled);
          input.addEventListener('change', () => {
            let target = findRoute(source, dest);
            if (!target) {
              target = {
                source, destination: dest, enabled: false, channelMask: 65535, transpose: 0,
                velocityCurve: 'LINEAR', fixedVelocity: 100, noteMin: 0, noteMax: 127
              };
              cfg().midi.routes.push(target);
            }
            target.enabled = input.checked;
            markDirty();
          });
          cells.push(UI.el('td', {}, [input]));
        }
        return UI.el('tr', {}, cells);
      });
      body.appendChild(UI.el('table', {}, [UI.el('thead', {}, [head]), UI.el('tbody', {}, rows)]));
    };
    rebuild();

    return UI.card('Routing matrix', [
      UI.el('p', { class: 'hint',
        text: 'Tick a cell to send everything a source receives to that destination. '
            + 'Per-route filters (channel, transpose, velocity curve, note range) are on the '
            + 'Advanced page.' }),
      body
    ]);
  }

  async function refreshMidiStatus(host) {
    try {
      const status = await API.midiStatus();
      UI.clear(host);
      host.appendChild(UI.table(
        ['Port', 'In', 'Out', 'State', { label: 'RX', num: true }, { label: 'TX', num: true }],
        status.ports.map((p) => [
          p.port,
          p.in ? 'yes' : '—',
          p.out ? 'yes' : '—',
          UI.badge(p.connected ? 'Connected' : 'Disconnected', p.connected ? 'ok' : 'neutral'),
          p.rx, p.tx
        ])));
      if (status.rtpPeer) {
        host.appendChild(UI.el('div', { class: 'hint', text: 'RTP-MIDI peer: ' + status.rtpPeer }));
      }
    } catch (err) {
      UI.clear(host);
      host.appendChild(UI.el('div', { class: 'hint', text: err.message }));
    }
  }

  function monitorCard() {
    const rows = [];
    const body = UI.el('tbody');
    let paused = false;

    WS.on('monitor', (message) => {
      if (paused) return;
      for (const item of message.items) {
        rows.push(item);
        if (rows.length > 200) rows.shift();
      }
      redraw();
    });

    function redraw() {
      UI.clear(body);
      for (const r of rows.slice(-120).reverse()) {
        body.appendChild(UI.el('tr', {}, [
          UI.el('td', { text: (r.ts / 1000).toFixed(2) }),
          UI.el('td', { text: r.src }),
          UI.el('td', { text: r.type }),
          UI.el('td', { class: 'num', text: String(r.ch) }),
          UI.el('td', { class: 'num', text: String(r.d1) }),
          UI.el('td', { class: 'num', text: String(r.d2) })
        ]));
      }
    }

    const table = UI.el('table', {}, [
      UI.el('thead', {}, [UI.el('tr', {}, ['Time', 'Source', 'Type', 'Ch', 'Data 1', 'Data 2']
        .map((h) => UI.el('th', { text: h })))]),
      body
    ]);

    return UI.card('MIDI monitor', [
      UI.el('div', { class: 'btn-row', style: 'margin-bottom:10px' }, [
        UI.el('button', { class: 'btn small', text: 'Pause', onclick: (e) => {
          paused = !paused;
          e.target.textContent = paused ? 'Resume' : 'Pause';
          WS.monitor({ paused });
        } }),
        UI.el('button', { class: 'btn small ghost', text: 'Clear', onclick: () => {
          rows.length = 0; redraw(); WS.monitor({ clear: true });
        } })
      ]),
      UI.el('div', { class: 'mon' }, [UI.el('div', { class: 'table-wrap' }, [table])])
    ], UI.el('span', { class: 'hint', text: 'Bounded to 128 entries on the device.' }));
  }

  // =========================================================================
  // Instrument
  // =========================================================================
  function instrument(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Instrument' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'Transposition, note priority and articulation.' }));

    const i = cfg().instrument;
    host.appendChild(UI.el('div', { class: 'grid wide' }, [
      UI.card('Pitch', [
        UI.field('Instrument', UI.select(
          opts(['BB_TRUMPET', 'C_TRUMPET', 'EB_TRUMPET', 'CUSTOM']), i.type,
          (v) => { set('instrument.type', v); App.rerender(); }),
          'A Bb trumpet sounds a whole tone below the written note.'),
        i.type === 'CUSTOM'
          ? UI.field('Custom transposition (semitones)',
              UI.number(i.customTranspose, (v) => set('instrument.customTranspose', v),
                        { min: -24, max: 24, step: 1 }))
          : null,
        UI.field('Incoming MIDI is', UI.select([
          { value: 'CONCERT', label: 'Concert pitch (what you hear)' },
          { value: 'WRITTEN', label: 'Written pitch (what the player reads)' }
        ], i.pitchMode, (v) => set('instrument.pitchMode', v)),
          'Only affects which fingering is chosen and which pitch is produced.'),
        UI.field('Pitch bend range', UI.select(
          [1, 2, 3, 12].map((n) => ({ value: String(n), label: '± ' + n + ' semitones' })),
          String(cfg().audio.pitchBendRange),
          (v) => set('audio.pitchBendRange', parseInt(v, 10))))
      ]),
      UI.card('Playing style', [
        UI.field('Note priority', UI.select([
          { value: 'LAST', label: 'Last note (default)' },
          { value: 'HIGH', label: 'Highest note' },
          { value: 'LOW', label: 'Lowest note' }
        ], i.notePriority, (v) => set('instrument.notePriority', v)),
          'A trumpet is monophonic: this decides which note wins when several overlap.'),
        UI.toggle('Legato (slur overlapping notes)', i.legato, (v) => set('instrument.legato', v)),
        UI.toggle('Retrigger the envelope on every note', i.retrigger,
          (v) => set('instrument.retrigger', v)),
        UI.slider('Portamento', i.portamentoMs, 0, 400, 5,
          (v) => set('instrument.portamentoMs', v), (v) => v + ' ms')
      ]),
      UI.card('Range', [
        UI.field('Lowest note', UI.number(i.noteMin, (v) => set('instrument.noteMin', v),
          { min: 0, max: 127 }), UI.noteName(i.noteMin)),
        UI.field('Highest note', UI.number(i.noteMax, (v) => set('instrument.noteMax', v),
          { min: 0, max: 127 }), UI.noteName(i.noteMax))
      ])
    ]));

    host.appendChild(fingeringCard());
  }

  function fingeringCard() {
    const body = UI.el('div', { class: 'table-wrap' });
    const card = UI.card('Fingering table', [
      UI.el('p', { class: 'hint',
        text: 'Written pitch to valve combination. Click a circle to change it.' }),
      UI.el('div', { class: 'btn-row', style: 'margin:10px 0' }, [
        UI.el('button', { class: 'btn small', text: 'Save to the instrument', onclick: saveTable }),
        UI.el('button', { class: 'btn small ghost', text: 'Reset to default',
          onclick: () => API.resetFingering().then(load) }),
        UI.el('button', { class: 'btn small ghost', text: 'Export', onclick: exportTable }),
        UI.el('label', { class: 'btn small ghost' }, [
          document.createTextNode('Import'),
          (() => {
            const input = UI.el('input', { type: 'file', accept: '.json', style: 'display:none' });
            input.addEventListener('change', importTable);
            return input;
          })()
        ])
      ]),
      body
    ]);

    let data = null;

    async function load() {
      try {
        data = await API.fingering();
        render();
      } catch (err) {
        UI.clear(body);
        body.appendChild(UI.el('div', { class: 'hint', text: err.message }));
      }
    }

    function render() {
      UI.clear(body);
      const valveCount = data.valveCount || 3;
      const head = UI.el('tr', {}, [UI.el('th', { text: 'Note' })].concat(
        Array.from({ length: valveCount }, (_, i) => UI.el('th', { text: 'V' + (i + 1) })))
        .concat([UI.el('th', { text: 'Alternate' })]));

      const rows = data.notes.map((row) => {
        const cells = [UI.el('td', { text: UI.noteName(row.written) })];
        for (let v = 0; v < valveCount; v++) {
          const on = row.primary >= 0 && (row.primary & (1 << v)) !== 0;
          const button = UI.el('button', {
            class: 'btn small ghost', text: on ? '●' : '○',
            title: 'Valve ' + (v + 1)
          });
          button.addEventListener('click', () => {
            if (row.primary < 0) row.primary = 0;
            row.primary ^= (1 << v);
            button.textContent = (row.primary & (1 << v)) ? '●' : '○';
            card.dataset.dirty = '1';
          });
          cells.push(UI.el('td', {}, [button]));
        }
        cells.push(UI.el('td', { text: row.alternate >= 0 ? maskText(row.alternate) : '—' }));
        const tr = UI.el('tr', {}, cells);
        if (!row.inRange) tr.style.opacity = '.5';
        return tr;
      });

      body.appendChild(UI.el('table', {},
        [UI.el('thead', {}, [head]), UI.el('tbody', {}, rows)]));
      body.appendChild(UI.el('div', { class: 'hint',
        text: 'Greyed rows are outside the practical trumpet range but still usable. '
            + 'The table lives in RAM and is rebuilt at every boot (persisting it is planned '
            + 'for schema v3).' }));
    }

    const maskText = (mask) => mask === 0 ? 'open'
      : [1, 2, 4, 8].map((bit, i) => (mask & bit) ? String(i + 1) : '').join('');

    async function saveTable() {
      if (!data) return;
      try {
        const result = await API.putFingering(data.notes.map((r) => ({
          written: r.written, primary: r.primary, alternate: r.alternate
        })));
        UI.toast('Fingering applied (' + result.applied + ' notes)', 'ok');
        card.dataset.dirty = '';
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
          render();
          UI.toast('Table loaded, press Save to apply it', 'info');
        } catch (err) { UI.toast(err.message, 'bad'); }
      };
      reader.readAsText(file);
    }

    load();
    return card;
  }

  // =========================================================================
  // Audio
  // =========================================================================
  function audio(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Audio' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'How the sound is generated and where it goes.' }));

    const a = cfg().audio;
    host.appendChild(UI.el('div', { class: 'grid wide' }, [
      UI.card('Sound engine', [
        UI.field('Generator', UI.select([
          { value: 'ADDITIVE', label: 'Additive (recommended for a trumpet)' },
          { value: 'WAVETABLE', label: 'Wavetable (cheaper on CPU)' },
          { value: 'HYBRID', label: 'Hybrid (additive + wavetable)' },
          { value: 'SINE', label: 'Sine (test tone)' }
        ], a.engine, (v) => set('audio.engine', v))),
        UI.slider('Master volume', Math.round(a.masterVolume * 100), 0, 100, 1,
          (v) => { set('audio.masterVolume', v / 100); API.audioVolume(v / 100); },
          (v) => v + ' %'),
        UI.slider('Harmonics', a.additive.harmonicCount, 1, 16, 1,
          (v) => set('audio.additive.harmonicCount', v)),
        UI.slider('Brightness follows velocity', Math.round(a.additive.velocityBrightness * 100),
          0, 100, 1, (v) => set('audio.additive.velocityBrightness', v / 100), (v) => v + ' %'),
        UI.slider('Brightness follows breath (CC2)', Math.round(a.additive.breathBrightness * 100),
          0, 100, 1, (v) => set('audio.additive.breathBrightness', v / 100), (v) => v + ' %')
      ]),
      UI.card('Envelope', [
        UI.slider('Attack', a.envelope.attackMs, 1, 200, 1,
          (v) => set('audio.envelope.attackMs', v), (v) => v + ' ms'),
        UI.slider('Decay', a.envelope.decayMs, 1, 500, 1,
          (v) => set('audio.envelope.decayMs', v), (v) => v + ' ms'),
        UI.slider('Sustain', Math.round(a.envelope.sustain * 100), 0, 100, 1,
          (v) => set('audio.envelope.sustain', v / 100), (v) => v + ' %'),
        UI.slider('Release', a.envelope.releaseMs, 1, 800, 1,
          (v) => set('audio.envelope.releaseMs', v), (v) => v + ' ms'),
        UI.slider('Attack noise', Math.round(a.envelope.attackNoise * 100), 0, 60, 1,
          (v) => set('audio.envelope.attackNoise', v / 100), (v) => v + ' %'),
        UI.slider('Breath noise', Math.round(a.envelope.breathNoise * 100), 0, 30, 1,
          (v) => set('audio.envelope.breathNoise', v / 100), (v) => v + ' %')
      ]),
      UI.card('Vibrato', [
        UI.field('Source', UI.select([
          { value: 'CC1', label: 'Modulation wheel (CC1)' },
          { value: 'AFTERTOUCH', label: 'Channel pressure' },
          { value: 'AUTOMATIC', label: 'Always on' },
          { value: 'OFF', label: 'Off' }
        ], a.vibrato.source, (v) => set('audio.vibrato.source', v))),
        UI.slider('Rate', a.vibrato.frequencyHz, 1, 12, 0.1,
          (v) => set('audio.vibrato.frequencyHz', v), (v) => v.toFixed(1) + ' Hz'),
        UI.slider('Depth', a.vibrato.depthCents, 0, 100, 1,
          (v) => set('audio.vibrato.depthCents', v), (v) => v + ' cents'),
        UI.slider('Delay', a.vibrato.delayMs, 0, 1000, 10,
          (v) => set('audio.vibrato.delayMs', v), (v) => v + ' ms'),
        UI.slider('Fade in', a.vibrato.fadeInMs, 0, 1500, 10,
          (v) => set('audio.vibrato.fadeInMs', v), (v) => v + ' ms')
      ])
    ]));

    host.appendChild(hardwareChainCard());
  }

  function hardwareChainCard() {
    const hw = S.hardware || { audioBackends: [], amplifiers: [], speakers: [] };
    const a = cfg().audio;

    const backendOptions = hw.audioBackends.map((b) => ({
      value: b.key,
      label: b.title + (b.available ? (b.maturity === 'STABLE' ? '' : ' — ' + b.maturity.toLowerCase())
                                    : ' (not available on this board)'),
      disabled: !b.available
    }));
    const current = hw.audioBackends.find((b) => b.key === a.backend);

    return UI.card('Output chain', [
      UI.field('Audio backend', UI.select(backendOptions, a.backend,
        (v) => { set('audio.backend', v); App.rerender(); }),
        current ? current.summary : ''),
      current && current.maturity !== 'STABLE'
        ? UI.el('div', { class: 'issues' }, [
            UI.el('div', { class: 'issue', 'data-sev': 'WARNING' }, [
              UI.el('span', { class: 'tag', text: 'WARNING' }),
              UI.el('div', { text: current.maturity === 'PROTOTYPE'
                ? 'This backend is prototype quality by design and is not meant for playing.'
                : 'This backend is implemented but has not been validated on hardware by the '
                  + 'project. Report what you find.' })
            ])
          ])
        : null,
      UI.field('Amplifier', UI.select(
        hw.amplifiers.map((x) => ({ value: x.key, label: x.key })),
        cfg().amplifier.type, (v) => {
          const preset = hw.amplifiers.find((x) => x.key === v);
          set('amplifier.type', v);
          if (preset && v !== 'CUSTOM') {
            set('amplifier.maxPower', preset.maxPower);
            set('amplifier.gainDb', preset.gainDb);
            set('amplifier.speakerImpedance', preset.impedance);
          }
          App.rerender();
        })),
      UI.field('Speaker', UI.select(
        hw.speakers.map((x) => ({ value: x.key, label: x.name })),
        cfg().speaker.profile, (v) => {
          const preset = hw.speakers.find((x) => x.key === v);
          set('speaker.profile', v);
          if (preset && v !== 'CUSTOM') {
            set('speaker.name', preset.name);
            set('speaker.impedance', preset.impedance);
            set('speaker.powerRms', preset.powerRms);
            set('speaker.recommendedHighPass', preset.recommendedHighPass);
            set('speaker.powerLimit', preset.powerLimit);
          }
          App.rerender();
        })),
      cfg().speaker.profile === 'CUSTOM' ? customSpeakerFields() : null,
      UI.field('Acoustic coupling', UI.select([
        { value: 'SEALED_CHAMBER', label: 'Sealed chamber into the leadpipe (reference)' },
        { value: 'OPEN', label: 'Open (speaker in free air)' },
        { value: 'CUSTOM_CHAMBER', label: 'Custom chamber' }
      ], cfg().acoustic.coupling, (v) => set('acoustic.coupling', v)),
        'The speaker is used as a pressure source feeding the trumpet, not as a loudspeaker box.'),
      UI.field('Speaker protection', UI.el('div', {}, [
        UI.toggle('Software limiter enabled', cfg().audio.limiter.enabled,
          (v) => set('audio.limiter.enabled', v)),
        UI.el('div', { class: 'hint',
          text: 'The safe peak level is derived from the speaker power, its impedance and the '
              + 'amplifier rating. It cannot be disabled: only the soft limiter can.' })
      ]))
    ]);
  }

  function customSpeakerFields() {
    const s = cfg().speaker;
    return UI.el('div', { class: 'card', style: 'background:var(--card-2);margin-bottom:14px' }, [
      UI.el('h3', { text: 'Custom speaker' }),
      UI.field('Name', UI.text(s.name, (v) => set('speaker.name', v), { maxlength: 31 })),
      UI.el('div', { class: 'row' }, [
        UI.field('Impedance (ohm)', UI.number(s.impedance, (v) => set('speaker.impedance', v),
          { min: 2, max: 32, step: 0.1 })),
        UI.field('RMS power (W)', UI.number(s.powerRms, (v) => set('speaker.powerRms', v),
          { min: 0.5, max: 200, step: 0.5 }))
      ]),
      UI.el('div', { class: 'row' }, [
        UI.field('Minimum frequency (Hz)', UI.number(s.minFrequency,
          (v) => set('speaker.minFrequency', v), { min: 20, max: 2000 })),
        UI.field('Maximum frequency (Hz)', UI.number(s.maxFrequency,
          (v) => set('speaker.maxFrequency', v), { min: 1000, max: 30000 }))
      ]),
      UI.el('div', { class: 'row' }, [
        UI.field('Recommended high pass (Hz)', UI.number(s.recommendedHighPass,
          (v) => set('speaker.recommendedHighPass', v), { min: 0, max: 2000 })),
        UI.field('Maximum power allowed (W)', UI.number(s.powerLimit,
          (v) => set('speaker.powerLimit', v), { min: 0.1, max: 200, step: 0.1 }))
      ])
    ]);
  }

  // =========================================================================
  // Pistons
  // =========================================================================
  function pistons(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Pistons' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'One to four valves, each independently a servo or a solenoid.' }));

    const v = cfg().valves;
    const liveHost = UI.el('div', { class: 'valves' });

    host.appendChild(UI.card('Live state', [
      UI.el('div', { class: 'field' }, [
        UI.el('label', { text: 'Mode' }),
        UI.select([
          { value: 'AUTO', label: 'AUTO — follow the MIDI notes' },
          { value: 'MANUAL', label: 'MANUAL — press the valves from here' },
          { value: 'MIDI_CC', label: 'MIDI_CC — one controller per valve' },
          { value: 'DISABLED', label: 'DISABLED — valves parked' }
        ], v.mode, (value) => {
          set('valves.mode', value);
          API.valveMode(value).catch((e) => UI.toast(e.message, 'bad'));
          App.rerender();
        })
      ]),
      v.mode === 'MANUAL'
        ? UI.el('p', { class: 'hint', text: 'Press and hold a valve to actuate it.' })
        : null,
      liveHost
    ]));

    host.appendChild(UI.card('Configuration', [
      UI.field('Number of valves', UI.select(
        [1, 2, 3, 4].map((n) => ({ value: String(n), label: String(n) })), String(v.count),
        (value) => { set('valves.count', parseInt(value, 10)); App.rerender(); })),
      UI.el('div', { class: 'grid' }, v.items.slice(0, v.count).map((item, index) =>
        valveEditor(item, index)))
    ]));

    host.updateLive = (t) => { if (t) renderValves(liveHost, t.valves, v.mode === 'MANUAL'); };
  }

  function valveEditor(item, index) {
    const path = 'valves.items.' + index + '.';
    const body = [
      UI.field('Actuator', UI.select([
        { value: 'SERVO', label: 'Servo' },
        { value: 'SOLENOID', label: 'Solenoid' },
        { value: 'DISABLED', label: 'Not fitted' }
      ], item.type, (v) => { set(path + 'type', v); App.rerender(); }))
    ];

    if (item.type === 'SERVO') {
      body.push(UI.field('Driver', UI.select([
        { value: 'ESP32_PWM', label: 'ESP32 PWM (direct GPIO)' },
        { value: 'PCA9685', label: 'PCA9685 expander' }
      ], item.driver, (v) => { set(path + 'driver', v); App.rerender(); })));
      if (item.driver === 'PCA9685') {
        body.push(UI.field('Channel', UI.number(item.channel,
          (v) => set(path + 'channel', v), { min: 0, max: 15 })));
      } else {
        body.push(UI.field('GPIO', UI.number(item.gpio, (v) => set(path + 'gpio', v),
          { min: -1, max: 48 })));
      }
      body.push(UI.slider('Released angle', item.releasedAngle, 0, 180, 1,
        (v) => set(path + 'releasedAngle', v), (v) => v + '°'));
      body.push(UI.slider('Pressed angle', item.pressedAngle, 0, 180, 1,
        (v) => set(path + 'pressedAngle', v), (v) => v + '°'));
      body.push(UI.el('div', { class: 'hint',
        text: 'Use the Calibration page to move the servo while you adjust the linkage.' }));
    } else if (item.type === 'SOLENOID') {
      body.push(UI.field('GPIO (MOSFET gate)', UI.number(item.gpio,
        (v) => set(path + 'gpio', v), { min: -1, max: 48 })));
      body.push(UI.slider('Pull-in level', item.pullInPwm, 10, 100, 1,
        (v) => set(path + 'pullInPwm', v), (v) => v + ' %'));
      body.push(UI.slider('Pull-in time', item.pullInMs, 5, 300, 5,
        (v) => set(path + 'pullInMs', v), (v) => v + ' ms'));
      body.push(UI.slider('Hold level', item.holdPwm, 0, 100, 1,
        (v) => set(path + 'holdPwm', v), (v) => v + ' %'));
      body.push(UI.slider('Maximum continuous ON', item.maxOnMs, 200, 20000, 100,
        (v) => set(path + 'maxOnMs', v), (v) => (v / 1000).toFixed(1) + ' s'));
      body.push(UI.el('div', { class: 'hint',
        text: 'The thermal guard is mandatory: past this time the coil is released and a fault '
            + 'is raised, even if the MIDI note never ends.' }));
    }

    if (cfg().valves.mode === 'MIDI_CC') {
      body.push(UI.field('Controller number', UI.number(item.cc,
        (v) => set(path + 'cc', v), { min: 0, max: 127 })));
    }

    body.push(UI.el('div', { class: 'btn-row', style: 'margin-top:10px' }, [
      UI.el('button', { class: 'btn small', text: 'Test pulse',
        onclick: () => API.valveTest(index, 350).catch((e) => UI.toast(e.message, 'bad')) })
    ]));

    return UI.card('Valve ' + (index + 1), body);
  }

  // =========================================================================
  // Hardware
  // =========================================================================
  function hardware(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Hardware' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'Pick a documented bundle, or configure each element yourself.' }));

    const hw = S.hardware;
    if (!hw) { host.appendChild(UI.el('div', { class: 'hint', text: 'Loading…' })); return; }

    host.appendChild(UI.card('Board', [
      UI.el('dl', { class: 'kv' }, [
        UI.el('dt', { text: 'Chip' }), UI.el('dd', { text: hw.board.chip }),
        UI.el('dt', { text: 'Cores' }), UI.el('dd', { text: String(hw.board.cores) }),
        UI.el('dt', { text: 'Flash' }),
        UI.el('dd', { text: (hw.board.flash / 1048576).toFixed(1) + ' MB' }),
        UI.el('dt', { text: 'PSRAM' }),
        UI.el('dd', { text: hw.board.psram ? (hw.board.psram / 1048576).toFixed(1) + ' MB' : 'none' }),
        UI.el('dt', { text: 'Native USB' }),
        UI.el('dd', {}, [UI.badge(hw.board.hasNativeUsb ? 'yes' : 'no',
          hw.board.hasNativeUsb ? 'ok' : 'neutral')]),
        UI.el('dt', { text: 'Internal DAC' }),
        UI.el('dd', {}, [UI.badge(hw.board.hasInternalDac ? 'yes' : 'no',
          hw.board.hasInternalDac ? 'ok' : 'neutral')]),
        UI.el('dt', { text: 'BLE' }),
        UI.el('dd', {}, [UI.badge(hw.board.hasBle ? 'yes' : 'no',
          hw.board.hasBle ? 'ok' : 'neutral')])
      ])
    ]));

    host.appendChild(UI.card('Presets', [
      UI.el('div', { class: 'presets' }, hw.presets.map((p) => presetCard(p)))
    ]));

    host.appendChild(hardwareChainCard());
    host.appendChild(validationCard());
  }

  function presetCard(p) {
    const selected = cfg().system.preset === p.key;
    return UI.el('div', {
      class: 'preset' + (selected ? ' selected' : '') + (p.available ? '' : ' disabled'),
      onclick: async () => {
        try {
          const result = await API.applyPreset(p.key);
          if (!result.ok) {
            UI.toast('The preset was refused: see the issues below', 'bad');
          } else {
            UI.toast('Preset "' + p.title + '" applied — reboot to use it', 'ok');
            await App.reload();
          }
        } catch (err) { UI.toast(err.message, 'bad'); }
      }
    }, [
      p.recommended ? UI.el('div', { class: 'reco', text: 'RECOMMENDED' }) : null,
      UI.el('div', { class: 't', text: p.title }),
      UI.el('div', { class: 's', text: p.summary }),
      p.stars ? UI.el('div', { class: 'stars', text: UI.stars(p.stars) }) : null
    ]);
  }

  function validationCard() {
    const body = UI.el('div');
    const card = UI.card('Validation', [
      UI.el('p', { class: 'hint',
        text: 'Checked before every save: duplicated GPIO, flash and strapping pins, '
            + 'board capabilities, speaker/amplifier compatibility, actuator safety.' }),
      UI.el('div', { class: 'btn-row', style: 'margin-bottom:10px' }, [
        UI.el('button', { class: 'btn small', text: 'Check this configuration', onclick: run })
      ]),
      body
    ]);

    async function run() {
      try {
        const result = await API.validate(cfg());
        UI.clear(body);
        if (!result.issues.length) {
          body.appendChild(UI.badge('No problem found', 'ok'));
        } else {
          body.appendChild(UI.issues(result.issues));
        }
      } catch (err) { UI.toast(err.message, 'bad'); }
    }
    run();
    return card;
  }

  // =========================================================================
  // Calibration
  // =========================================================================
  function calibration(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Calibration' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'Adjust the mechanics and the sound with the instrument in front of you.' }));

    const v = cfg().valves;
    host.appendChild(UI.card('Servo calibration', [
      UI.el('p', { class: 'hint',
        text: 'Moving a slider drives the servo immediately so you can set the linkage by hand.' }),
      UI.el('div', { class: 'grid' },
        v.items.slice(0, v.count).map((item, index) => servoCalibrator(item, index)))
    ]));

    host.appendChild(UI.card('Audio calibration', [
      UI.el('p', { class: 'hint',
        text: 'Play a tone through the whole chain: DAC, amplifier, speaker and chamber.' }),
      UI.el('div', { class: 'btn-row' },
        [100, 200, 500, 1000, 2000, 5000].map((hz) =>
          UI.el('button', {
            class: 'btn small', text: hz >= 1000 ? (hz / 1000) + ' kHz' : hz + ' Hz',
            onclick: () => API.audioTest({ type: 'tone', frequency: hz, durationMs: 2000,
                                           amplitude: 0.2 })
          }))
        .concat([
          UI.el('button', { class: 'btn small primary', text: 'Sweep 100 Hz → 8 kHz',
            onclick: () => API.audioTest({ type: 'sweep', startHz: 100, endHz: 8000,
                                           durationMs: 6000, amplitude: 0.2 }) }),
          UI.el('button', { class: 'btn small ghost', text: 'Stop',
            onclick: () => API.audioTest({ type: 'stop' }) })
        ])),
      UI.el('div', { class: 'field', style: 'margin-top:16px' }, [
        UI.el('label', { text: 'Output level' }),
        UI.el('div', { class: 'meter' }, [UI.el('i')])
      ]),
      UI.el('h3', { text: 'Voicing' }),
      UI.slider('High pass', cfg().audio.highPassHz, 40, 600, 5,
        (value) => set('audio.highPassHz', value), (value) => value + ' Hz'),
      UI.el('div', { class: 'hint',
        text: 'The strictest of this value, the speaker recommendation and the chamber corner '
            + 'is the one actually applied.' }),
      eqEditor()
    ]));

    host.appendChild(UI.card('Hardware test', [
      UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn small', text: 'Mute',
          onclick: () => API.audioMute(true) }),
        UI.el('button', { class: 'btn small', text: 'Unmute',
          onclick: () => API.audioMute(false) })
      ].concat(v.items.slice(0, v.count).map((item, index) =>
        UI.el('button', { class: 'btn small', text: 'Pulse valve ' + (index + 1),
          onclick: () => API.valveTest(index, 350).catch((e) => UI.toast(e.message, 'bad')) }))))
    ]));

    host.appendChild(UI.card('Microphone calibration', [
      UI.el('p', { class: 'hint',
        text: 'Planned for the ES8388 and WM8960 backends: a measurement microphone on the codec '
            + 'ADC, a frequency sweep, then an automatic EQ correction. The capture path is '
            + 'already opened by those backends; the measurement itself is not implemented in '
            + 'this version, so nothing on this page pretends to perform it.' })
    ]));

    host.updateLive = (t) => {
      if (!t || !t.audio) return;
      const meter = host.querySelector('.meter');
      if (!meter) return;
      const peak = Math.min(1, t.audio.peak || 0);
      meter.querySelector('i').style.width = (peak * 100).toFixed(1) + '%';
      meter.className = 'meter' + (peak > 0.95 ? ' clip' : peak > 0.75 ? ' hot' : '');
    };
  }

  function servoCalibrator(item, index) {
    if (item.type !== 'SERVO') {
      return UI.card('Valve ' + (index + 1), [
        UI.el('div', { class: 'hint', text: item.type === 'SOLENOID'
          ? 'Solenoids have no angle to calibrate; use the pull-in and hold levels on the '
            + 'Pistons page.'
          : 'Not fitted.' })
      ]);
    }
    const path = 'valves.items.' + index + '.';
    return UI.card('Valve ' + (index + 1), [
      UI.slider('Released', item.releasedAngle, 0, 180, 1, (v) => {
        set(path + 'releasedAngle', v);
        API.valveCalibrate(index, v);
      }, (v) => v + '°'),
      UI.slider('Pressed', item.pressedAngle, 0, 180, 1, (v) => {
        set(path + 'pressedAngle', v);
        API.valveCalibrate(index, v);
      }, (v) => v + '°'),
      UI.slider('Speed', item.speed, 60, 3000, 20, (v) => set(path + 'speed', v),
        (v) => v + ' °/s'),
      UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn small', text: 'Test released',
          onclick: () => API.valveCalibrate(index, get(path + 'releasedAngle')) }),
        UI.el('button', { class: 'btn small', text: 'Test pressed',
          onclick: () => API.valveCalibrate(index, get(path + 'pressedAngle')) })
      ]),
      UI.toggle('Invert direction', item.invert, (v) => set(path + 'invert', v)),
      UI.toggle('Detach after the movement', item.detachAfterMove,
        (v) => set(path + 'detachAfterMove', v))
    ]);
  }

  function eqEditor() {
    const bands = cfg().audio.eq;
    return UI.el('div', {}, [UI.el('h3', { text: 'Equaliser' })].concat(
      bands.map((band, index) => UI.el('div', { class: 'card',
        style: 'background:var(--card-2);margin-bottom:10px' }, [
        UI.toggle('Band ' + (index + 1), band.enabled,
          (v) => set('audio.eq.' + index + '.enabled', v)),
        UI.el('div', { class: 'row' }, [
          UI.field('Frequency', UI.number(band.frequency,
            (v) => set('audio.eq.' + index + '.frequency', v), { min: 30, max: 16000 })),
          UI.field('Gain (dB)', UI.number(band.gainDb,
            (v) => set('audio.eq.' + index + '.gainDb', v), { min: -18, max: 18, step: 0.5 })),
          UI.field('Q', UI.number(band.q, (v) => set('audio.eq.' + index + '.q', v),
            { min: 0.2, max: 8, step: 0.1 }))
        ])
      ]))));
  }

  // =========================================================================
  // Diagnostics
  // =========================================================================
  function diagnostics(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Diagnostics' }));
    host.appendChild(UI.el('p', { class: 'sub', text: 'Everything the firmware knows about itself.' }));

    const body = UI.el('div', { class: 'grid wide' });
    host.appendChild(body);

    async function refresh() {
      try {
        const d = await API.diagnostics();
        UI.clear(body);
        body.appendChild(UI.card('Firmware', kv([
          ['Version', d.firmware], ['Board', d.board], ['Cores', d.cores],
          ['Uptime', formatUptime(d.uptime)], ['Reset reason', d.resetReason],
          ['Sketch size', (d.sketchSize / 1024).toFixed(0) + ' KB'],
          ['Free sketch space', (d.freeSketchSpace / 1024).toFixed(0) + ' KB'],
          ['OTA', d.otaAvailable ? 'available' : 'unavailable']
        ])));
        body.appendChild(UI.card('Memory', kv([
          ['Free heap', (d.freeHeap / 1024).toFixed(1) + ' KB'],
          ['Lowest free heap', (d.minFreeHeap / 1024).toFixed(1) + ' KB'],
          ['PSRAM', d.psram ? (d.psram / 1048576).toFixed(1) + ' MB' : 'none'],
          ['Free PSRAM', d.freePsram ? (d.freePsram / 1048576).toFixed(1) + ' MB' : '—'],
          ['Flash', (d.flash / 1048576).toFixed(0) + ' MB']
        ])));
        body.appendChild(UI.card('Audio', kv([
          ['Backend', d.audioBackend], ['Maturity', d.audioMaturity],
          ['Sample rate', d.audioSampleRate + ' Hz'],
          ['Blocks rendered', d.audioBlocks],
          ['Underruns', d.audioUnderruns],
          ['Audio CPU', (d.cpuLoad || 0).toFixed(1) + ' %']
        ])));
        body.appendChild(UI.card('MIDI', kv([
          ['Received', d.midiRx], ['Sent', d.midiTx],
          ['Rate in', d.midiRxPerSecond + ' /s'], ['Rate out', d.midiTxPerSecond + ' /s'],
          ['Dropped by filters', d.router.droppedByFilter],
          ['Loops suppressed', d.router.loopsSuppressed],
          ['Monitor overflow', d.monitorOverflow]
        ])));
        body.appendChild(UI.card('Network', kv([
          ['Wi-Fi RSSI', d.wifiRssi + ' dBm'],
          ['BLE connected', d.ble ? 'yes' : 'no']
        ])));
        body.appendChild(UI.card('Valves', [
          UI.table(['#', 'Type', 'State', 'Angle', 'Duty', 'Fault'],
            d.valves.map((v, i) => [
              i + 1, v.type,
              v.pressed ? 'pressed' : 'released',
              v.type === 'SERVO' ? Math.round(v.angle) + '°' : '—',
              v.type === 'SOLENOID' ? v.duty + ' %' : '—',
              v.fault ? (v.faultText || 'fault') : '—'
            ]))
        ]));
        if (d.faults) {
          body.appendChild(UI.card('Faults', [
            UI.badge(d.faultText || 'Fault active', 'bad'),
            UI.el('div', { class: 'btn-row', style: 'margin-top:10px' }, [
              UI.el('button', { class: 'btn small', text: 'Clear and resume',
                onclick: () => API.releasePanic().then(refresh) })
            ])
          ]));
        }
      } catch (err) {
        UI.clear(body);
        body.appendChild(UI.el('div', { class: 'hint', text: err.message }));
      }
    }

    host.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-top:14px' }, [
      UI.el('button', { class: 'btn', text: 'Refresh', onclick: refresh })
    ]));
    refresh();
    const timer = window.setInterval(refresh, 4000);
    host.onLeave = () => window.clearInterval(timer);
  }

  function kv(rows) {
    return [UI.el('dl', { class: 'kv' }, rows.flatMap(([k, v]) =>
      [UI.el('dt', { text: k }), UI.el('dd', { text: String(v) })]))];
  }

  // =========================================================================
  // Advanced
  // =========================================================================
  function advanced(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Advanced' }));
    host.appendChild(UI.el('p', { class: 'sub',
      text: 'Pin assignments, DSP internals and anything that can break a working '
          + 'instrument. You should not need this page for a normal setup.' }));

    const a = cfg().audio;
    host.appendChild(UI.el('div', { class: 'grid wide' }, [
      UI.card('I2S pins', [
        pinRow('BCLK', 'audio.i2s.bclk'), pinRow('WS / LRCK', 'audio.i2s.ws'),
        pinRow('DOUT', 'audio.i2s.dout'), pinRow('DIN (codec ADC)', 'audio.i2s.din'),
        pinRow('MCLK', 'audio.i2s.mclk'),
        pinRow('DAC mute / SD_MODE', 'audio.sdModePin')
      ]),
      UI.card('I2C pins', [
        pinRow('Codec SDA', 'audio.i2c.sda'), pinRow('Codec SCL', 'audio.i2c.scl'),
        UI.field('Codec address', UI.number(a.codecAddress,
          (v) => set('audio.codecAddress', v), { min: 0, max: 127 })),
        pinRow('PCA9685 SDA', 'valves.pca9685I2c.sda'),
        pinRow('PCA9685 SCL', 'valves.pca9685I2c.scl'),
        pinRow('PCA9685 OE', 'valves.pca9685OePin'),
        UI.field('PCA9685 address', UI.number(cfg().valves.pca9685Address,
          (v) => set('valves.pca9685Address', v), { min: 0, max: 127 }))
      ]),
      UI.card('UART / DIN MIDI', [
        UI.field('UART', UI.select([0, 1, 2].map((n) => ({ value: String(n), label: 'UART' + n })),
          String(cfg().midi.din.uart), (v) => set('midi.din.uart', parseInt(v, 10))),
          'UART0 is the debug console: using it for MIDI will fight with the log output.'),
        pinRow('RX (from the optocoupler)', 'midi.din.rxGpio'),
        pinRow('TX (to the DIN OUT driver)', 'midi.din.txGpio')
      ]),
      UI.card('Audio engine internals', [
        UI.field('Sample rate', UI.select(
          [22050, 32000, 44100, 48000].map((n) => ({ value: String(n), label: n + ' Hz' })),
          String(a.sampleRate), (v) => set('audio.sampleRate', parseInt(v, 10)))),
        UI.field('Bit depth', UI.select(
          [16, 24, 32].map((n) => ({ value: String(n), label: n + ' bit' })),
          String(a.bitDepth), (v) => set('audio.bitDepth', parseInt(v, 10))),
          'Backends that cannot reach the requested depth reduce it and say so.'),
        UI.field('Block size (frames)', UI.number(a.blockSize,
          (v) => set('audio.blockSize', v), { min: 32, max: 512, step: 32 })),
        UI.field('DMA buffers', UI.number(a.dmaBuffers,
          (v) => set('audio.dmaBuffers', v), { min: 2, max: 16 })),
        UI.toggle('Start muted', a.startupMute, (v) => set('audio.startupMute', v))
      ]),
      UI.card('Limiter', [
        UI.slider('Threshold', a.limiter.thresholdDb, -24, 0, 0.5,
          (v) => set('audio.limiter.thresholdDb', v), (v) => v.toFixed(1) + ' dB'),
        UI.slider('Attack', a.limiter.attackMs, 0.1, 20, 0.1,
          (v) => set('audio.limiter.attackMs', v), (v) => v.toFixed(1) + ' ms'),
        UI.slider('Release', a.limiter.releaseMs, 5, 500, 5,
          (v) => set('audio.limiter.releaseMs', v), (v) => v + ' ms'),
        UI.slider('Hard ceiling', Math.round(a.limiter.hardCeiling * 100), 20, 100, 1,
          (v) => set('audio.limiter.hardCeiling', v / 100), (v) => v + ' %'),
        UI.el('div', { class: 'hint',
          text: 'The hard ceiling clamps every sample unconditionally, even if the limiter is off.' })
      ]),
      UI.card('Servo timing', [
        UI.field('Servo PWM frequency', UI.number(cfg().valves.servoFrequencyHz,
          (v) => set('valves.servoFrequencyHz', v), { min: 50, max: 333 })),
        UI.field('Solenoid PWM frequency', UI.number(cfg().valves.solenoidPwmFrequencyHz,
          (v) => set('valves.solenoidPwmFrequencyHz', v), { min: 1000, max: 40000, step: 1000 }),
          'Above 20 kHz the PWM stays out of the audio band.')
      ]),
      UI.card('Network', [
        UI.field('Wi-Fi mode', UI.select([
          { value: 'AP', label: 'Hotspot only' },
          { value: 'STA', label: 'Join an existing network' },
          { value: 'AP_STA', label: 'Both' }
        ], cfg().wifi.mode, (v) => { set('wifi.mode', v); App.rerender(); })),
        UI.field('Network name (SSID)', UI.text(cfg().wifi.ssid,
          (v) => set('wifi.ssid', v), { maxlength: 31 })),
        UI.field('Password', UI.text(cfg().wifi.password,
          (v) => set('wifi.password', v), { password: true, maxlength: 63 })),
        UI.field('Hotspot name', UI.text(cfg().wifi.apSsid,
          (v) => set('wifi.apSsid', v), { maxlength: 31, placeholder: 'MIDI-Trumpet-XXXX' })),
        UI.field('Hotspot password', UI.text(cfg().wifi.apPassword,
          (v) => set('wifi.apPassword', v), { password: true, maxlength: 63 }),
          'Leave empty for an open hotspot.'),
        UI.field('Hostname', UI.text(cfg().wifi.hostname,
          (v) => set('wifi.hostname', v), { maxlength: 31 }),
          'Reachable as http://<hostname>.local on a network that supports mDNS.'),
        UI.toggle('Captive portal on the hotspot', cfg().wifi.captivePortal,
          (v) => set('wifi.captivePortal', v))
      ]),
      UI.card('Per-route filters', [routeFilters()])
    ]));

    host.appendChild(UI.card('Configuration file', [
      UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn small', text: 'Export JSON', onclick: exportConfig }),
        UI.el('label', { class: 'btn small' }, [
          document.createTextNode('Import JSON'),
          (() => {
            const input = UI.el('input', { type: 'file', accept: '.json', style: 'display:none' });
            input.addEventListener('change', importConfig);
            return input;
          })()
        ]),
        UI.el('button', { class: 'btn small danger', text: 'Factory reset', onclick: () => {
          if (!window.confirm('Erase the configuration and reboot with the defaults?')) return;
          API.factoryReset().then(() => UI.toast('Reset, rebooting…', 'warn'));
        } })
      ]),
      UI.el('div', { class: 'hint', style: 'margin-top:10px',
        text: 'Schema version ' + cfg().schemaVersion
            + '. Older files are migrated automatically when the firmware is updated.' })
    ]));
  }

  function pinRow(label, path) {
    const hw = S.hardware;
    const value = get(path);
    const warn = hw && hw.board.warnPins.indexOf(value) !== -1;
    const reserved = hw && hw.board.reservedPins.indexOf(value) !== -1;
    return UI.el('div', { class: 'field' }, [
      UI.el('label', {}, [
        document.createTextNode(label),
        reserved ? UI.el('span', { class: 'ctrl-val', style: 'color:var(--bad)', text: 'reserved' })
                 : warn ? UI.el('span', { class: 'ctrl-val', style: 'color:var(--warn)',
                                          text: 'strapping pin' })
                        : null
      ]),
      UI.number(value, (v) => { set(path, v); App.rerender(); }, { min: -1, max: 48 })
    ]);
  }

  function routeFilters() {
    const wrap = UI.el('div');
    const enabled = cfg().midi.routes.filter((r) => r.enabled);
    if (!enabled.length) {
      wrap.appendChild(UI.el('div', { class: 'hint', text: 'No route is enabled.' }));
      return wrap;
    }
    for (const route of enabled) {
      wrap.appendChild(UI.el('div', { class: 'card',
        style: 'background:var(--card-2);margin-bottom:10px' }, [
        UI.el('h3', { text: route.source + ' → ' + (DEST_LABEL[route.destination]
                                                   || route.destination) }),
        UI.el('div', { class: 'row' }, [
          UI.field('Transpose', UI.number(route.transpose,
            (v) => { route.transpose = v; markDirty(); }, { min: -48, max: 48 })),
          UI.field('Velocity curve', UI.select(
            opts(['LINEAR', 'SOFT', 'HARD', 'FIXED']), route.velocityCurve,
            (v) => { route.velocityCurve = v; markDirty(); })),
          UI.field('Lowest note', UI.number(route.noteMin,
            (v) => { route.noteMin = v; markDirty(); }, { min: 0, max: 127 })),
          UI.field('Highest note', UI.number(route.noteMax,
            (v) => { route.noteMax = v; markDirty(); }, { min: 0, max: 127 }))
        ])
      ]));
    }
    return wrap;
  }

  function exportConfig() {
    const blob = new Blob([JSON.stringify(cfg(), null, 2)], { type: 'application/json' });
    const link = UI.el('a', { href: URL.createObjectURL(blob), download: 'trumpet-config.json' });
    link.click();
    URL.revokeObjectURL(link.href);
  }

  function importConfig(event) {
    const file = event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async () => {
      try {
        const result = await API.importConfig(reader.result);
        if (result.ok) {
          UI.toast('Configuration imported — reboot to apply it', 'ok');
          await App.reload();
        } else {
          UI.toast('The file was refused', 'bad');
          App.showIssues(result.issues);
        }
      } catch (err) { UI.toast(err.message, 'bad'); }
    };
    reader.readAsText(file);
  }

  // =========================================================================
  // Firmware
  // =========================================================================
  function firmware(host) {
    UI.clear(host);
    host.appendChild(UI.el('h1', { text: 'Firmware' }));
    host.appendChild(UI.el('p', { class: 'sub', text: 'Update the instrument over the air.' }));

    const body = UI.el('div');
    host.appendChild(UI.card('Firmware update', [body]));

    API.status().then((status) => {
      UI.clear(body);
      body.appendChild(UI.el('dl', { class: 'kv' }, [
        UI.el('dt', { text: 'Installed version' }),
        UI.el('dd', { text: status.firmware })
      ]));

      if (!status.otaAvailable) {
        body.appendChild(UI.el('div', { class: 'issues' }, [
          UI.el('div', { class: 'issue', 'data-sev': 'WARNING' }, [
            UI.el('span', { class: 'tag', text: 'WARNING' }),
            UI.el('div', { text: 'This build has a single application partition, so an '
                               + 'over-the-air update is not possible. On a 4 MB ESP32-WROOM the '
                               + 'firmware does not leave room for two OTA slots: flash over USB, '
                               + 'or use an 8 MB module with the esp32dev_8mb environment.' })
          ])
        ]));
        return;
      }

      const progress = UI.el('div', { class: 'meter' }, [UI.el('i')]);
      const fileInput = UI.el('input', { type: 'file', accept: '.bin' });
      body.appendChild(UI.field('Firmware image (.bin)', fileInput));
      body.appendChild(UI.el('p', { class: 'hint',
        text: 'Before the first byte is written the instrument mutes the audio, releases every '
            + 'valve and de-energises the solenoids.' }));
      body.appendChild(progress);
      body.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-top:12px' }, [
        UI.el('button', { class: 'btn primary', text: 'Upload and install', onclick: async () => {
          const file = fileInput.files[0];
          if (!file) { UI.toast('Pick a .bin file first', 'warn'); return; }
          try {
            await API.uploadFirmware(file, (fraction) => {
              progress.querySelector('i').style.width = (fraction * 100).toFixed(0) + '%';
            });
            UI.toast('Update installed, rebooting…', 'ok');
          } catch (err) { UI.toast(err.message, 'bad'); }
        } })
      ]));
    }).catch((err) => {
      UI.clear(body);
      body.appendChild(UI.el('div', { class: 'hint', text: err.message }));
    });

    host.appendChild(UI.card('Restart', [
      UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn', text: 'Reboot the instrument', onclick: () => {
          API.reboot().then(() => UI.toast('Rebooting…', 'warn'));
        } })
      ])
    ]));
  }

  return {
    state: S,
    dashboard, play, midi, instrument, audio, pistons, hardware,
    calibration, diagnostics, advanced, firmware,
    renderValves, formatUptime
  };
})();
