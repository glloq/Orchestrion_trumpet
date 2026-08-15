/* ===========================================================================
   ws.js - the live link.
   Notes, controllers and valve commands are sent here rather than through
   individual HTTP requests, and telemetry arrives the same way.
   Reconnects on its own: the hotspot drops whenever a phone sleeps.
   =========================================================================== */
'use strict';

const WS = (() => {
  let socket = null;
  let retryDelay = 800;
  let manualClose = false;
  const listeners = {};

  function emit(type, payload) {
    for (const handler of listeners[type] || []) handler(payload);
  }

  function connect() {
    manualClose = false;
    const host = location.hostname || '192.168.4.1';
    try {
      socket = new WebSocket('ws://' + host + ':81/');
    } catch (_) {
      scheduleRetry();
      return;
    }

    socket.onopen = () => {
      retryDelay = 800;
      emit('open');
    };
    socket.onclose = () => {
      emit('close');
      if (!manualClose) scheduleRetry();
    };
    socket.onerror = () => { /* onclose always follows */ };
    socket.onmessage = (event) => {
      let message;
      try { message = JSON.parse(event.data); } catch (_) { return; }
      if (message && message.t) emit(message.t, message);
    };
  }

  function scheduleRetry() {
    window.setTimeout(connect, retryDelay);
    // Back off gently so a sleeping phone does not hammer the ESP32 on wake.
    retryDelay = Math.min(retryDelay * 1.6, 8000);
  }

  function send(object) {
    if (!socket || socket.readyState !== WebSocket.OPEN) return false;
    socket.send(JSON.stringify(object));
    return true;
  }

  return {
    connect,
    close() { manualClose = true; if (socket) socket.close(); },
    on(type, handler) { (listeners[type] = listeners[type] || []).push(handler); },
    isOpen: () => !!socket && socket.readyState === WebSocket.OPEN,
    send,
    noteOn:  (note, velocity, channel) => send({ t: 'noteon',  n: note, v: velocity, c: channel || 1 }),
    noteOff: (note, channel)           => send({ t: 'noteoff', n: note, v: 0, c: channel || 1 }),
    cc:      (number, value, channel)  => send({ t: 'cc',      n: number, v: value, c: channel || 1 }),
    pitchBend: (value, channel)        => send({ t: 'pb',      v: value, c: channel || 1 }),
    valve:   (index, pressed)          => send({ t: 'valve',   i: index, p: pressed }),
    panic:   ()                        => send({ t: 'panic' }),
    monitor: (opts)                    => send(Object.assign({ t: 'monitor' }, opts))
  };
})();
