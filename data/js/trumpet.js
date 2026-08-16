/* ===========================================================================
   trumpet.js — the playable picture of the instrument.

   A flat pictogram, in the language of the reference icon
   (glloq/General-Midi-Boop, images-a-faire/trumpet2.svg): five flat colours,
   no gradient, no outline detail, chunky rounded tubing. It is a drawing of
   the instrument, not a rendering of it — at the size this sits on the page,
   simple reads better than shaded.

   Everything except the pistons is decoration; the pistons are the working
   part. They follow the fingering of the note being played, and in MANUAL
   mode pressing one drives the real actuator through the WebSocket.

   Layout, bell to the right:

        chamber ─ leadpipe ──────────────╮
                     │                   │ tuning slide
                  [1][2][3]              │
                     ╰─ bell ─► ((( ─────╯

   Overlapping shapes are separated by a stroke in the surface colour rather
   than by an outline: that is what keeps a flat drawing readable when one
   gold shape crosses another, and it follows the page theme in the dark
   scheme too.
   =========================================================================== */
'use strict';

const Trumpet = (() => {
  let root = null;
  let pistons = [];
  let onPress = null;
  let interactive = false;
  let count = 3;

  // ---- geometry ------------------------------------------------------------
  const G = {
    view: { w: 700, h: 250 },
    // The block is anchored by its RIGHT edge, so changing the valve count
    // lengthens or shortens the leadpipe instead of moving the bell.
    casing: { w: 34, pitch: 50, right: 334, top: 68, bottom: 210, r: 8 },
    piston: { stemW: 11, stemTop: 26, capW: 28, capH: 13, travel: 15 },
    bell: { cy: 104, throatX: 424, mouthX: 620, r0: 9.5, r1: 82, rimRx: 25 },
    tube: 19,                       // stroke width of every plain tube
    leadpipe: { cy: 158, x0: 100 },
    ret: { cy: 194 },
    crook: { x: 540, bow: 30 },
    chamber: { x: 14, y: 124, w: 66, h: 68 }
  };

  const blockRight = () => G.casing.right;
  const casingX = (i) => G.casing.right - G.casing.w - (count - 1 - i) * G.casing.pitch;

  function build(container, options) {
    const o = options || {};
    root = container;
    count = Math.min(Math.max(o.valveCount || 3, 1), 4);
    interactive = !!o.interactive;
    onPress = o.onPress || null;
    render();
    return api;
  }

  const s = (tag, attrs, children) => UI.svg(tag, attrs, children);
  const n = (v) => Math.round(v * 10) / 10;

  // A plain tube: one flat stroke, round caps, with a gap stroke behind it so
  // it stays legible where it crosses something else.
  function tube(d, opts) {
    const o = opts || {};
    const w = o.width || G.tube;
    const body = s('path', { d, fill: 'none', stroke: 'var(--br-body)', 'stroke-width': w,
                             'stroke-linecap': 'round', 'stroke-linejoin': 'round' });
    // `joined` is for a tube that runs into another gold shape: a gap stroke
    // there would cut a white crescent out of it.
    if (o.joined) return body;
    return s('g', {}, [
      s('path', { d, fill: 'none', stroke: 'var(--surface)', 'stroke-width': w + 7,
                  'stroke-linecap': 'round', 'stroke-linejoin': 'round' }),
      body
    ]);
  }

  // ---- the instrument ------------------------------------------------------
  function render() {
    UI.clear(root);
    const svg = s('svg', {
      class: 'trumpet', viewBox: '0 0 ' + G.view.w + ' ' + G.view.h, role: 'img',
      'aria-label': 'Trumpet with ' + count + ' piston' + (count > 1 ? 's' : '')
    });

    // Back to front: the bell covers the far end of the tuning slide, and the
    // casings cover the leadpipe running behind them.
    svg.appendChild(tubing());
    svg.appendChild(bell());
    // After the bell: it runs into the throat, and drawing it on top keeps the
    // joint from showing as a break.
    svg.appendChild(tube('M' + (blockRight() - 10) + ' ' + G.bell.cy
                         + ' L ' + (G.bell.throatX + 26) + ' ' + G.bell.cy,
                         { joined: true }));
    svg.appendChild(chamber());
    pistons = [];
    for (let i = 0; i < count; i++) svg.appendChild(valve(i));

    root.appendChild(svg);
  }

  // Leadpipe, tuning slide and the return, as one continuous run plus the
  // short bell tube. Drawn as strokes, which is what gives the flat look.
  function tubing() {
    const l = G.leadpipe;
    const r = G.ret;
    const x = G.crook.x;
    const g = s('g', {});

    // One continuous run. It passes behind the casings and shows between them,
    // which is what ties the three bars to the instrument instead of leaving
    // them floating — the casing spacing is set so that gap is wide enough to
    // read.
    g.appendChild(tube(
      'M' + l.x0 + ' ' + l.cy + ' L ' + x + ' ' + l.cy
      + ' A ' + G.crook.bow + ' ' + n((r.cy - l.cy) / 2) + ' 0 0 1 ' + x + ' ' + r.cy
      + ' L ' + (blockRight() - 10) + ' ' + r.cy));

    return g;
  }

  // ---- bell ----------------------------------------------------------------
  // Exponential radius along the axis: a linear taper is a megaphone. The
  // mouth is an ellipse, so the drawing looks into the bore instead of showing
  // a flat triangle.
  function bell() {
    const b = G.bell;
    // r(t) = r0 + (r1-r0)*t^2.8 : nearly straight for most of the length, then
    // it opens fast. A linear taper is a megaphone.
    const radius = (t) => b.r0 + (b.r1 - b.r0) * Math.pow(t, 2.8);
    const upper = [];
    const lower = [];
    const steps = 20;
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const bx = b.throatX + t * (b.mouthX - b.throatX);
      const br = radius(t);
      upper.push(n(bx) + ' ' + n(b.cy - br));
      lower.push(n(bx) + ' ' + n(b.cy + br));
    }
    const flare = 'M' + upper.join(' L ')
      + ' A ' + b.rimRx + ' ' + b.r1 + ' 0 0 1 ' + n(b.mouthX) + ' ' + n(b.cy + b.r1)
      + ' L ' + lower.slice().reverse().join(' L ') + ' Z';

    // The bore is inset, so the gold reads as a rim all the way round the
    // mouth instead of only down one side.
    const boreRx = b.rimRx - 7;
    const boreRy = b.r1 - 10;
    // The one highlight: the lit inner wall, a sliver following the upper
    // edge over the last two thirds of the flare.
    const gloss = [];
    for (let i = 9; i <= steps; i++) {
      const t = i / steps;
      gloss.push(n(b.throatX + t * (b.mouthX - b.throatX) - 6) + ' '
                 + n(b.cy - radius(t) + 13));
    }

    return s('g', {}, [
      // Gap stroke, so the flare separates from the tuning slide behind it.
      s('path', { d: flare, fill: 'var(--br-body)', stroke: 'var(--surface)',
                  'stroke-width': '7', 'stroke-linejoin': 'round' }),
      s('path', { d: flare, fill: 'var(--br-body)' }),
      s('ellipse', { cx: n(b.mouthX - 3), cy: n(b.cy), rx: n(boreRx), ry: n(boreRy),
                     fill: 'var(--br-bore)' }),
      s('path', { d: 'M' + gloss.join(' L '), fill: 'none', stroke: 'var(--br-gloss)',
                  'stroke-width': '14', 'stroke-linecap': 'round',
                  'stroke-linejoin': 'round' })
    ]);
  }

  // ---- the chamber that stands in for the mouthpiece ------------------------
  function chamber() {
    const c = G.chamber;
    const cy = c.y + c.h / 2;
    const l = G.leadpipe;
    return s('g', {}, [
      s('rect', { x: c.x, y: c.y, width: c.w, height: c.h, rx: '12',
                  fill: 'var(--surface-2)', stroke: 'var(--muted)', 'stroke-width': '2' }),
      s('circle', { cx: c.x + c.w / 2, cy, r: '17', fill: 'var(--muted)', opacity: '.35' }),
      // The cone down to the leadpipe, in two steps: the compression stages
      // the acoustic model describes.
      s('path', {
        d: 'M' + (c.x + c.w) + ' ' + n(cy - 22) + ' L ' + (c.x + c.w + 22) + ' '
         + n(l.cy - 11) + ' L ' + (c.x + c.w + 22) + ' ' + n(l.cy + 11)
         + ' L ' + (c.x + c.w) + ' ' + n(cy + 22) + ' Z',
        fill: 'var(--surface-2)', stroke: 'var(--muted)', 'stroke-width': '2',
        'stroke-linejoin': 'round'
      }),
      s('text', { x: c.x + c.w / 2, y: c.y + c.h + 19, 'text-anchor': 'middle',
                  'font-size': '11', fill: 'var(--muted)', text: 'chamber' })
    ]);
  }

  // ---- one valve -----------------------------------------------------------
  function valve(i) {
    const c = G.casing;
    const p = G.piston;
    const x = casingX(i);
    const cx = x + c.w / 2;
    const g = s('g', interactive ? { class: 'piston-btn' } : {});

    // Piston first, so it sinks into the casing rather than sliding over it.
    const stem = s('rect', {
      x: n(cx - p.stemW / 2), y: p.stemTop, width: p.stemW, height: c.top - p.stemTop + 20,
      rx: n(p.stemW / 2), fill: 'var(--br-valve)'
    });
    const cap = s('rect', {
      x: n(cx - p.capW / 2), y: p.stemTop - p.capH, width: p.capW, height: p.capH,
      rx: n(p.capH / 2), fill: 'var(--br-valve)'
    });
    const label = s('text', {
      x: cx, y: n(p.stemTop - p.capH / 2 + 4), 'text-anchor': 'middle', 'font-size': '10',
      'font-weight': '800', fill: '#ffffff', text: String(i + 1)
    });
    const moving = s('g', { transform: 'translate(0,0)' }, [stem, cap, label]);
    moving.style.transition = 'transform .09s ease-out';
    g.appendChild(moving);

    // Casing, with the gap stroke that separates it from the tubes behind.
    g.appendChild(s('rect', {
      x, y: c.top, width: c.w, height: c.bottom - c.top, rx: c.r,
      fill: 'var(--br-body)', stroke: 'var(--surface)', 'stroke-width': '6'
    }));

    if (interactive) {
      const hit = s('rect', {
        x: x - 8, y: p.stemTop - p.capH - 8, width: c.w + 16,
        height: c.bottom - p.stemTop + p.capH + 16, fill: 'transparent',
        style: 'cursor:pointer'
      });
      hit.addEventListener('pointerdown', (e) => {
        e.preventDefault();
        hit.setPointerCapture(e.pointerId);
        if (onPress) onPress(i, true);
      });
      const up = () => { if (onPress) onPress(i, false); };
      hit.addEventListener('pointerup', up);
      hit.addEventListener('pointercancel', up);
      g.appendChild(hit);
    }

    pistons.push({ moving, cap, stem, pressed: false });
    return g;
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
        p.moving.setAttribute('transform',
                              pressed ? 'translate(0,' + G.piston.travel + ')' : 'translate(0,0)');
      }
      const fill = fault ? 'var(--error)'
                         : (pressed ? 'var(--br-piston-down)' : 'var(--br-valve)');
      p.cap.setAttribute('fill', fill);
      p.stem.setAttribute('fill', fill);
    }
  }

  const api = { build, update, valveCount: () => count };
  return api;
})();
