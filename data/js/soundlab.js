/* ===========================================================================
   soundlab.js — the interactive sound-development page.

   Voicing a driver behind a cone behind a real trumpet is not a settings
   screen, it is a session: hours of small changes, each one judged by ear
   against the last. Three things make that possible and this page is built
   around them.

   1. EVERYTHING IS LIVE. A slider posts to /api/audio/preview, the firmware
      drops the voicing into a lock-free mailbox and the audio task picks it up
      at the start of the next block — about 2.7 ms at 48 kHz / 128 frames.
      Nothing is saved and nothing reboots until you say so.

        slider -> POST /api/audio/preview -> mailbox -> next block -> sound

   2. A/B. After twenty minutes of tuning nobody can still remember whether the
      result is actually better. A and B are two complete voicings held in the
      browser; switching between them is one preview call, so the comparison is
      instantaneous and the ear has nothing to fill in.

   3. MACROS FIRST, PARAMETERS SECOND. Quick Tune offers seven musical
      controls; each drives several real parameters at once, and the Voicing
      and Expert levels show exactly what they moved. Nobody tunes an
      instrument by typing "harmonic 7 = 0.137" — but everybody wants to see
      that number once the sound is right.

   The macros are computed here, in the browser. The device only ever receives
   a voicing: it has no idea a macro exists, which is what keeps the firmware's
   idea of "the sound" a single, serialisable object.

   The split is deliberate and hard: this page can only ever change a
   VoicingConfig. Impedance, power rating, protection limits, pins and backend
   are not reachable from here — they live in Settings, behind validation and a
   reboot, and no amount of experimenting on the timbre can touch them.
   =========================================================================== */
'use strict';

