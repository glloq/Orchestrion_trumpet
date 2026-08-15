/* ===========================================================================
   keyboard.js - the virtual keyboard of the Play page.
   Pointer events only, so mouse, touch and stylus behave identically, and
   every note travels over the WebSocket.
   =========================================================================== */
'use strict';

const Keyboard = (() => {
  const WHITE_OFFSETS = [0, 2, 4, 5, 7, 9, 11];
  const BLACK_OFFSETS = [1, 3, 6, 8, 10];
  const active = new Set();

  let root = null;
  let baseNote = 60;      // C4
  let octaves = 2;
  let velocity = 100;
  let channel = 1;
  let range = { min: 0, max: 127 };

  function build(container, options) {
    root = container;
    const o = options || {};
    baseNote = o.baseNote !== undefined ? o.baseNote : baseNote;
    octaves = o.octaves || octaves;
    range = o.range || range;
    render();
    return api;
  }

  function render() {
    UI.clear(root);
    const whiteCount = 7 * octaves;
    const whiteWidth = 100 / whiteCount;
    let whiteIndex = 0;

    for (let octave = 0; octave < octaves; octave++) {
      for (const offset of WHITE_OFFSETS) {
        const note = baseNote + octave * 12 + offset;
        root.appendChild(makeKey(note, 'white', whiteIndex * whiteWidth, whiteWidth));
        whiteIndex++;
      }
    }
    // Black keys are positioned from the white key that precedes them.
    whiteIndex = 0;
    for (let octave = 0; octave < octaves; octave++) {
      for (let i = 0; i < WHITE_OFFSETS.length; i++) {
        const offset = WHITE_OFFSETS[i];
        if (BLACK_OFFSETS.indexOf(offset + 1) !== -1) {
          const note = baseNote + octave * 12 + offset + 1;
          const left = (whiteIndex + 1) * whiteWidth - whiteWidth * 0.3;
          root.appendChild(makeKey(note, 'black', left, whiteWidth * 0.6));
        }
        whiteIndex++;
      }
    }
  }

  function makeKey(note, kind, leftPercent, widthPercent) {
    const outOfRange = note < range.min || note > range.max;
    const key = UI.el('div', {
      class: 'key ' + kind + (outOfRange ? ' out' : ''),
      'data-note': note,
      title: UI.noteName(note) + (outOfRange ? ' (outside the instrument range)' : '')
    }, [UI.el('span', { text: kind === 'white' ? UI.noteName(note) : '' })]);
    key.style.left = leftPercent + '%';
    key.style.width = widthPercent + '%';
    if (active.has(note)) key.classList.add('on');

    key.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      key.setPointerCapture(e.pointerId);
      press(note);
    });
    key.addEventListener('pointerup', () => release(note));
    key.addEventListener('pointercancel', () => release(note));
    key.addEventListener('pointerleave', (e) => {
      if (e.buttons) release(note);
    });
    return key;
  }

  function paint(note, on) {
    const node = root && root.querySelector('.key[data-note="' + note + '"]');
    if (node) node.classList.toggle('on', on);
  }

  function press(note) {
    if (active.has(note)) return;
    active.add(note);
    paint(note, true);
    WS.noteOn(note, velocity, channel);
  }

  function release(note) {
    if (!active.has(note)) return;
    active.delete(note);
    paint(note, false);
    WS.noteOff(note, channel);
  }

  const api = {
    build,
    setVelocity: (v) => { velocity = v; },
    setChannel: (c) => { channel = c; },
    setRange: (r) => { range = r; if (root) render(); },
    shiftOctave(delta) {
      const next = baseNote + delta * 12;
      if (next < 12 || next > 108) return baseNote;
      baseNote = next;
      render();
      return baseNote;
    },
    baseNote: () => baseNote,
    releaseAll() {
      for (const note of Array.from(active)) release(note);
    },
    // Highlights a note played from an external MIDI source.
    highlightExternal(note, on) { paint(note, on); }
  };

  return api;
})();
