/* ===========================================================================
   trumpet.js — the playable picture of the instrument.

   The reference project draws the real fretboard rather than an abstract grid,
   so the screen and the machine look like the same object. The equivalent here
   is the trumpet itself.

   Three things decide whether a drawn trumpet reads as one:

   1. THE BELL PROFILE IS EXPONENTIAL. A cone is a megaphone. The radius here
      comes from r(t) = r0 * exp(k*t) sampled along the axis, so the tube stays
      close to its bore for most of its length and then opens very fast.

   2. THE MOUTH IS AN ELLIPSE. Drawn in strict orthographic side view a bell is
      a triangle and looks like a funnel; every photograph of a trumpet is
      slightly off-axis, so you see into the bore. The mouth is an ellipse with
      a dark interior, a lit far wall and a bright rolled rim.

   3. THE TUBES ARE CYLINDERS. A flat trapezoid with a hairline on it reads as
      a bar. Each tube gets a dark top edge, a narrow specular band in the
      upper third, and a dark bottom — which is what a lacquered brass tube
      actually does under a light.

   Layout, bell to the right:

        chamber ─ leadpipe ──────────────────╮
                     │                       │ main tuning slide, behind the bell
                  [1][2][3] valve block      │
                     │  ╰── valve slides     │
                     ╰─ bell tube ─► ((( bell

   It is a stylised side elevation, not a technical drawing: the tube routing
   inside the valve block is not shown, and the second valve slide — which on a
   real trumpet points towards the player — hangs down like the others so it
   reads in two dimensions.

   The mouthpiece is deliberately absent: this instrument is driven by the
   sealed chamber in its place, which is the whole point of the project.

   Pistons are also buttons: pressing one in MANUAL mode drives the real
   actuator through the WebSocket.
   =========================================================================== */
'use strict';

