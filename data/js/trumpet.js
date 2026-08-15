/* ===========================================================================
   trumpet.js — the playable picture of the instrument.

   The reference project draws the real fretboard rather than an abstract grid,
   so the screen and the machine look like the same object. The equivalent here
   is the trumpet itself: bell, leadpipe, valve casings and three pistons that
   move with the fingering actually being played.

   Pistons are also buttons: pressing one in MANUAL mode drives the real
   actuator through the WebSocket.
   =========================================================================== */
'use strict';

const Trumpet = (() => {
  const NS = 'http://www.w3.org/2000/svg';
  let root = null;
  let pistons = [];
  let onPress = null;
  let interactive = false;
  let count = 3;

  // Geometry: a side view, bell on the right, in a 620 x 240 box.
  const CASING_X = [232, 288, 344];
  const CASING_Y = 96;
  const CASING_W = 42;
  const CASING_H = 96;

  function build(container, options) {
    const o = options || {};
    root = container;
    count = Math.min(Math.max(o.valveCount || 3, 1), 4);
    interactive = !!o.interactive;
    onPress = o.onPress || null;
    render();
    return api;
  }

  function render() {
    UI.clear(root);
    const s = UI.svg;
    const svg = s('svg', {
      class: 'trumpet', viewBox: '0 0 620 240', role: 'img',
      'aria-label': 'Trumpet with ' + count + ' pistons'
    });

    // ---- gradients -------------------------------------------------------
    const defs = s('defs', {}, [
      s('linearGradient', { id: 'brass', x1: '0', y1: '0', x2: '0', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '38%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '78%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-shade)' })
      ]),
      s('linearGradient', { id: 'brass-h', x1: '0', y1: '0', x2: '1', y2: '0' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-body-c)' }),
        s('stop', { offset: '30%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-b)' })
      ])
    ]);
    svg.appendChild(defs);

    // ---- bell ------------------------------------------------------------
    svg.appendChild(s('path', {
      d: 'M470 144 C 500 144, 520 120, 528 96 L 596 40 C 606 32, 614 40, 610 52 '
       + 'L 586 148 C 578 176, 548 196, 512 196 C 488 196, 470 178, 470 158 Z',
      fill: 'url(#brass)', stroke: 'var(--br-shade)', 'stroke-width': '2'
    }));
    svg.appendChild(s('path', {
      d: 'M596 40 L 610 52 L 586 148 C 582 162, 570 172, 556 178 L 578 60 Z',
      fill: 'var(--br-highlight)', opacity: '.45'
    }));

    // ---- main tube from the casings to the bell --------------------------
    svg.appendChild(s('path', {
      d: 'M386 118 L 476 118 L 476 146 L 386 146 Z',
      fill: 'url(#brass-h)', stroke: 'var(--br-shade)', 'stroke-width': '1.5'
    }));

    // ---- leadpipe and the acoustic chamber that replaces the mouthpiece --
    svg.appendChild(s('path', {
      d: 'M96 120 L 232 120 L 232 144 L 96 144 Z',
      fill: 'url(#brass-h)', stroke: 'var(--br-shade)', 'stroke-width': '1.5'
    }));
    // The sealed chamber: this is where the speaker feeds the instrument.
    svg.appendChild(s('rect', {
      x: '26', y: '104', width: '54', height: '58', rx: '9',
      fill: 'var(--surface)', stroke: 'var(--muted)', 'stroke-width': '2'
    }));
    svg.appendChild(s('path', {
      d: 'M80 116 L 96 124 L 96 140 L 80 150 Z',
      fill: 'var(--surface-2)', stroke: 'var(--muted)', 'stroke-width': '1.5'
    }));
    svg.appendChild(s('circle', {
      cx: '53', cy: '133', r: '17', fill: 'var(--surface-2)',
      stroke: 'var(--muted)', 'stroke-width': '1.5'
    }));
    svg.appendChild(s('circle', { cx: '53', cy: '133', r: '7', fill: 'var(--muted)', opacity: '.5' }));
    svg.appendChild(s('text', {
      x: '53', y: '182', 'text-anchor': 'middle', 'font-size': '11',
      fill: 'var(--muted)', text: 'chamber'
    }));

    // ---- tuning slide loop under the bell --------------------------------
    svg.appendChild(s('path', {
      d: 'M410 146 L 410 196 C 410 206, 418 212, 428 212 L 452 212 '
       + 'C 462 212, 470 206, 470 196 L 470 146',
      fill: 'none', stroke: 'var(--br-body-b)', 'stroke-width': '13',
      'stroke-linecap': 'round'
    }));

    // ---- valve casings + pistons ----------------------------------------
    pistons = [];
    for (let i = 0; i < count; i++) {
      const x = CASING_X[i] !== undefined ? CASING_X[i] : CASING_X[2] + 56 * (i - 2);
      const group = s('g', interactive ? { class: 'piston-btn' } : {});

      // casing
      group.appendChild(s('rect', {
        x, y: CASING_Y, width: CASING_W, height: CASING_H, rx: '7',
        fill: 'url(#brass-h)', stroke: 'var(--br-shade)', 'stroke-width': '2'
      }));
      // valve slide dropping out of the casing
      group.appendChild(s('path', {
        d: 'M' + (x + 10) + ' ' + (CASING_Y + CASING_H) + ' L ' + (x + 10) + ' '
         + (CASING_Y + CASING_H + 34 + i * 12) + ' C ' + (x + 10) + ' '
         + (CASING_Y + CASING_H + 46 + i * 12) + ', ' + (x + 32) + ' '
         + (CASING_Y + CASING_H + 46 + i * 12) + ', ' + (x + 32) + ' '
         + (CASING_Y + CASING_H + 34 + i * 12) + ' L ' + (x + 32) + ' '
         + (CASING_Y + CASING_H),
        fill: 'none', stroke: 'var(--br-body-b)', 'stroke-width': '9', 'stroke-linecap': 'round'
      }));

      // piston stem + button, translated when pressed
      const stem = s('rect', {
        x: x + 13, y: CASING_Y - 34, width: 16, height: 38, rx: '4',
        fill: 'var(--br-piston)', stroke: 'var(--br-piston-ink)', 'stroke-width': '1.5'
      });
      const cap = s('rect', {
        x: x + 4, y: CASING_Y - 48, width: 34, height: 18, rx: '6',
        fill: 'var(--br-piston)', stroke: 'var(--br-piston-ink)', 'stroke-width': '2'
      });
      const label = s('text', {
        x: x + 21, y: CASING_Y - 35, 'text-anchor': 'middle', 'font-size': '12',
        'font-weight': '700', fill: 'var(--br-piston-ink)', text: String(i + 1)
      });
      const moving = s('g', { transform: 'translate(0,0)' }, [stem, cap, label]);
      moving.style.transition = 'transform .09s ease-out';
      group.appendChild(moving);

      if (interactive) {
        const hit = s('rect', {
          x: x - 4, y: CASING_Y - 52, width: CASING_W + 8, height: CASING_H + 56,
          fill: 'transparent', style: 'cursor:pointer'
        });
        hit.addEventListener('pointerdown', (e) => {
          e.preventDefault();
          hit.setPointerCapture(e.pointerId);
          if (onPress) onPress(i, true);
        });
        const up = () => { if (onPress) onPress(i, false); };
        hit.addEventListener('pointerup', up);
        hit.addEventListener('pointercancel', up);
        group.appendChild(hit);
      }

      svg.appendChild(group);
      pistons.push({ moving, cap, stem, pressed: false });
    }

    root.appendChild(svg);
  }

  // `states` is the telemetry valve array.
  function update(states) {
    if (!states) return;
    for (let i = 0; i < pistons.length; i++) {
      const p = pistons[i];
      const v = states[i];
      const pressed = !!(v && v.pressed);
      const fault = !!(v && v.fault);
      if (pressed !== p.pressed) {
        p.pressed = pressed;
        p.moving.setAttribute('transform', pressed ? 'translate(0,17)' : 'translate(0,0)');
      }
      const fill = fault ? 'var(--error)' : (pressed ? 'var(--br-piston-down)' : 'var(--br-piston)');
      p.cap.setAttribute('fill', fill);
      p.stem.setAttribute('fill', pressed ? fill : 'var(--br-piston)');
    }
  }

  const api = { build, update, valveCount: () => count };
  return api;
})();