const SoundLab = (() => {
  const LEVELS = ['Quick Tune', 'Voicing', 'Expert'];
  let level = 'Quick Tune';

  // A and B are complete voicings. `slot` says which one is sounding.
  const ab = { A: null, B: null, slot: 'A' };
  // Macro positions, 0..1. Derived, never stored on the device.
  let macros = null;
  let saved = null;         // the voicing the instrument would boot with
  let library = [];
  let pushTimer = null;

  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const lerp = (a, b, t) => a + (b - a) * t;
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const live = () => ab[ab.slot];

  // =========================================================================
  // Macros
  //
  // Each one is a named musical intention that moves a handful of real
  // parameters together. They are applied to the FACTORY value of the fields
  // they own, not to the current value, so dragging a macro back to the middle
  // really does undo it instead of drifting.
  // =========================================================================
  const MACROS = [
    { key: 'brightness', label: 'Body ↔ Brightness', hint: 'Too dull? push this first.',
      help: 'Spectral tilt: how fast the partials fall away. This is the single '
          + 'biggest control over how covered or how open the instrument sounds.',
      apply: (v, x) => {
        v.darkTilt = lerp(3.6, 1.7, x);
        v.brightTilt = lerp(1.3, 0.15, x);
      } },
    { key: 'brassy', label: 'Soft ↔ Brassy', hint: 'Too aggressive? pull this back.',
      help: 'Level of the partials above the fifth. A brass instrument gets its '
          + 'edge from those, and so does a driver that is being asked for too much.',
      apply: (v, x) => {
        const base = MOCK.defaultVoicing().additive.harmonicGain;
        const k = lerp(0.35, 1.9, x);
        for (let i = 4; i < 16; i++) v.additive.harmonicGain[i] = clamp(base[i] * k, 0, 2);
      } },
    { key: 'breath', label: 'Clean ↔ Breath', hint: 'Air moving through the instrument.',
      help: 'Continuous breath noise, plus the air fed through the exciter. A '
          + 'little is what stops a sustained note sounding like an oscillator.',
      apply: (v, x) => {
        v.envelope.breathNoise = lerp(0.0, 0.11, x);
        v.exciter.noiseAmount = lerp(0.0, 0.18, x);
      } },
    { key: 'attack', label: 'Soft ↔ Sharp attack', hint: 'Attack too synthetic? start here.',
      help: 'Attack time and the chiff at the start of a note. Real attacks are '
          + 'noisy; a clean fast ramp reads as a synthesiser every time.',
      apply: (v, x) => {
        v.envelope.attackMs = lerp(55, 3, x);
        v.envelope.attackNoise = lerp(0.02, 0.34, x);
        v.exciter.transientMs = lerp(45, 5, x);
      } },
    { key: 'dynamics', label: 'Dynamics', hint: 'How much velocity really does.',
      help: 'Level and brightness range between a soft and a hard note. At zero '
          + 'the instrument plays everything at the same weight.',
      apply: (v, x) => {
        v.velocityFloor = lerp(0.7, 0.04, x);
        v.additive.velocityBrightness = lerp(0.25, 1.0, x);
      } },
    { key: 'presence', label: 'Warm ↔ Presence', hint: 'Corrects the chamber, not the note.',
      help: 'A gentle shelf pair: down low, up around 3 kHz. Use it for the '
          + 'colouration of the chamber and the cone, not for the timbre itself.',
      apply: (v, x) => {
        const d = (x - 0.5) * 2;             // -1 .. +1
        v.eq[0].gainDb = clamp(-d * 3.5, -18, 18);
        v.eq[0].enabled = Math.abs(d) > 0.02;
        v.eq[4].gainDb = clamp(d * 5.0, -18, 18);
        v.eq[4].enabled = Math.abs(d) > 0.02;
      } },
    { key: 'output', label: 'Output', hint: 'Trim, before the limiter — never past it.',
      help: 'Level going into the protection stage. It cannot lift the output '
          + 'past the speaker limit: that ceiling is computed from the driver '
          + 'and the amplifier and is not reachable from this page.',
      apply: (v, x) => { v.outputTrimDb = lerp(-12, 6, x); } }
  ];

  const defaultMacros = () => ({ brightness: 0.5, brassy: 0.5, breath: 0.3,
                                 attack: 0.5, dynamics: 0.6, presence: 0.5,
                                 output: clamp((0 + 12) / 18, 0, 1) });

  function applyMacros(base, m) {
    const v = clone(base);
    for (const macro of MACROS) macro.apply(v, clamp(m[macro.key], 0, 1));
    return v;
  }

  // Which fields a macro owns, so "Advanced" can show what it actually did.
  function macroSummary(v) {
    return [
      ['Spectral tilt', v.darkTilt.toFixed(2) + ' → ' + v.brightTilt.toFixed(2)],
      ['Harmonics 5–16', 'x' + (v.additive.harmonicGain[5] /
                                Math.max(0.0001, MOCK.defaultVoicing().additive.harmonicGain[5]))
                              .toFixed(2)],
      ['Breath noise', v.envelope.breathNoise.toFixed(3)],
      ['Attack', v.envelope.attackMs.toFixed(1) + ' ms · chiff '
                 + v.envelope.attackNoise.toFixed(2)],
      ['Velocity floor', v.velocityFloor.toFixed(2)],
      ['EQ 220 Hz / 3.2 kHz', v.eq[0].gainDb.toFixed(1) + ' dB / '
                              + v.eq[4].gainDb.toFixed(1) + ' dB'],
      ['Output trim', v.outputTrimDb.toFixed(1) + ' dB']
    ];
  }

  // =========================================================================
  // Live push
  // =========================================================================
  // Coalesced: dragging a slider produces far more events than there are audio
  // blocks, and only the newest one matters.
  function push(immediate) {
    if (pushTimer) clearTimeout(pushTimer);
    const send = async () => {
      pushTimer = null;
      try {
        const r = await API.audioPreview(live());
        // The firmware echoes what it actually applied, so a clamped value is
        // shown clamped instead of the UI pretending otherwise.
        if (r && r.voicing) ab[ab.slot] = r.voicing;
        markDirty();
      } catch (err) { UI.toast(err.message, 'bad'); }
    };
    if (immediate) send(); else pushTimer = setTimeout(send, 40);
  }

  function markDirty() {
    const badge = document.getElementById('sl-dirty');
    if (!badge) return;
    const changed = JSON.stringify(live()) !== JSON.stringify(saved);
    badge.textContent = changed ? 'not saved' : 'saved';
    badge.className = 'badge ' + (changed ? 'warn' : 'ok');
  }

  // =========================================================================
  // Page
  // =========================================================================
  async function render(host) {
    if (!ab.A) {
      const cfg = App.config();
      saved = clone(cfg.voicing);
      ab.A = clone(cfg.voicing);
      ab.B = clone(cfg.voicing);
      macros = defaultMacros();
    }
    try {
      const r = await API.voicings();
      library = r.items || [];
      saved = r.saved || saved;
    } catch (_) { /* the page still works without the library */ }

    UI.clear(host);
    host.appendChild(header());
    host.appendChild(UI.el('div', { class: 'card' }, [
      UI.el('div', { class: 'modal-tabs' }, LEVELS.map((name) => UI.el('button', {
        class: 'modal-tab' + (name === level ? ' active' : ''), text: name,
        onclick: () => { level = name; App.render(); }
      }))),
      level === 'Quick Tune' ? quickTune()
        : level === 'Voicing' ? voicing() : expert()
    ]));
    host.appendChild(guide());
    markDirty();
  }

  function header() {
    const slotBtn = (name) => UI.el('button', {
      class: 'btn' + (ab.slot === name ? ' primary' : ''), text: name,
      onclick: () => { ab.slot = name; push(true); App.render(); }
    });
    return UI.el('div', { class: 'card' }, [
      UI.el('div', { class: 'card-head' }, [
        UI.el('h2', { text: 'Sound Lab' }),
        UI.el('span', { class: 'card-note',
          text: 'live — every change is heard on the next audio block' })
      ]),
      UI.el('div', { class: 'btn-row' }, [
        slotBtn('A'), slotBtn('B'),
        UI.el('button', { class: 'btn', text: 'Copy A → B',
          onclick: () => { ab.B = clone(ab.A); UI.toast('A copied into B', 'ok'); } }),
        UI.el('button', { class: 'btn', text: 'Copy B → A',
          onclick: () => { ab.A = clone(ab.B); UI.toast('B copied into A', 'ok'); } }),
        UI.el('span', { style: 'flex:1' }),
        UI.el('span', { id: 'sl-dirty', class: 'badge', text: 'saved' })
      ]),
      UI.el('p', { class: 'help',
        text: 'A and B are two complete voicings, held here in the browser. '
            + 'Switching between them is one live update, so the comparison is '
            + 'immediate — which is the only way to tell after twenty minutes '
            + 'of tuning whether the sound is really better.' }),
      UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn primary', text: 'Save as the instrument’s sound',
          onclick: async () => {
            try {
              await API.audioPreview(live());
              await API.audioCommit();
              saved = clone(live());
              UI.toast('Saved — this is now the sound it boots with', 'ok');
              markDirty();
            } catch (err) { UI.toast(err.message, 'bad'); }
          } }),
        UI.el('button', { class: 'btn', text: 'Revert to saved', onclick: async () => {
          try {
            await API.audioRevert();
            ab[ab.slot] = clone(saved);
            UI.toast('Back to the stored sound', 'ok');
            App.render();
          } catch (err) { UI.toast(err.message, 'bad'); }
        } }),
        UI.el('button', { class: 'btn', text: 'Reset to factory voicing', onclick: () => {
          ab[ab.slot] = MOCK.defaultVoicing();
          macros = defaultMacros();
          push(true);
          App.render();
        } })
      ]),
      presetRow()
    ]);
  }

  function presetRow() {
    const wrap = UI.el('div', {});
    wrap.appendChild(UI.el('div', { class: 'section-title', style: 'margin-top:16px',
                                    text: 'Voicing presets' }));
    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'Kept separate from the hardware presets on purpose: one cone, '
          + 'several sounds. The hardware preset says what the instrument is '
          + 'made of; a voicing says how it should sound.' }));
    const row = UI.el('div', { class: 'btn-row' });
    for (const item of library) {
      row.appendChild(UI.el('button', {
        class: 'btn small chip', text: item.name,
        onclick: async () => {
          try {
            await API.voicingLoad(item.name);
            ab[ab.slot] = clone(item);
            UI.toast('“' + item.name + '” loaded into ' + ab.slot, 'ok');
            App.render();
          } catch (err) { UI.toast(err.message, 'bad'); }
        }
      }));
      row.appendChild(UI.el('button', {
        class: 'btn small', text: '×', title: 'Delete ' + item.name,
        onclick: async () => {
          try { await API.voicingDelete(item.name); App.render(); }
          catch (err) { UI.toast(err.message, 'bad'); }
        }
      }));
    }
    let pendingName = live() ? live().name : 'Voicing';
    row.appendChild(UI.text(pendingName, (v) => { pendingName = v; }, { maxlength: 31 }));
    row.appendChild(UI.el('button', { class: 'btn small primary', text: 'Save preset',
      onclick: async () => {
        try {
          await API.audioPreview(live());
          await API.voicingSave(pendingName);
          UI.toast('Saved as “' + pendingName + '”', 'ok');
          App.render();
        } catch (err) { UI.toast(err.message, 'bad'); }
      } }));
    wrap.appendChild(row);
    return wrap;
  }

  // ---- level 1 -------------------------------------------------------------
  function quickTune() {
    const wrap = UI.el('div');
    wrap.appendChild(UI.el('p', { class: 'help',
      text: 'Seven controls, each moving several real parameters at once. Play '
          + 'a note and drag: the sound follows immediately. What they changed '
          + 'is at the bottom, and every one of those numbers is editable under '
          + 'Voicing and Expert.' }));

    for (const macro of MACROS) {
      const row = UI.el('div', { class: 'field' }, [
        UI.slider(macro.label, Math.round(macros[macro.key] * 100), 0, 100, 1, (x) => {
          macros[macro.key] = x / 100;
          ab[ab.slot] = applyMacros(baseFor(), macros);
          push();
          const s = document.getElementById('sl-macro-summary');
          if (s) refreshSummary(s);
        }, (x) => x + ' %'),
        UI.el('div', { class: 'help', text: macro.hint + '  ' + macro.help })
      ]);
      wrap.appendChild(row);
    }

    wrap.appendChild(UI.disclosure('What the macros changed', () => {
      const host = UI.el('div', { id: 'sl-macro-summary' });
      refreshSummary(host);
      return host;
    }));
    return wrap;
  }

  // Macros are applied to the factory value of the fields they own, so they
  // stay reversible. Everything else in the voicing is carried across
  // untouched, which is what lets Quick Tune and Expert coexist.
  function baseFor() {
    const base = clone(live());
    const factory = MOCK.defaultVoicing();
    base.darkTilt = factory.darkTilt;
    base.brightTilt = factory.brightTilt;
    base.additive.harmonicGain = factory.additive.harmonicGain.slice();
    return base;
  }

  function refreshSummary(host) {
    UI.clear(host);
    host.appendChild(UI.kv(macroSummary(live())));
  }

  // ---- level 2 -------------------------------------------------------------
  function voicing() {
    const v = live();
    const set = (path, x) => {
      const parts = path.split('.');
      let node = v;
      for (let i = 0; i < parts.length - 1; i++) node = node[parts[i]];
      node[parts[parts.length - 1]] = x;
      push();
    };
    const num = (label, path, opts, help) => {
      const parts = path.split('.');
      let node = v;
      for (let i = 0; i < parts.length - 1; i++) node = node[parts[i]];
      return UI.field(label, UI.number(node[parts[parts.length - 1]],
                                       (x) => set(path, x), opts), help);
    };
    const wrap = UI.el('div');

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Source' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Generator', UI.select([
        { value: 'ADDITIVE', label: 'Additive — reference' },
        { value: 'SINE', label: 'Sine' },
        { value: 'WAVETABLE', label: 'Wavetable' },
        { value: 'HYBRID', label: 'Hybrid' },
        { value: 'BRASS_EXCITER', label: 'Brass exciter — EXPERIMENTAL' }
      ], v.engine, (x) => { set('engine', x); App.render(); }),
        v.engine === 'BRASS_EXCITER'
          ? 'Non-linear exciter. It has never been heard through a real cone: '
          + 'it exists to be characterised against ADDITIVE, which stays the reference.'
          : 'ADDITIVE is deterministic and easy to reason about, which is what '
          + 'you want while the cone is still changing.'),
      num('Harmonics', 'additive.harmonicCount', { min: 1, max: 16 }),
      v.engine === 'HYBRID'
        ? num('Hybrid mix', 'hybridMix', { min: 0, max: 1, step: 0.01 },
              '1 = all additive, 0 = all wavetable.') : null
    ]));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Harmonic levels' }));
    wrap.appendChild(harmonicEditor(v));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Spectrum' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      num('Dark tilt', 'darkTilt', { min: 0, max: 6, step: 0.05 },
          'Roll-off exponent when the instrument is played softly.'),
      num('Bright tilt', 'brightTilt', { min: 0, max: 6, step: 0.05 },
          'And when it is played hard. The two interpolate with the blow amount.'),
      num('Pitch → brightness', 'additive.pitchBrightness', { min: 0, max: 1, step: 0.01 })
    ]));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Dynamics' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      num('Velocity floor', 'velocityFloor', { min: 0, max: 1, step: 0.01 },
          'Level at velocity 1. Lower means a wider dynamic range.'),
      num('Velocity → brightness', 'additive.velocityBrightness', { min: 0, max: 1, step: 0.01 }),
      num('CC2 breath → level', 'breathToVolume', { min: 0, max: 1, step: 0.01 }),
      num('CC2 breath → brightness', 'additive.breathBrightness', { min: 0, max: 1, step: 0.01 }),
      num('CC11 expression → level', 'expressionToVolume', { min: 0, max: 1, step: 0.01 }),
      num('CC11 → brightness', 'additive.expressionBrightness', { min: 0, max: 1, step: 0.01 }),
      num('Aftertouch → brightness', 'aftertouchToBrightness', { min: 0, max: 1, step: 0.01 }),
      num('Aftertouch → level', 'aftertouchToVolume', { min: 0, max: 1, step: 0.01 })
    ]));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Attack and breath' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      num('Attack (ms)', 'envelope.attackMs', { min: 0.5, max: 500, step: 0.5 }),
      num('Decay (ms)', 'envelope.decayMs', { min: 1, max: 2000 }),
      num('Sustain', 'envelope.sustain', { min: 0, max: 1, step: 0.01 }),
      num('Release (ms)', 'envelope.releaseMs', { min: 1, max: 3000 }),
      num('Attack noise', 'envelope.attackNoise', { min: 0, max: 1, step: 0.01 },
          'The chiff at the very start of a note.'),
      num('Breath noise', 'envelope.breathNoise', { min: 0, max: 0.5, step: 0.005 })
    ]));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Vibrato and pitch' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Vibrato source', UI.select([
        { value: 'CC1', label: 'Modulation wheel (CC1)' },
        { value: 'AFTERTOUCH', label: 'Channel pressure' },
        { value: 'AUTOMATIC', label: 'Always on' },
        { value: 'OFF', label: 'Off' }
      ], v.vibrato.source, (x) => set('vibrato.source', x))),
      num('Rate (Hz)', 'vibrato.frequencyHz', { min: 0.1, max: 20, step: 0.1 }),
      num('Depth (cents)', 'vibrato.depthCents', { min: 0, max: 200 }),
      num('Delay (ms)', 'vibrato.delayMs', { min: 0, max: 5000 }),
      num('Fade-in (ms)', 'vibrato.fadeInMs', { min: 1, max: 5000 }),
      UI.field('Pitch bend range', UI.select(
        [1, 2, 3, 12].map((x) => ({ value: String(x), label: '± ' + x + ' semitones' })),
        String(v.pitchBendRange), (x) => set('pitchBendRange', parseInt(x, 10))),
        'A sequencer can still override this at runtime with RPN 0.')
    ]));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Register compensation' }));
    wrap.appendChild(registerEditor(v, set));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Tone — 6 bands' }));
    wrap.appendChild(eqEditor(v, set));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Output' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      num('Trim (dB)', 'outputTrimDb', { min: -24, max: 12, step: 0.5 },
          'Before the limiter, never past it: the speaker ceiling is computed '
        + 'from the driver and the amplifier and is not editable here.')
    ]));
    return wrap;
  }

  function harmonicEditor(v) {
    const wrap = UI.el('div');
    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'H1 is the fundamental. These are the levels before the spectral '
          + 'tilt is applied, so they shape the instrument’s character and the '
          + 'tilt decides how much of it survives at a given dynamic.' }));
    const grid = UI.el('div', { class: 'harmonics' });
    for (let i = 0; i < 16; i++) {
      const active = i < v.additive.harmonicCount;
      grid.appendChild(UI.el('div', { class: 'harmonic' + (active ? '' : ' off') }, [
        UI.slider('H' + (i + 1), Math.round(v.additive.harmonicGain[i] * 100), 0, 200, 1,
                  (x) => { v.additive.harmonicGain[i] = x / 100; push(); },
                  (x) => (x / 100).toFixed(2))
      ]));
    }
    wrap.appendChild(grid);
    wrap.appendChild(UI.el('div', { class: 'btn-row' }, [
      UI.el('button', { class: 'btn small', text: 'Reset harmonics', onclick: () => {
        v.additive.harmonicGain = MOCK.defaultVoicing().additive.harmonicGain.slice();
        push(true);
        App.render();
      } })
    ]));
    return wrap;
  }

  function registerEditor(v, set) {
    const wrap = UI.el('div');
    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'Level and brightness as a function of the note, interpolated '
          + 'between five breakpoints. This is the right tool for “the bottom '
          + 'is weak and the top is harsh”: the master EQ would fix it by '
          + 'wrecking the timbre everywhere else.' }));
    const rows = v.registerCurve.map((pt, i) => [
      UI.noteName(pt.note),
      UI.number(pt.note, (x) => set('registerCurve.' + i + '.note', x), { min: 0, max: 127 }),
      UI.number(pt.gainDb, (x) => set('registerCurve.' + i + '.gainDb', x),
                { min: -18, max: 12, step: 0.5 }),
      UI.number(pt.brightness, (x) => set('registerCurve.' + i + '.brightness', x),
                { min: -1, max: 1, step: 0.05 })
    ]);
    wrap.appendChild(UI.table(['Note', 'MIDI', 'Gain (dB)', 'Brightness'], rows));
    return wrap;
  }

  function eqEditor(v, set) {
    const rows = v.eq.map((b, i) => [
      UI.toggle('', b.enabled, (x) => set('eq.' + i + '.enabled', x)),
      UI.number(b.frequency, (x) => set('eq.' + i + '.frequency', x), { min: 20, max: 20000 }),
      UI.number(b.gainDb, (x) => set('eq.' + i + '.gainDb', x), { min: -18, max: 18, step: 0.5 }),
      UI.number(b.q, (x) => set('eq.' + i + '.q', x), { min: 0.1, max: 10, step: 0.05 })
    ]);
    const wrap = UI.el('div');
    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'Six parametric bands, on top of the protection high pass. A driver '
          + 'in a sealed chamber behind a two-stage cone usually has more than '
          + 'three things wrong with it.' }));
    wrap.appendChild(UI.table(['On', 'Frequency (Hz)', 'Gain (dB)', 'Q'], rows));
    return wrap;
  }

  // ---- level 3 -------------------------------------------------------------
  function expert() {
    const v = live();
    const set = (path, x) => {
      const parts = path.split('.');
      let node = v;
      for (let i = 0; i < parts.length - 1; i++) node = node[parts[i]];
      node[parts[parts.length - 1]] = x;
      push();
    };
    const wrap = UI.el('div');

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Brass exciter — EXPERIMENTAL' }));
    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'A light non-linear exciter, not a physical model: the real trumpet '
          + 'is the resonator, so there is nothing downstream of the cone left '
          + 'to simulate. Asymmetric waveshaping is what produces the even '
          + 'harmonics a symmetric shaper cannot — that difference is the '
          + 'difference between a trumpet and a clarinet. It has never been '
          + 'heard through a real cone.' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Drive', UI.number(v.exciter.drive,
        (x) => set('exciter.drive', x), { min: 0.1, max: 12, step: 0.1 })),
      UI.field('Asymmetry', UI.number(v.exciter.asymmetry,
        (x) => set('exciter.asymmetry', x), { min: -1, max: 1, step: 0.05 }),
        'Even-harmonic content. Zero is a clarinet.'),
      UI.field('Static pressure', UI.number(v.exciter.pressure,
        (x) => set('exciter.pressure', x), { min: 0, max: 1, step: 0.01 })),
      UI.field('Blow → drive', UI.number(v.exciter.pressureToDrive,
        (x) => set('exciter.pressureToDrive', x), { min: 0, max: 4, step: 0.05 })),
      UI.field('Air noise', UI.number(v.exciter.noiseAmount,
        (x) => set('exciter.noiseAmount', x), { min: 0, max: 1, step: 0.01 }),
        'Fed through the shaper, not added after it.'),
      UI.field('Pressure rise (ms)', UI.number(v.exciter.transientMs,
        (x) => set('exciter.transientMs', x), { min: 0.5, max: 500, step: 0.5 }))
    ]));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'The whole voicing' }));
    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'Exactly what the firmware holds, and exactly what a preview sends. '
          + 'Copy it into a note, paste it back later.' }));
    const area = UI.el('textarea', { rows: '14', class: 'mono-area',
                                     spellcheck: 'false' });
    area.value = JSON.stringify(live(), null, 2);
    wrap.appendChild(area);
    wrap.appendChild(UI.el('div', { class: 'btn-row' }, [
      UI.el('button', { class: 'btn small', text: 'Apply this JSON', onclick: () => {
        try {
          ab[ab.slot] = MOCK.sanitiseVoicing(JSON.parse(area.value));
          push(true);
          App.render();
        } catch (err) { UI.toast('Not a valid voicing: ' + err.message, 'bad'); }
      } })
    ]));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Not editable here' }));
    wrap.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'Speaker impedance and power, the protection limit, the hard '
          + 'ceiling, the DC blocker, the pins, the backend and the sample '
          + 'rate are deliberately absent from this page. They are not part of '
          + 'a voicing, a preview cannot reach them, and changing them goes '
          + 'through validation and a reboot. A laboratory slider that can '
          + 'switch off the thing protecting the coil is not a laboratory '
          + 'slider, it is a fault waiting to happen.' }));
    return wrap;
  }

  // ---- the order to do it in ----------------------------------------------
  const STEPS = [
    ['Safety and level', 'Check the validation report and set Output so the limiter is '
      + 'not working on every note. Everything after this is wasted if the '
      + 'protection stage is clipping.'],
    ['Speaker and cone', 'Sweep and tones from Configure → Calibration. Find the chamber '
      + 'resonances before trying to voice through them.'],
    ['Low/high balance', 'Warm ↔ Presence, then the EQ bands if a resonance needs a '
      + 'narrow cut.'],
    ['Harmonic richness', 'Body ↔ Brightness first, then Soft ↔ Brassy, then the '
      + 'individual harmonic levels.'],
    ['Attack and breath', 'Soft ↔ Sharp attack and Clean ↔ Breath. Play repeated short '
      + 'notes, not long ones.'],
    ['Dynamics', 'Play soft and hard alternately and set Dynamics, then the CC2 and '
      + 'CC11 weights if you use a breath controller.'],
    ['Register balance', 'Play a scale over the whole range and correct with the '
      + 'register curve — not with the master EQ.'],
    ['A/B', 'Copy A → B, change one thing, switch back and forth. If you cannot '
      + 'hear which is which, the change was not worth making.'],
    ['Save', 'Save as a named voicing. Hardware presets and voicings are separate, '
      + 'so one cone can keep several sounds.']
  ];

  function guide() {
    return UI.card('Tune my sound — the order that works', [
      UI.el('p', { class: 'help', style: 'margin-top:0',
        text: 'Voicing out of order wastes time: fixing the register before the '
            + 'harmonics means doing the register twice.' }),
      UI.el('ol', { class: 'guide' }, STEPS.map(([title, body]) =>
        UI.el('li', {}, [
          UI.el('strong', { text: title }),
          UI.el('div', { class: 'help', text: body })
        ])))
    ]);
  }

  return { render, macros: () => macros };
})();
