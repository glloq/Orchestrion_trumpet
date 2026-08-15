/* ===========================================================================
   wiring.js — the harness, generated from the configuration.

   Two halves, both taken from what the reference project does well:

   1. A diagram drawn live from the current configuration — the real GPIO, the
      real backend, the real actuator per valve — so the picture can never
      drift from the instrument. Downloadable as SVG to print and take to the
      bench.

   2. An electrical dossier that ASKS rather than assumes. A flyback diode, a
      fuse, a logic-level MOSFET and a separate actuator supply are not things
      the firmware can detect: it can only ask the builder to declare them, and
      refuse to pretend the build is safe until they have. Anything not
      declared stays "unverified" and is reported as such.
   =========================================================================== */
'use strict';

const Wiring = (() => {
  const NS = 'http://www.w3.org/2000/svg';

  // The declarations the firmware cannot verify by itself.
  const CHECKS = [
    { key: 'separateSupply', label: 'Actuators on their own supply',
      why: 'A servo or a solenoid drawing from the ESP32 regulator browns the board out mid-note.',
      appliesTo: 'actuators', severity: 'ERROR' },
    { key: 'flyback', label: 'Flyback diode across every solenoid',
      why: 'Without it the inductive kick destroys the MOSFET, and often the ESP32 with it.',
      appliesTo: 'solenoid', severity: 'ERROR' },
    { key: 'logicMosfet', label: 'Logic-level MOSFETs',
      why: 'An IRF540 never fully turns on from 3.3 V and cooks. IRLZ44N, IRLB8721, AO3400.',
      appliesTo: 'solenoid', severity: 'ERROR' },
    { key: 'gatePulldown', label: 'Gate pull-down resistors (100 kΩ)',
      why: 'Keeps the MOSFET off while the ESP32 is in reset.',
      appliesTo: 'solenoid', severity: 'WARNING' },
    { key: 'fuse', label: 'Fuse on the actuator rail',
      why: 'Sized just above the total pull-in current.',
      appliesTo: 'actuators', severity: 'WARNING' },
    { key: 'bulkCaps', label: 'Bulk capacitors fitted',
      why: 'Keeps the actuator in-rush out of the audio ground. See the table below.',
      appliesTo: 'actuators', severity: 'WARNING' },
    { key: 'estop', label: 'Hardware emergency stop',
      why: 'Cuts the actuator rails WITHOUT going through the firmware. A software stop is not '
         + 'a safety function.', appliesTo: 'actuators', severity: 'WARNING' },
    { key: 'optocoupler', label: 'DIN MIDI IN opto-isolated',
      why: 'The MIDI standard requires it: without it a ground loop injects hum straight into '
         + 'the audio chain.', appliesTo: 'dinIn', severity: 'ERROR' },
    { key: 'commonGround', label: 'Single common ground, star-wired',
      why: 'Audio ground and actuator ground must meet once, at the supply — not through the ESP32.',
      appliesTo: 'always', severity: 'WARNING' }
  ];

  const STATES = [
    { value: 'unknown', label: 'Not checked yet' },
    { value: 'yes', label: 'Fitted / verified' },
    { value: 'no', label: 'Not fitted' },
    { value: 'na', label: 'Not applicable' }
  ];

  function relevant(cfg, check) {
    const valves = cfg.valves.items.slice(0, cfg.valves.count);
    const hasSolenoid = valves.some((v) => v.type === 'SOLENOID');
    const hasActuator = valves.some((v) => v.type !== 'DISABLED');
    switch (check.appliesTo) {
      case 'solenoid': return hasSolenoid;
      case 'actuators': return hasActuator;
      case 'dinIn': return !!cfg.midi.din.in;
      default: return true;
    }
  }

  const declarations = (cfg) => (cfg.electrical = cfg.electrical || {});

  // ---- summary ------------------------------------------------------------
  function audit(cfg) {
    const decl = declarations(cfg);
    const issues = [];
    let undeclared = 0;
    for (const check of CHECKS) {
      if (!relevant(cfg, check)) continue;
      const state = decl[check.key] || 'unknown';
      if (state === 'unknown') { undeclared++; continue; }
      if (state === 'no') {
        issues.push({ severity: check.severity, field: 'electrical.' + check.key,
                      message: check.label + ' — declared NOT fitted. ' + check.why });
      }
    }
    return { issues, undeclared };
  }

  // ---- bulk capacitor sizing ---------------------------------------------
  function powerTable(cfg) {
    const valves = cfg.valves.items.slice(0, cfg.valves.count);
    const servos = valves.filter((v) => v.type === 'SERVO').length;
    const solenoids = valves.filter((v) => v.type === 'SOLENOID').length;
    const rows = [];
    if (servos) {
      rows.push({ rail: '5 V servo rail', count: servos,
                  peak: (servos * 1.0).toFixed(1) + ' A',
                  bulk: servos <= 2 ? '1000 µF' : (servos <= 4 ? '2200 µF' : '4700 µF') });
    }
    if (solenoids) {
      rows.push({ rail: '12–24 V solenoid rail', count: solenoids,
                  peak: (solenoids * 2.0).toFixed(1) + ' A',
                  bulk: solenoids <= 2 ? '2200 µF' : '4700 µF' });
    }
    rows.push({ rail: '5 V logic (ESP32 + DAC)', count: 1, peak: '0.6 A', bulk: '470 µF' });
    return rows;
  }

  // ---- diagram ------------------------------------------------------------
  function buildSvg(cfg) {
    const s = UI.svg;
    const valves = cfg.valves.items.slice(0, cfg.valves.count);
    const hasDin = !!(cfg.midi.din.in || cfg.midi.din.out);
    // The viewBox has to cover whatever the configuration actually draws: the
    // valve rows, and the DIN MIDI row that sits below them when it exists.
    const dinY = 300 + valves.length * 64;
    const valveBottom = valves.length ? 300 + (valves.length - 1) * 64 + 50 : 260;
    const height = (hasDin ? dinY + 42 : valveBottom) + 20;
    const svg = s('svg', {
      class: 'harness', viewBox: '0 0 900 ' + height,
      xmlns: NS, role: 'img', 'aria-label': 'Generated wiring harness'
    });

    const box = (x, y, w, h, title, sub, fill) => s('g', {}, [
      s('rect', { x, y, width: w, height: h, rx: '8', fill: fill || 'var(--w-module)' }),
      s('text', { x: x + 12, y: y + 23, 'font-size': '14', 'font-weight': '700',
                  fill: '#fff', text: title }),
      sub ? s('text', { x: x + 12, y: y + 41, 'font-size': '11',
                        fill: 'rgba(255,255,255,.75)', text: sub }) : null
    ]);

    const wire = (x1, y1, x2, y2, colour, label) => s('g', {}, [
      s('path', { d: 'M' + x1 + ' ' + y1 + ' L ' + x2 + ' ' + y2, stroke: colour,
                  'stroke-width': '2.5', fill: 'none' }),
      label ? s('text', { x: (x1 + x2) / 2, y: y1 - 6, 'font-size': '10',
                          'text-anchor': 'middle', fill: 'var(--muted)', text: label }) : null
    ]);

    // ---- modules ---------------------------------------------------------
    svg.appendChild(box(20, 30, 168, 62, 'ESP32-S3', '3.3 V logic'));
    svg.appendChild(box(20, 120, 168, 62, 'PSU', '24 V + 5 V buck', 'var(--w-psu)'));

    const backend = cfg.audio.backend;
    const chain = [];
    if (backend !== 'NONE') {
      chain.push({ title: backend, sub: cfg.audio.bitDepth + ' bit I²S' });
      if (cfg.amplifier.type !== 'NONE' &&
          cfg.amplifier.type !== 'MAX98357_INTERNAL' &&
          cfg.amplifier.type !== 'TAS5760_INTERNAL') {
        chain.push({ title: cfg.amplifier.type, sub: cfg.amplifier.maxPower + ' W' });
      }
      chain.push({ title: cfg.speaker.name,
                   sub: cfg.speaker.impedance + ' Ω · limited to ' + cfg.speaker.powerLimit + ' W' });
      chain.push({ title: 'Sealed chamber', sub: '→ trumpet leadpipe' });
    }
    chain.forEach((m, i) => {
      const x = 250 + i * 165;
      svg.appendChild(box(x, 30, 150, 62, m.title, m.sub,
                          i === chain.length - 1 ? 'var(--w-audio)' : undefined));
      if (i > 0) svg.appendChild(wire(x - 15, 61, x, 61, 'var(--w-audio)'));
    });
    if (chain.length) {
      svg.appendChild(wire(188, 61, 250, 61, 'var(--w-i2s)',
        'I²S ' + cfg.audio.i2s.bclk + '/' + cfg.audio.i2s.ws + '/' + cfg.audio.i2s.dout));
    }

    // ---- rails -----------------------------------------------------------
    const railY = 214;
    const rails = [
      { y: railY, colour: 'var(--w-v24)', label: '24 V (amplifier / solenoids)' },
      { y: railY + 18, colour: 'var(--w-v5)', label: '5 V (logic, servos)' },
      { y: railY + 36, colour: 'var(--w-gnd)', label: 'GND (common, star)' }
    ];
    // The rails stop at 690 so their labels fit inside the 900-unit viewBox
    // instead of being clipped by the right edge.
    rails.forEach((r) => {
      svg.appendChild(s('path', { d: 'M104 182 L 104 ' + r.y + ' L 690 ' + r.y,
                                  stroke: r.colour, 'stroke-width': '3', fill: 'none' }));
      svg.appendChild(s('text', { x: '698', y: r.y + 4, 'font-size': '10',
                                  fill: 'var(--muted)', text: r.label }));
    });

    // ---- valves ----------------------------------------------------------
    valves.forEach((v, i) => {
      const y = 300 + i * 64;
      const solenoid = v.type === 'SOLENOID';
      const pca = v.type === 'SERVO' && v.driver === 'PCA9685';
      const title = 'Valve ' + (i + 1) + ' · ' + v.type;
      const sub = v.type === 'DISABLED' ? 'not fitted'
                : pca ? 'PCA9685 ch ' + v.channel
                : 'GPIO ' + v.gpio;
      svg.appendChild(box(250, y, 200, 50, title, sub,
                          solenoid ? 'var(--w-valve)' : 'var(--w-i2c)'));

      if (v.type === 'DISABLED') return;

      // Signal from the ESP32.
      svg.appendChild(s('path', {
        d: 'M104 ' + (railY + 36) + ' L 104 ' + (y + 25) + ' L 250 ' + (y + 25),
        stroke: solenoid ? 'var(--w-valve)' : 'var(--w-i2c)', 'stroke-width': '2',
        fill: 'none', 'stroke-dasharray': pca ? '5 4' : ''
      }));

      if (solenoid) {
        // MOSFET + flyback + fuse, drawn because they are mandatory.
        svg.appendChild(box(500, y, 120, 50, 'MOSFET', 'logic level', 'var(--w-module)'));
        svg.appendChild(wire(450, y + 25, 500, y + 25, 'var(--w-valve)'));
        svg.appendChild(box(670, y, 150, 50, 'Solenoid', 'flyback + fuse', 'var(--w-psu)'));
        svg.appendChild(wire(620, y + 25, 670, y + 25, 'var(--w-v24)'));
      } else {
        svg.appendChild(box(500, y, 150, 50, 'Servo', v.releasedAngle + '° → ' + v.pressedAngle + '°',
                            'var(--w-module)'));
        svg.appendChild(wire(450, y + 25, 500, y + 25, 'var(--w-v5)'));
      }
    });

    // ---- DIN MIDI --------------------------------------------------------
    if (hasDin) {
      const y = dinY;
      const parts = [];
      if (cfg.midi.din.in) parts.push('IN GPIO ' + cfg.midi.din.rxGpio + ' (opto)');
      if (cfg.midi.din.out) parts.push('OUT GPIO ' + cfg.midi.din.txGpio);
      svg.appendChild(box(250, y - 4, 250, 46, 'DIN MIDI', parts.join(' · '), 'var(--w-3v3)'));
      svg.appendChild(s('path', {
        d: 'M104 ' + (railY + 36) + ' L 104 ' + (y + 19) + ' L 250 ' + (y + 19),
        stroke: 'var(--w-3v3)', 'stroke-width': '2', fill: 'none'
      }));
    }

    return svg;
  }

  function downloadSvg(cfg) {
    const svg = buildSvg(cfg);
    // Inline the tokens: a downloaded file has no stylesheet behind it.
    const style = getComputedStyle(document.documentElement);
    let markup = new XMLSerializer().serializeToString(svg);
    markup = markup.replace(/var\(([^)]+)\)/g,
                            (_, name) => style.getPropertyValue(name.trim()).trim() || '#888');
    const blob = new Blob(['<?xml version="1.0" encoding="UTF-8"?>\n' + markup],
                          { type: 'image/svg+xml' });
    const link = UI.el('a', { href: URL.createObjectURL(blob), download: 'trumpet-wiring.svg' });
    link.click();
    URL.revokeObjectURL(link.href);
  }

  return { CHECKS, STATES, relevant, declarations, audit, powerTable, buildSvg, downloadSvg };
})();
