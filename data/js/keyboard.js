/* ===========================================================================
   keyboard.js — the virtual keyboard of the Play page.

   Pointer events only, so mouse, touch and stylus behave identically, and
   every note travels over the WebSocket rather than through an HTTP request.
   Keys played from the browser and keys played from an external MIDI source
   are highlighted differently, so the page always shows what the instrument is
   really doing.
   =========================================================================== */
'use strict';

const Keyboard = (() => {
  const WHITE_OFFSETS = [0, 2, 4, 5, 7, 9, 11];
  const BLACK_AFTER = [0, 2, 5, 7, 9];   // white offsets that carry a black key
  const active = new Set();

  let root = null;
  let baseNote = 60;      // C4
  let octaves = 2;
  let velocity = 100;
  let channel = 1;
  let range = { min: 0, max: 127 };
  let externalNote = 0;

  function build(container, options) {
    root = container;
    const o = options || {};
    if (o.baseNote !== undefined) baseNote = o.baseNote;
    if (o.octaves) octaves = o.octaves;
    if (o.range) range = o.range;
    render();
    return api;
  }

  function render() {
    UI.clear(root);
    const whiteCount = 7 * octaves;
    const whiteWidth = 100 / whiteCount;

    let index = 0;
    for (let octave = 0; octave < octaves; octave++) {
      for (const offset of WHITE_OFFSETS) {
        const note = baseNote + octave * 12 + offset;
        root.appendChild(makeKey(note, 'white', index * whiteWidth, whiteWidth));
        index++;
      }
    }
    index = 0;
    for (let octave = 0; octave < octaves; octave++) {
      for (const offset of WHITE_OFFSETS) {
        if (BLACK_AFTER.indexOf(offset) !== -1) {
          const note = baseNote + octave * 12 + offset + 1;
          const left = (index + 1) * whiteWidth - whiteWidth * 0.29;
          root.appendChild(makeKey(note, 'black', left, whiteWidth * 0.58));
        }
        index++;
      }
    }
    if (externalNote) paint(externalNote, 'ext', true);
  }

  function makeKey(note, kind, leftPercent, widthPercent) {
    const outOfRange = note < range.min || note > range.max;
    const key = UI.el('div', {
      class: 'key ' + kind + (outOfRange ? ' out' : ''),
      'data-note': note,
      title: UI.noteName(note) + (outOfRange ? ' — outside the instrument range' : '')
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
    key.addEventListener('pointerleave', (e) => { if (e.buttons) release(note); });
    return key;
  }

  function paint(note, cls, on) {
    const node = root && root.querySelector('.key[data-note="' + note + '"]');
    if (node) node.classList.toggle(cls, on);
  }

  function press(note) {
    if (active.has(note)) return;
    active.add(note);
    paint(note, 'on', true);
    WS.noteOn(note, velocity, channel);
  }

  function release(note) {
    if (!active.has(note)) return;
    active.delete(note);
    paint(note, 'on', false);
    WS.noteOff(note, channel);
  }

  const api = {
    build,
    setVelocity: (v) => { velocity = v; },
    setChannel: (c) => { channel = c; },
    setRange(r) { range = r; if (root) render(); },
    shiftOctave(delta) {
      const next = baseNote + delta * 12;
      if (next < 12 || next > 96) return baseNote;
      releaseAll();
      baseNote = next;
      render();
      return baseNote;
    },
    baseNote: () => baseNote,
    releaseAll() { for (const note of Array.from(active)) release(note); },
    // Highlights the note actually sounding, whatever source it came from.
    setExternal(note) {
      if (note === externalNote) return;
      if (externalNote) paint(externalNote, 'ext', false);
      externalNote = note || 0;
      if (externalNote) paint(externalNote, 'ext', true);
    }
  };
  return api;
})();
