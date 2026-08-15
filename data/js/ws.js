/* ===========================================================================
   ws.js — the live link.

   Notes, controllers and valve commands go here rather than through individual
   HTTP requests, and telemetry arrives the same way. Reconnects on its own: a
   hotspot drops whenever a phone sleeps.

   With no device (mock mode) it emits the same telemetry frames from MOCK, so
   the interface behaves identically offline.
   =========================================================================== */
'use strict';

const WS = (() => {
  let socket = null;
  let retryDelay = 800;
  let manualClose = false;
  let mockTimer = 0;
  const listeners = {};

  function emit(type, payload) {
    for (const handler of listeners[type] || []) handler(payload);
  }

  function startMock() {
    if (mockTimer) return;
    emit('open');
    mockTimer = window.setInterval(() => emit('telemetry', MOCK.telemetry()), 250);
  }

  function connect() {
    manualClose = false;
    if (API.isMock() || location.protocol === 'file:') { startMock(); return; }

    const host = location.hostname || '192.168.4.1';
    try {
      socket = new WebSocket('ws://' + host + ':81/');
    } catch (_) {
      scheduleRetry();
      return;
    }
    socket.onopen = () => { retryDelay = 800; emit('open'); };
    socket.onclose = () => { emit('close'); if (!manualClose) scheduleRetry(); };
    socket.onerror = () => { /* onclose always follows */ };
    socket.onmessage = (event) => {
      let message;
      try { message = JSON.parse(event.data); } catch (_) { return; }
      if (message && message.t) emit(message.t, message);
    };
  }

  function scheduleRetry() {
    window.setTimeout(() => {
      // The REST layer may have decided in the meantime that there is no
      // device at all; follow it rather than hammering a socket that is not
      // there.
      if (API.isMock()) { startMock(); return; }
      connect();
    }, retryDelay);
    retryDelay = Math.min(retryDelay * 1.6, 8000);
  }

  function send(object) {
    if (API.isMock()) return applyMock(object);
    if (!socket || socket.readyState !== WebSocket.OPEN) return false;
    socket.send(JSON.stringify(object));
    return true;
  }

  // Offline, a note still has to light up the keyboard and move the pistons.
  function applyMock(message) {
    const s = MOCK.state;
    if (message.t === 'noteon') { s.note = message.n; s.velocity = message.v; }
    else if (message.t === 'noteoff' && s.note === message.n) { s.note = 0; s.velocity = 0; }
    else if (message.t === 'valve' && s.valves[message.i]) s.valves[message.i].pressed = message.p;
    else if (message.t === 'panic') {
      s.mode = 'PANIC';
      s.valves.forEach((v) => { v.pressed = false; });
      s.note = 0;
    }
    if (s.note) {
      s.frequency = 440 * Math.pow(2, (s.note - 69) / 12);
      // Mirror the fingering so the trumpet graphic reacts offline too.
      const V1 = 1, V2 = 2, V3 = 4;
      const chart = [V1|V2|V3, 0, V2|V3, V1|V2, V1, V2, 0, V1|V2|V3, V1|V3, V2|V3, V1|V2, V1,
                     V2, 0, V2|V3, V1|V2, V1, V2, 0, V1|V2, V1, V2, 0, V1, V2, 0, V2|V3,
                     V1|V2, V1, V2, 0];
      let idx = (s.note + 2) - 54;
      while (idx < 0) idx += 12;
      while (idx >= chart.length) idx -= 12;
      const mask = chart[idx];
      s.valves.forEach((v, i) => { v.pressed = (mask & (1 << i)) !== 0; });
    } else if (message.t === 'noteoff') {
      s.valves.forEach((v) => { v.pressed = false; });
      s.frequency = 0;
    }
    return true;
  }

  return {
    connect,
    close() { manualClose = true; if (socket) socket.close(); },
    on(type, handler) { (listeners[type] = listeners[type] || []).push(handler); },
    isOpen: () => API.isMock() || (!!socket && socket.readyState === WebSocket.OPEN),
    send,
    noteOn: (n, v, c) => send({ t: 'noteon', n, v, c: c || 1 }),
    noteOff: (n, c) => send({ t: 'noteoff', n, v: 0, c: c || 1 }),
    cc: (n, v, c) => send({ t: 'cc', n, v, c: c || 1 }),
    pitchBend: (v, c) => send({ t: 'pb', v, c: c || 1 }),
    valve: (i, p) => send({ t: 'valve', i, p }),
    panic: () => send({ t: 'panic' }),
    monitor: (opts) => send(Object.assign({ t: 'monitor' }, opts))
  };
})();
