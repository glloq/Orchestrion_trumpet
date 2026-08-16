/* ===========================================================================
   trumpet.js — the playable picture of the instrument.

   The reference project draws the real fretboard rather than an abstract grid,
   so the screen and the machine look like the same object. The equivalent here
   is the trumpet itself, drawn as a proper side view rather than a diagram:

        chamber ─ leadpipe ─────────────────────────╮
                     │                              │  main tuning slide
                  [1][2][3]  valve casings          │
                     │  ╰── valve slides            │
                     ╰─ bell tube ──────► ((( bell  ╯

   It is a stylised side elevation, not a technical drawing: the tube routing
   inside the valve block is not shown, and the second valve slide — which on a
   real trumpet points towards the player — is drawn hanging down like the
   others so it reads in two dimensions. Everything else is where it is on the
   instrument: the bell above, the leadpipe and the tuning-slide return running
   beneath it to the crook at the far right, the casings in the middle with
   their slides hanging below, shortest on the second valve and longest on the
   third.

   The mouthpiece is deliberately absent — this instrument is driven by the
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
  // One place for every dimension, so the drawing can be re-proportioned
  // without hunting through path data.
  const G = {
    view: { w: 780, h: 340 },
    // The block is anchored by its RIGHT edge, so changing the valve count
    // lengthens or shortens the leadpipe instead of pushing the bell and the
    // tuning slide around.
    casing: { w: 46, pitch: 60, right: 416, top: 92, bottom: 226, capH: 18 },
    piston: { stemW: 15, stemTop: 44, buttonW: 34, buttonH: 20, travel: 18 },
    // Long tubes, by centre line and radius.
    bell:     { cy: 118, r: 11, throatX: 540 },
    leadpipe: { cy: 168, r0: 8, r1: 10, x0: 120 },
    ret:      { cy: 206, r: 10 },
    crook:    { x: 596, bow: 68 },
    chamber:  { x: 20, y: 132, w: 72, h: 72 },
    // Valve slide depth, per valve. The second is the shortest and the third
    // the longest on a real trumpet; a fourth valve is rare and sits between.
    slideDepth: [52, 30, 78, 40]
  };

  const blockRight = () => G.casing.right;
  const casingX = (i) =>
    G.casing.right - G.casing.w - (count - 1 - i) * G.casing.pitch;

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

  // A horizontal brass tube: body, specular line along the top, shadow along
  // the bottom. Drawing the highlight separately is what stops it reading as a
  // flat bar.
  function hTube(x1, x2, cy, r, opts) {
    const o = opts || {};
    const r2 = o.r2 === undefined ? r : o.r2;
    const body = s('path', {
      d: 'M' + x1 + ' ' + (cy - r) + ' L ' + x2 + ' ' + (cy - r2)
       + ' L ' + x2 + ' ' + (cy + r2) + ' L ' + x1 + ' ' + (cy + r) + ' Z',
      fill: 'url(#brass-v)'
    });
    const gloss = s('path', {
      d: 'M' + x1 + ' ' + (cy - r * 0.55) + ' L ' + x2 + ' ' + (cy - r2 * 0.55),
      stroke: 'var(--br-highlight)', 'stroke-width': Math.max(1.4, r * 0.34),
      'stroke-linecap': 'round', opacity: '.75', fill: 'none'
    });
    const shade = s('path', {
      d: 'M' + x1 + ' ' + (cy + r * 0.72) + ' L ' + x2 + ' ' + (cy + r2 * 0.72),
      stroke: 'var(--br-shade)', 'stroke-width': Math.max(1.2, r * 0.3),
      'stroke-linecap': 'round', opacity: '.5', fill: 'none'
    });
    return s('g', {}, [body, gloss, shade]);
  }

  // A brass tube following an arbitrary path, drawn as a fat stroke with a
  // thinner bright stroke on top of it.
  function pathTube(d, width) {
    return s('g', {}, [
      s('path', { d, fill: 'none', stroke: 'var(--br-shade)',
                  'stroke-width': width + 2, 'stroke-linecap': 'round',
                  'stroke-linejoin': 'round' }),
      s('path', { d, fill: 'none', stroke: 'url(#brass-v)',
                  'stroke-width': width, 'stroke-linecap': 'round',
                  'stroke-linejoin': 'round' }),
      s('path', { d, fill: 'none', stroke: 'var(--br-highlight)',
                  'stroke-width': Math.max(1.2, width * 0.24),
                  'stroke-linecap': 'round', 'stroke-linejoin': 'round',
                  opacity: '.5', transform: 'translate(0,' + (-width * 0.26) + ')' })
    ]);
  }

  // The little rings soldered at every tube joint. Cheap to draw and they are
  // most of what makes brass look like brass.
  function ferrule(x, cy, r, w) {
    return s('rect', {
      x: x - (w || 7) / 2, y: cy - r - 1.5, width: w || 7, height: r * 2 + 3, rx: '2',
      fill: 'url(#brass-ferrule)', stroke: 'var(--br-shade)', 'stroke-width': '.8'
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

    // Order matters: everything the casings should hide is drawn first.
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
      // Vertical: a lit tube seen from the side.
      s('linearGradient', { id: 'brass-v', x1: '0', y1: '0', x2: '0', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '16%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '44%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '76%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-shade)' })
      ]),
      // The casings are cylinders lit from the left.
      s('linearGradient', { id: 'brass-cyl', x1: '0', y1: '0', x2: '1', y2: '0' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-shade)' }),
        s('stop', { offset: '18%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '34%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '62%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-c)' })
      ]),
      s('linearGradient', { id: 'brass-ferrule', x1: '0', y1: '0', x2: '0', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '50%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-c)' })
      ]),
      // The flare catches light across its whole width, not just at the top.
      s('linearGradient', { id: 'brass-flare', x1: '0', y1: '0', x2: '0.25', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-highlight)' }),
        s('stop', { offset: '30%', 'stop-color': 'var(--br-body-a)' }),
        s('stop', { offset: '62%', 'stop-color': 'var(--br-body-b)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-shade)' })
      ]),
      // Looking into the bore: dark, with a rim of reflected light.
      s('radialGradient', { id: 'bore', cx: '.42', cy: '.42', r: '.75' }, [
        s('stop', { offset: '0%', 'stop-color': 'var(--br-shade)' }),
        s('stop', { offset: '62%', 'stop-color': 'var(--br-body-c)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-body-a)' })
      ]),
      s('linearGradient', { id: 'nickel', x1: '0', y1: '0', x2: '0', y2: '1' }, [
        s('stop', { offset: '0%', 'stop-color': '#ffffff' }),
        s('stop', { offset: '40%', 'stop-color': 'var(--br-piston)' }),
        s('stop', { offset: '100%', 'stop-color': 'var(--br-piston-ink)' })
      ])
    ]);
  }

  // The bell: throat, flare and the rolled rim, with the bore visible inside.
  function bell() {
    const b = G.bell;
    const x = b.throatX;
    const rimX = 706;
    const g = s('g', {});

    // Flare body. A trumpet bell stays close to its bore for most of its
    // length and then opens very fast, so the profile is two curves rather
    // than one: a slow taper, then the flare.
    g.appendChild(s('path', {
      d: 'M' + x + ' ' + (b.cy - b.r - 2)
       + ' C 606 ' + (b.cy - 18) + ', 652 ' + (b.cy - 32) + ', 680 ' + (b.cy - 58)
       + ' C 694 ' + (b.cy - 72) + ', 702 ' + (b.cy - 84) + ', ' + rimX + ' 28'
       + ' A 30 90 0 0 1 ' + rimX + ' 208'
       + ' C 702 ' + (b.cy + 84) + ', 694 ' + (b.cy + 72) + ', 680 ' + (b.cy + 58)
       + ' C 652 ' + (b.cy + 32) + ', 606 ' + (b.cy + 18) + ', ' + x + ' ' + (b.cy + b.r + 2)
       + ' Z',
      fill: 'url(#brass-flare)', stroke: 'var(--br-shade)', 'stroke-width': '1.6'
    }));

    // The bore seen through the mouth: a sliver of the far wall.
    g.appendChild(s('path', {
      d: 'M' + rimX + ' 28 A 30 90 0 0 1 ' + rimX + ' 208'
       + ' A 16 90 0 0 0 ' + rimX + ' 28 Z',
      fill: 'url(#bore)', opacity: '.9'
    }));

    // Rolled rim: a bright bead all the way round the mouth.
    g.appendChild(s('path', {
      d: 'M' + rimX + ' 28 A 30 90 0 0 1 ' + rimX + ' 208',
      fill: 'none', stroke: 'var(--br-highlight)', 'stroke-width': '7',
      'stroke-linecap': 'round', opacity: '.95'
    }));
    g.appendChild(s('path', {
      d: 'M' + rimX + ' 28 A 30 90 0 0 1 ' + rimX + ' 208',
      fill: 'none', stroke: 'var(--br-shade)', 'stroke-width': '1.6',
      'stroke-linecap': 'round', opacity: '.6'
    }));

    // Specular sweep across the upper half of the flare.
    g.appendChild(s('path', {
      d: 'M' + (x + 16) + ' ' + (b.cy - 6)
       + ' C 610 ' + (b.cy - 16) + ', 654 ' + (b.cy - 30) + ', 678 ' + (b.cy - 54)
       + ' C 692 ' + (b.cy - 70) + ', 698 ' + (b.cy - 78) + ', ' + (rimX - 6) + ' 40',
      fill: 'none', stroke: 'var(--br-highlight)', 'stroke-width': '9',
      'stroke-linecap': 'round', opacity: '.55'
    }));

    // Bell-brace ring where the flare meets the throat.
    g.appendChild(ferrule(x + 4, b.cy, b.r + 3, 9));
    return g;
  }

  function bellTube() {
    const b = G.bell;
    const x0 = blockRight();
    const g = s('g', {});
    // Slightly conical: the bore opens on its way to the flare.
    g.appendChild(hTube(x0 - 6, b.throatX + 4, b.cy, b.r - 1, { r2: b.r + 2 }));
    g.appendChild(ferrule(x0 + 8, b.cy, b.r));
    return g;
  }

  function leadpipe() {
    const l = G.leadpipe;
    const g = s('g', {});
    // Tapered, thin at the chamber and fuller at the tuning slide.
    g.appendChild(hTube(l.x0, G.crook.x + 4, l.cy, l.r0, { r2: l.r1 }));
    g.appendChild(ferrule(l.x0 + 10, l.cy, l.r0 + 1, 9));
    g.appendChild(ferrule(G.crook.x - 26, l.cy, l.r1));

    // Finger hook, just clear of the valve block, where the right hand sits.
    const hx = blockRight() + 22;
    g.appendChild(s('path', {
      d: 'M' + hx + ' ' + (l.cy + l.r1) + ' L ' + hx + ' ' + (l.cy + 26)
       + ' C ' + hx + ' ' + (l.cy + 36) + ', ' + (hx + 16) + ' ' + (l.cy + 36) + ', '
       + (hx + 16) + ' ' + (l.cy + 26),
      fill: 'none', stroke: 'var(--br-body-b)', 'stroke-width': '5',
      'stroke-linecap': 'round'
    }));
    return g;
  }

  function returnTube() {
    const r = G.ret;
    const g = s('g', {});
    g.appendChild(hTube(blockRight() - 6, G.crook.x + 4, r.cy, r.r));
    g.appendChild(ferrule(blockRight() + 12, r.cy, r.r));
    g.appendChild(ferrule(G.crook.x - 26, r.cy, r.r));
    return g;
  }

  // The big U at the bell end, under the flare, with its outer sleeves and a
  // water key on the bow.
  function mainTuningSlide() {
    const x = G.crook.x;
    const top = G.leadpipe.cy;
    const bot = G.ret.cy;
    const mid = (top + bot) / 2;
    const g = s('g', {});

    g.appendChild(pathTube(
      'M' + (x - 30) + ' ' + top + ' L ' + (x + 26) + ' ' + top
      + ' C ' + (x + 52) + ' ' + top + ', ' + (x + 62) + ' ' + (mid - 8) + ', '
      + (x + 62) + ' ' + mid
      + ' C ' + (x + 62) + ' ' + (mid + 8) + ', ' + (x + 52) + ' ' + bot + ', '
      + (x + 26) + ' ' + bot
      + ' L ' + (x - 30) + ' ' + bot, 19));

    // Water key: saddle, lever and the little cork nipple, on the underside of
    // the bow where it actually lives.
    g.appendChild(s('rect', {
      x: x + 34, y: bot + 9, width: 18, height: 8, rx: '3.5',
      fill: 'url(#brass-ferrule)', stroke: 'var(--br-shade)', 'stroke-width': '1'
    }));
    g.appendChild(s('path', {
      d: 'M' + (x + 46) + ' ' + (bot + 17) + ' L ' + (x + 8) + ' ' + (bot + 28),
      stroke: 'var(--br-body-b)', 'stroke-width': '4', 'stroke-linecap': 'round'
    }));
    g.appendChild(s('circle', { cx: x + 8, cy: bot + 28, r: '4',
                                fill: 'var(--br-body-a)', stroke: 'var(--br-shade)',
                                'stroke-width': '1' }));
    return g;
  }

  // The flat bars that hold the bell, the leadpipe and the slide together.
  function braces() {
    const g = s('g', {});
    const bar = (x, y1, y2, w) => s('rect', {
      x: x - (w || 9) / 2, y: y1, width: w || 9, height: y2 - y1, rx: '3',
      fill: 'url(#brass-ferrule)', stroke: 'var(--br-shade)', 'stroke-width': '.9'
    });
    // Bell tube to leadpipe.
    g.appendChild(bar(492, G.bell.cy + G.bell.r, G.leadpipe.cy - G.leadpipe.r1, 11));
    // Leadpipe to tuning-slide return.
    g.appendChild(bar(G.crook.x - 62, G.leadpipe.cy + G.leadpipe.r1, G.ret.cy - G.ret.r));
    return g;
  }

  // The sealed chamber that stands in for the mouthpiece: this is where the
  // loudspeaker feeds the instrument.
  function chamber() {
    const c = G.chamber;
    const cy = c.y + c.h / 2;
    const g = s('g', {});
    g.appendChild(s('rect', {
      x: c.x, y: c.y, width: c.w, height: c.h, rx: '11',
      fill: 'var(--surface)', stroke: 'var(--muted)', 'stroke-width': '2'
    }));
    g.appendChild(s('circle', { cx: c.x + c.w / 2, cy, r: '22',
                                fill: 'var(--surface-2)', stroke: 'var(--muted)',
                                'stroke-width': '1.5' }));
    g.appendChild(s('circle', { cx: c.x + c.w / 2, cy, r: '8',
                                fill: 'var(--muted)', opacity: '.45' }));
    // The two compression stages, drawn as the acoustic model describes them:
    // driver -> stage 1 -> intermediate tube -> stage 2 -> leadpipe.
    const midX = c.x + c.w + 20;
    const endX = G.leadpipe.x0 + 2;
    const ly = G.leadpipe.cy;
    const lr = G.leadpipe.r0 + 2;
    const stage = (x1, r1, x2, r2, y1, y2) => s('path', {
      d: 'M' + x1 + ' ' + (y1 - r1) + ' L ' + x2 + ' ' + (y2 - r2)
       + ' L ' + x2 + ' ' + (y2 + r2) + ' L ' + x1 + ' ' + (y1 + r1) + ' Z',
      fill: 'var(--surface-2)', stroke: 'var(--muted)', 'stroke-width': '1.4'
    });
    g.appendChild(stage(c.x + c.w, 26, midX, 13, cy, (cy + ly) / 2));
    g.appendChild(stage(midX, 13, midX + 8, 13, (cy + ly) / 2, (cy + ly) / 2 + 2));
    g.appendChild(stage(midX + 8, 13, endX, lr, (cy + ly) / 2 + 2, ly));
    g.appendChild(s('text', {
      x: c.x + c.w / 2, y: c.y + c.h + 20, 'text-anchor': 'middle', 'font-size': '11',
      fill: 'var(--muted)', text: 'chamber'
    }));
    return g;
  }

  // One casing, its piston and its slide.
  function valve(i) {
    const c = G.casing;
    const p = G.piston;
    const x = casingX(i);
    const cx = x + c.w / 2;
    const g = s('g', interactive ? { class: 'piston-btn' } : {});

    // Slide hanging below, drawn first so the bottom cap covers its legs.
    const depth = G.slideDepth[i] || 40;
    const yTop = c.bottom + c.capH;
    const xl = x + 11;
    const xr = x + c.w - 11;
    g.appendChild(pathTube(
      'M' + xl + ' ' + yTop + ' L ' + xl + ' ' + (yTop + depth - 16)
      + ' C ' + xl + ' ' + (yTop + depth) + ', ' + xr + ' ' + (yTop + depth) + ', '
      + xr + ' ' + (yTop + depth - 16)
      + ' L ' + xr + ' ' + yTop, 11));
    // The third slide carries the ring the player pulls it with.
    if (i === 2) {
      const ry = yTop + depth - 30;
      g.appendChild(s('path', {
        d: 'M' + xl + ' ' + ry + ' L ' + (xl - 10) + ' ' + ry,
        stroke: 'var(--br-body-b)', 'stroke-width': '3.4', 'stroke-linecap': 'round'
      }));
      g.appendChild(s('circle', {
        cx: xl - 18, cy: ry, r: '8', fill: 'none',
        stroke: 'var(--br-body-b)', 'stroke-width': '3.4'
      }));
    }

    // Piston first, casing on top of it: the stem then genuinely disappears
    // into the casing as the piston goes down, instead of sliding across its
    // face. It is drawn long enough to stay covered at full travel.
    const stem = s('rect', {
      x: cx - p.stemW / 2, y: p.stemTop, width: p.stemW,
      height: c.top - p.stemTop + 10,
      rx: '3', fill: 'url(#nickel)', stroke: 'var(--br-piston-ink)', 'stroke-width': '1.2'
    });
    const collar = s('rect', {
      x: cx - p.buttonW / 2 + 4, y: p.stemTop + 4, width: p.buttonW - 8, height: 7, rx: '2.5',
      fill: 'url(#brass-ferrule)', stroke: 'var(--br-shade)', 'stroke-width': '.8'
    });
    const cap = s('rect', {
      x: cx - p.buttonW / 2, y: p.stemTop - p.buttonH, width: p.buttonW, height: p.buttonH,
      rx: '7', fill: 'var(--br-piston)', stroke: 'var(--br-piston-ink)', 'stroke-width': '1.6'
    });
    // The mother-of-pearl inlay in the top of the button.
    const pearl = s('ellipse', {
      cx, cy: p.stemTop - p.buttonH / 2, rx: p.buttonW / 2 - 7, ry: p.buttonH / 2 - 4,
      fill: '#ffffff', opacity: '.55'
    });
    const label = s('text', {
      x: cx, y: p.stemTop - p.buttonH / 2 + 4, 'text-anchor': 'middle', 'font-size': '11',
      'font-weight': '700', fill: 'var(--br-piston-ink)', text: String(i + 1)
    });

    const moving = s('g', { transform: 'translate(0,0)' }, [stem, collar, cap, pearl, label]);
    moving.style.transition = 'transform .09s ease-out';
    g.appendChild(moving);

    // Casing body and its two caps, over the stem.
    g.appendChild(s('rect', {
      x, y: c.top, width: c.w, height: c.bottom - c.top, rx: '4',
      fill: 'url(#brass-cyl)', stroke: 'var(--br-shade)', 'stroke-width': '1.6'
    }));
    g.appendChild(s('rect', {
      x: x - 3, y: c.top - c.capH, width: c.w + 6, height: c.capH, rx: '5',
      fill: 'url(#brass-cyl)', stroke: 'var(--br-shade)', 'stroke-width': '1.6'
    }));
    g.appendChild(s('rect', {
      x: x - 3, y: c.bottom, width: c.w + 6, height: c.capH, rx: '5',
      fill: 'url(#brass-cyl)', stroke: 'var(--br-shade)', 'stroke-width': '1.6'
    }));
    // Knurling, the way a valve cap is actually machined.
    for (let k = 0; k < 5; k++) {
      const kx = x + 6 + k * (c.w - 12) / 4;
      g.appendChild(s('path', {
        d: 'M' + kx + ' ' + (c.top - c.capH + 4) + ' L ' + kx + ' ' + (c.top - 4),
        stroke: 'var(--br-shade)', 'stroke-width': '1', opacity: '.45'
      }));
      g.appendChild(s('path', {
        d: 'M' + kx + ' ' + (c.bottom + 4) + ' L ' + kx + ' ' + (c.bottom + c.capH - 4),
        stroke: 'var(--br-shade)', 'stroke-width': '1', opacity: '.45'
      }));
    }

    if (interactive) {
      const hit = s('rect', {
        x: x - 8, y: p.stemTop - p.buttonH - 8, width: c.w + 16,
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
      const fill = fault ? 'var(--error)' : (pressed ? 'var(--br-piston-down)' : 'var(--br-piston)');
      p.cap.setAttribute('fill', fill);
    }
  }

  const api = { build, update, valveCount: () => count };
  return api;
})();