const Trumpet = (() => {
  let root = null;
  let pistons = [];
  let onPress = null;
  let interactive = false;
  let count = 3;

  // ---- geometry ------------------------------------------------------------
  // Proportions follow a Bb trumpet: ~490 mm long, 123 mm bell, 28 mm casings
  // 30 mm apart, 13 mm tubing. At this scale that is about 1.6 units per mm,
  // which is where every number below comes from.
  const G = {
    view: { w: 800, h: 380 },
    // The block is anchored by its RIGHT edge, so changing the valve count
    // lengthens or shortens the leadpipe instead of pushing the bell and the
    // tuning slide around. The casings very nearly touch, as they do on the
    // instrument: they are one block, not three posts.
    casing: { w: 44, pitch: 48, right: 380, top: 110, bottom: 250, capH: 16 },
    piston: { stemW: 14, stemTop: 58, buttonW: 32, buttonH: 19, travel: 17 },
    // The bell axis sits well above the tube stack, so the flare passes over
    // the tuning slide and the bow still shows beneath it — which is what it
    // does on the instrument, and what stops the bottom right becoming a blob.
    bell: { cy: 126, throatX: 470, mouthX: 694, r0: 14, r1: 100, rimRx: 36 },
    leadpipe: { cy: 196, r0: 8, r1: 10, x0: 104 },
    ret: { cy: 232, r: 10 },
    crook: { x: 592, bow: 52 },
    chamber: { x: 16, y: 160, w: 72, h: 72 },
    // The second valve slide is the shortest and the third the longest, as on
    // the instrument. A fourth valve is rare and sits between the two.
    slideDepth: [46, 26, 70, 36]
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

  // ---- drawing helpers -----------------------------------------------------
  const s = (tag, attrs, children) => UI.svg(tag, attrs, children);
  const n = (v) => Math.round(v * 10) / 10;

  // A horizontal tube. The gradient does the cylinder; this only has to give
  // it a body and, optionally, a taper.
  function hTube(x1, x2, cy, r, r2) {
    const rb = r2 === undefined ? r : r2;
    return s('path', {
      d: 'M' + n(x1) + ' ' + n(cy - r) + ' L ' + n(x2) + ' ' + n(cy - rb)
       + ' L ' + n(x2) + ' ' + n(cy + rb) + ' L ' + n(x1) + ' ' + n(cy + r) + ' Z',
      fill: 'url(#brass-tube)'
    });
  }

  // A tube following an arbitrary path: a dark casing stroke, the body, then a
  // specular stroke offset upwards. Same recipe as the gradient, by hand.
  function pathTube(d, width) {
    return s('g', {}, [
      s('path', { d, fill: 'none', stroke: 'var(--br-shade)', 'stroke-width': width + 2.5,
                  'stroke-linecap': 'round', 'stroke-linejoin': 'round' }),
      s('path', { d, fill: 'none', stroke: 'var(--br-body-a)', 'stroke-width': width,
                  'stroke-linecap': 'round', 'stroke-linejoin': 'round' }),
      s('path', { d, fill: 'none', stroke: 'var(--br-highlight)',
                  'stroke-width': Math.max(1.4, width * 0.22), 'stroke-linecap': 'round',
                  'stroke-linejoin': 'round', opacity: '.85',
                  transform: 'translate(0,' + n(-width * 0.27) + ')' }),
      s('path', { d, fill: 'none', stroke: 'var(--br-shade)',
                  'stroke-width': Math.max(1.2, width * 0.16), 'stroke-linecap': 'round',
                  'stroke-linejoin': 'round', opacity: '.5',
                  transform: 'translate(0,' + n(width * 0.36) + ')' })
    ]);
  }

  // The rings soldered at every tube joint. Cheap, and they are most of what
  // makes brass look like brass rather than like pipe.
  function ferrule(x, cy, r, w) {
    const width = w || 8;
    return s('rect', {
      x: n(x - width / 2), y: n(cy - r - 2), width, height: n(r * 2 + 4), rx: '2.5',
      fill: 'url(#brass-ring)', stroke: 'var(--br-shade)', 'stroke-width': '.9'
    });
  }

  // ---- the instrument ------------------------------------------------------
  function render() {
    UI.clear(root);
    const svg = s('svg', {
      class: 'trumpet', viewBox: '0 0 ' + G.view.w + ' ' + G.view.h, role: 'img',
      'aria-label': 'Trumpet with ' + count + ' piston' + (count > 1 ? 's' : '')
    });

    svg.appendChild(defs());
    // Order matters: the bell hides part of the tuning slide, exactly as it
    // does on the instrument, and the casings hide the leadpipe behind them.
    svg.appendChild(mainTuningSlide());
    svg.appendChild(leadpipe());
    svg.appendChild(returnTube());
    svg.appendChild(bellTube());
    svg.appendChild(bell());
    svg.appendChild(braces());
    svg.appendChild(chamber());

    pistons = [];
    for (let i = 0; i < count; i++) svg.appendChild(valve(i));

    root.appendChild(svg);
  }

  function defs() {
    return s('defs', {}, [
      // A lacquered brass cylinder lit from above: dark top edge, narrow
      // specular band in the upper third, dark underside.
      s('linearGradient', { id: 'brass-tube', x1: '0', y1: '0', x2: '0', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-shade)' }),
        s('stop', { offset: '9%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '22%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '36%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '64%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '88%', 'stop-color': 'var(--br-body-c)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-shade)' })
      ]),
      // The same cylinder standing up: lit from the left.
      s('linearGradient', { id: 'brass-cyl', x1: '0', y1: '0', x2: '1', y2: '0' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-shade)' }),
        s('stop', { offset: '10%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '25%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '40%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '72%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-c)' })
      ]),
      s('linearGradient', { id: 'brass-ring', x1: '0', y1: '0', x2: '0', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '20%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '55%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-c)' })
      ]),
      // The flare is a surface, not a tube: the light runs across its width.
      s('linearGradient', { id: 'brass-flare', x1: '0', y1: '0', x2: '0.15', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '14%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '34%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '66%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-c)' })
      ]),
      // Looking into the bore: dark in the middle, the far wall catching light.
      s('radialGradient', { id: 'bore', cx: '.62', cy: '.42', r: '.72' }, [
        s('stop', { offset: '0%', 'stop-color': '#2a1c06' }),
        s('stop', { offset: '55%', 'stop-color': 'var(--br-shade)' }),
        s('stop', { offset: '86%', 'stop-color': 'var(--br-body-c)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-a)' })
      ]),
      s('linearGradient', { id: 'nickel', x1: '0', y1: '0', x2: '1', y2: '0' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-piston-ink)' }),
        s('stop', { offset: '26%', 'stop-color': '#ffffff' }),
        s('stop', { offset: '58%', 'stop-color': 'var(--br-piston)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-piston-ink)' })
      ])
    ]);
  }

  // ---- bell ----------------------------------------------------------------
  // The radius along the axis. Exponential, so it stays near the bore for most
  // of its length and then opens fast: a linear taper is a megaphone.
  function bellRadius(t) {
    const b = G.bell;
    return b.r0 * Math.pow(b.r1 / b.r0, t);
  }

  function flareEdges() {
    const b = G.bell;
    const upper = [];
    const lower = [];
    const steps = 26;
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const x = b.throatX + t * (b.mouthX - b.throatX);
      const r = bellRadius(t);
      upper.push([x, b.cy - r]);
      lower.push([x, b.cy + r]);
    }
    return { upper, lower };
  }

  function bell() {
    const b = G.bell;
    const e = flareEdges();
    const g = s('g', {});
    const line = (pts) => pts.map((p) => n(p[0]) + ' ' + n(p[1])).join(' L ');

    // Outer surface: up the top edge, round the far half of the rim, back
    // along the bottom edge.
    g.appendChild(s('path', {
      d: 'M' + line(e.upper)
       + ' A ' + b.rimRx + ' ' + b.r1 + ' 0 0 1 ' + n(b.mouthX) + ' ' + n(b.cy + b.r1)
       + ' L ' + line(e.lower.slice().reverse()) + ' Z',
      fill: 'url(#brass-flare)', stroke: 'var(--br-shade)', 'stroke-width': '1.4'
    }));

    // The mouth. This is the whole reason the drawing reads as a trumpet and
    // not as a funnel: we are looking slightly into the bore.
    g.appendChild(s('ellipse', {
      cx: n(b.mouthX), cy: n(b.cy), rx: b.rimRx, ry: b.r1, fill: 'url(#bore)'
    }));
    // The far inner wall, catching the light.
    g.appendChild(s('path', {
      d: 'M' + n(b.mouthX) + ' ' + n(b.cy - b.r1 + 4)
       + ' A ' + (b.rimRx - 7) + ' ' + (b.r1 - 4) + ' 0 0 0 '
       + n(b.mouthX) + ' ' + n(b.cy + b.r1 - 4),
      fill: 'none', stroke: 'var(--br-body-a)', 'stroke-width': '9',
      'stroke-linecap': 'round', opacity: '.55'
    }));
    // Rolled rim, all the way round the mouth.
    g.appendChild(s('ellipse', {
      cx: n(b.mouthX), cy: n(b.cy), rx: b.rimRx, ry: b.r1, fill: 'none',
      stroke: 'url(#brass-ring)', 'stroke-width': '8'
    }));
    g.appendChild(s('ellipse', {
      cx: n(b.mouthX), cy: n(b.cy), rx: b.rimRx + 4, ry: b.r1 + 4, fill: 'none',
      stroke: 'var(--br-shade)', 'stroke-width': '1.3', opacity: '.75'
    }));

    // Specular sweep along the upper surface of the flare.
    const gloss = e.upper.map((p, i) => [p[0], p[1] + 6 + i * 0.55]);
    g.appendChild(s('path', {
      d: 'M' + line(gloss.slice(3)),
      fill: 'none', stroke: 'var(--br-highlight)', 'stroke-width': '10',
      'stroke-linecap': 'round', opacity: '.5'
    }));

    // Bell brace ring at the throat.
    g.appendChild(ferrule(b.throatX + 6, b.cy, b.r0 + 3, 10));
    return g;
  }

  function bellTube() {
    const b = G.bell;
    const x0 = blockRight();
    return s('g', {}, [
      hTube(x0 - 8, b.throatX + 8, b.cy, b.r0 - 2, b.r0),
      ferrule(x0 + 10, b.cy, b.r0 - 1)
    ]);
  }

  // ---- leadpipe, return, tuning slide --------------------------------------
  function leadpipe() {
    const l = G.leadpipe;
    const g = s('g', {});
    g.appendChild(hTube(l.x0, G.crook.x + 6, l.cy, l.r0, l.r1));
    g.appendChild(ferrule(l.x0 + 12, l.cy, l.r0 + 1, 10));
    // The outer sleeve of the tuning slide: a visibly thicker section the
    // inner tube slides into.
    g.appendChild(hTube(G.crook.x - 74, G.crook.x + 6, l.cy, l.r1 + 2.5));
    g.appendChild(ferrule(G.crook.x - 74, l.cy, l.r1 + 3));

    // Finger hook: a J on top of the leadpipe, just clear of the block, which
    // is where the right hand's ring finger goes. Drawn opening upwards so it
    // reads as a hook rather than as another brace.
    const hx = blockRight() + 30;
    g.appendChild(s('path', {
      d: 'M' + hx + ' ' + n(l.cy - l.r1) + ' L ' + hx + ' ' + n(l.cy - 26)
       + ' C ' + hx + ' ' + n(l.cy - 36) + ', ' + (hx + 17) + ' ' + n(l.cy - 36) + ', '
       + (hx + 17) + ' ' + n(l.cy - 26)
       + ' L ' + (hx + 17) + ' ' + n(l.cy - 20),
      fill: 'none', stroke: 'var(--br-body-b)', 'stroke-width': '4.5',
      'stroke-linecap': 'round'
    }));
    return g;
  }

  function returnTube() {
    const r = G.ret;
    const g = s('g', {});
    g.appendChild(hTube(blockRight() - 8, G.crook.x + 6, r.cy, r.r));
    g.appendChild(ferrule(blockRight() + 14, r.cy, r.r));
    g.appendChild(hTube(G.crook.x - 74, G.crook.x + 6, r.cy, r.r + 2.5));
    g.appendChild(ferrule(G.crook.x - 74, r.cy, r.r + 3));
    return g;
  }

  // The U at the bell end. Most of it sits behind the flare, which is where it
  // is on the instrument; the bottom of the bow stays visible.
  function mainTuningSlide() {
    const x = G.crook.x;
    const top = G.leadpipe.cy;
    const bot = G.ret.cy;
    const mid = (top + bot) / 2;
    const g = s('g', {});
    g.appendChild(pathTube(
      'M' + (x - 40) + ' ' + top + ' L ' + (x + 14) + ' ' + top
      + ' C ' + (x + 40) + ' ' + top + ', ' + (x + G.crook.bow) + ' ' + (mid - 9) + ', '
      + (x + G.crook.bow) + ' ' + mid
      + ' C ' + (x + G.crook.bow) + ' ' + (mid + 9) + ', ' + (x + 40) + ' ' + bot + ', '
      + (x + 14) + ' ' + bot
      + ' L ' + (x - 40) + ' ' + bot, 20));

    // Water key on the underside of the bow: saddle, lever, cork nipple.
    g.appendChild(s('rect', {
      x: x + 20, y: bot + 10, width: 17, height: 8, rx: '3.5',
      fill: 'url(#brass-ring)', stroke: 'var(--br-shade)', 'stroke-width': '1'
    }));
    g.appendChild(s('path', {
      d: 'M' + (x + 31) + ' ' + (bot + 18) + ' L ' + (x - 4) + ' ' + (bot + 29),
      stroke: 'var(--br-body-b)', 'stroke-width': '4', 'stroke-linecap': 'round'
    }));
    g.appendChild(s('circle', { cx: x - 4, cy: bot + 29, r: '4',
                                fill: 'var(--br-body-a)', stroke: 'var(--br-shade)',
                                'stroke-width': '1' }));
    return g;
  }

  // The flat bars that tie the bell, the leadpipe and the slide together.
  function braces() {
    const bar = (x, y1, y2, w) => s('rect', {
      x: n(x - (w || 10) / 2), y: n(y1), width: w || 10, height: n(y2 - y1), rx: '3',
      fill: 'url(#brass-ring)', stroke: 'var(--br-shade)', 'stroke-width': '1'
    });
    return s('g', {}, [
      bar(blockRight() + 62, G.bell.cy + G.bell.r0 - 2, G.leadpipe.cy - G.leadpipe.r0, 12),
      bar(G.crook.x - 92, G.leadpipe.cy + G.leadpipe.r1, G.ret.cy - G.ret.r)
    ]);
  }

  // ---- the chamber that stands in for the mouthpiece ------------------------
  function chamber() {
    const c = G.chamber;
    const cy = c.y + c.h / 2;
    const l = G.leadpipe;
    const g = s('g', {});
    g.appendChild(s('rect', {
      x: c.x, y: c.y, width: c.w, height: c.h, rx: '11',
      fill: 'var(--surface)', stroke: 'var(--muted)', 'stroke-width': '2'
    }));
    g.appendChild(s('circle', { cx: c.x + c.w / 2, cy, r: '22', fill: 'var(--surface-2)',
                                stroke: 'var(--muted)', 'stroke-width': '1.5' }));
    g.appendChild(s('circle', { cx: c.x + c.w / 2, cy, r: '8',
                                fill: 'var(--muted)', opacity: '.45' }));

    // The two compression stages the acoustic model describes: driver -> stage
    // 1 -> intermediate tube -> stage 2 -> leadpipe.
    const stage = (x1, r1, x2, r2, y1, y2) => s('path', {
      d: 'M' + n(x1) + ' ' + n(y1 - r1) + ' L ' + n(x2) + ' ' + n(y2 - r2)
       + ' L ' + n(x2) + ' ' + n(y2 + r2) + ' L ' + n(x1) + ' ' + n(y1 + r1) + ' Z',
      fill: 'var(--surface-2)', stroke: 'var(--muted)', 'stroke-width': '1.4'
    });
    const midX = c.x + c.w + 18;
    g.appendChild(stage(c.x + c.w, 26, midX, 13, cy, (cy + l.cy) / 2));
    g.appendChild(stage(midX, 13, midX + 8, 13, (cy + l.cy) / 2, (cy + l.cy) / 2 + 1));
    g.appendChild(stage(midX + 8, 13, l.x0 + 2, l.r0 + 2, (cy + l.cy) / 2 + 1, l.cy));

    g.appendChild(s('text', {
      x: c.x + c.w / 2, y: c.y + c.h + 20, 'text-anchor': 'middle', 'font-size': '11',
      fill: 'var(--muted)', text: 'chamber'
    }));
    return g;
  }

  // ---- one valve -----------------------------------------------------------
  function valve(i) {
    const c = G.casing;
    const p = G.piston;
    const x = casingX(i);
    const cx = x + c.w / 2;
    const g = s('g', interactive ? { class: 'piston-btn' } : {});

    // The slide first, so the bottom cap covers where its legs enter.
    const depth = G.slideDepth[i] || 36;
    const yTop = c.bottom + c.capH;
    const xl = x + 10;
    const xr = x + c.w - 10;
    g.appendChild(pathTube(
      'M' + xl + ' ' + yTop + ' L ' + xl + ' ' + (yTop + depth - 15)
      + ' C ' + xl + ' ' + (yTop + depth) + ', ' + xr + ' ' + (yTop + depth) + ', '
      + xr + ' ' + (yTop + depth - 15)
      + ' L ' + xr + ' ' + yTop, 10));
    if (i === 2) {
      const ry = yTop + depth - 28;
      g.appendChild(s('path', {
        d: 'M' + xl + ' ' + ry + ' L ' + (xl - 9) + ' ' + ry,
        stroke: 'var(--br-body-b)', 'stroke-width': '3.4', 'stroke-linecap': 'round'
      }));
      g.appendChild(s('circle', { cx: xl - 17, cy: ry, r: '8', fill: 'none',
                                  stroke: 'var(--br-body-b)', 'stroke-width': '3.4' }));
    }

    // Piston before the casing, so it genuinely sinks into it rather than
    // sliding across its face.
    const stem = s('rect', {
      x: n(cx - p.stemW / 2), y: p.stemTop, width: p.stemW, height: c.top - p.stemTop + 12,
      rx: '3', fill: 'url(#nickel)', stroke: 'var(--br-piston-ink)', 'stroke-width': '1.1'
    });
    const collar = s('rect', {
      x: n(cx - p.buttonW / 2 + 4), y: p.stemTop + 3, width: p.buttonW - 8, height: 7, rx: '2.5',
      fill: 'url(#brass-ring)', stroke: 'var(--br-shade)', 'stroke-width': '.9'
    });
    const cap = s('rect', {
      x: n(cx - p.buttonW / 2), y: p.stemTop - p.buttonH, width: p.buttonW, height: p.buttonH,
      rx: '7', fill: 'var(--br-piston)', stroke: 'var(--br-piston-ink)', 'stroke-width': '1.5'
    });
    const pearl = s('ellipse', {
      cx, cy: n(p.stemTop - p.buttonH / 2), rx: n(p.buttonW / 2 - 6), ry: n(p.buttonH / 2 - 4),
      fill: '#ffffff', opacity: '.55'
    });
    const label = s('text', {
      x: cx, y: n(p.stemTop - p.buttonH / 2 + 4), 'text-anchor': 'middle', 'font-size': '11',
      'font-weight': '700', fill: 'var(--br-piston-ink)', text: String(i + 1)
    });
    const moving = s('g', { transform: 'translate(0,0)' }, [stem, collar, cap, pearl, label]);
    moving.style.transition = 'transform .09s ease-out';
    g.appendChild(moving);

    // Casing and its two caps, over the stem.
    g.appendChild(s('rect', {
      x, y: c.top, width: c.w, height: c.bottom - c.top, rx: '3',
      fill: 'url(#brass-cyl)', stroke: 'var(--br-shade)', 'stroke-width': '1.4'
    }));
    for (const capY of [c.top - c.capH, c.bottom]) {
      g.appendChild(s('rect', {
        x: x - 3, y: capY, width: c.w + 6, height: c.capH, rx: '4',
        fill: 'url(#brass-cyl)', stroke: 'var(--br-shade)', 'stroke-width': '1.4'
      }));
      for (let k = 0; k < 5; k++) {
        const kx = x + 6 + k * (c.w - 12) / 4;
        g.appendChild(s('path', {
          d: 'M' + n(kx) + ' ' + (capY + 4) + ' L ' + n(kx) + ' ' + (capY + c.capH - 4),
          stroke: 'var(--br-shade)', 'stroke-width': '1', opacity: '.45'
        }));
      }
    }

    if (interactive) {
      const hit = s('rect', {
        x: x - 6, y: p.stemTop - p.buttonH - 8, width: c.w + 12,
        height: c.bottom + c.capH - p.stemTop + p.buttonH + 16,
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
      g.appendChild(hit);
    }

    pistons.push({ moving, cap, pressed: false });
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
      p.cap.setAttribute('fill', fault ? 'var(--error)'
                                       : (pressed ? 'var(--br-piston-down)' : 'var(--br-piston)'));
    }
  }

  const api = { build, update, valveCount: () => count };
  return api;
})();
