/* ===========================================================================
   settings.js — everything that is configured rather than played.

   A modal with tabs, exactly like the reference project: the three navigation
   entries stay about the instrument, and the settings live one gear away.
   Rare or dangerous parameters fold into disclosures inside each tab, so the
   first screen of every tab is the one a normal build needs.
   =========================================================================== */
'use strict';

const Settings = (() => {
  const TABS = ['Device', 'MIDI', 'Audio', 'Pistons', 'Diagnostics', 'Firmware'];
  let current = 'Device';
  let root = null;

  const cfg = () => App.config();
  const hw = () => App.hardware();
  const set = (path, value) => App.set(path, value);

  function open(tab) {
    current = tab || 'Device';
    render();
  }

  function close() {
    UI.clear(document.getElementById('modal-root'));
    root = null;
  }

  function render() {
    const host = document.getElementById('modal-root');
    UI.clear(host);

    const body = UI.el('div', { class: 'modal-body' });
    const tabs = UI.el('div', { class: 'modal-tabs' }, TABS.map((name) =>
      UI.el('button', {
        class: 'modal-tab' + (name === current ? ' active' : ''), text: name,
        onclick: () => { current = name; render(); }
      })));

    root = UI.el('div', { class: 'overlay', onclick: (e) => {
      if (e.target === root) close();
    } }, [
      UI.el('div', { class: 'modal' }, [
        UI.el('div', { class: 'modal-head' }, [
          UI.el('h2', { text: 'Settings' }),
          UI.el('button', { class: 'icon-btn', text: '✕', onclick: close })
        ]),
        tabs,
        body,
        UI.el('div', { class: 'modal-foot' }, [
          UI.el('span', { class: 'hint',
                          text: API.isMock() ? 'Demo / mock backend' : 'Live device' }),
          UI.el('div', { class: 'btn-row' }, [
            UI.el('button', { class: 'btn', text: 'Close', onclick: close }),
            UI.el('button', { class: 'btn primary', text: 'Save and apply',
                              onclick: () => App.save().then(() => close()) })
          ])
        ])
      ])
    ]);
    host.appendChild(root);

    (BUILDERS[current] || (() => {}))(body);
  }

  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && root) close(); });

  // =========================================================================
  // Device — name, Wi-Fi, hotspot
  // =========================================================================
  function deviceTab(body) {
    const w = cfg().wifi;
    const status = App.status() || {};
    const net = status.network || {};

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Instrument' }));
    body.appendChild(UI.field('Name', UI.text(cfg().system.deviceName,
      (v) => set('system.deviceName', v), { maxlength: 31 }),
      'Shown in the title bar and used to introduce the instrument on the network.'));

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Wi-Fi' }));
    body.appendChild(UI.el('p', { class: 'help',
      text: 'Stored on the device itself, never inside an exported configuration, so it survives '
          + 'a profile change and a shared config file cannot leak your network password. '
          + 'If the network cannot be joined the instrument falls back to its own hotspot.' }));

    body.appendChild(UI.readout('Connected to',
      net.sta ? UI.el('span', { class: 'v', text: (w.ssid || 'a network') })
              : UI.el('span', { class: 'v', text: 'its own hotspot “' + (net.ssid || '—') + '”' }),
      UI.el('button', { class: 'btn small', text: 'Change network',
                        onclick: () => openNetworkPicker(body) })));
    body.appendChild(UI.readout('Reachable at',
      UI.el('span', { class: 'v', text: (net.ip || '—')
        + (w.hostname ? '  ·  http://' + w.hostname + '.local' : '') })));
    body.appendChild(UI.el('div', { id: 'net-picker' }));

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Fallback hotspot' }));
    body.appendChild(UI.readout('Hotspot name',
      UI.el('span', { class: 'v', text: net.ssid || 'MIDI-Trumpet-XXXX' }),
      UI.badge(net.apSecured ? 'WPA2' : 'open', net.apSecured ? 'ok' : 'warn')));
    body.appendChild(UI.el('div', { class: 'btn-row', style: 'margin:10px 0' }, [
      UI.el('button', { class: 'btn', text: 'Start the hotspot now', onclick: async () => {
        try {
          const r = await API.wifiHotspot();
          UI.toast(r.note || 'Hotspot starting', 'ok');
        } catch (err) { UI.toast(err.message, 'bad'); }
      } })
    ]));
    body.appendChild(UI.el('p', { class: 'help',
      text: 'Joining the instrument’s Wi-Fi opens this page automatically. The hotspot is '
          + 'also available by holding the board’s BOOT button for about two seconds — that '
          + 'is the way back in when the stored network credentials are wrong.' }));

    body.appendChild(UI.disclosure('Advanced network options', () => {
      const wrap = UI.el('div');
      wrap.appendChild(UI.field('Wi-Fi mode', UI.select([
        { value: 'AP', label: 'Hotspot only' },
        { value: 'STA', label: 'Join an existing network' },
        { value: 'AP_STA', label: 'Both at once' }
      ], w.mode, (v) => set('wifi.mode', v))));
      wrap.appendChild(UI.field('Hotspot name', UI.text(w.apSsid,
        (v) => set('wifi.apSsid', v), { maxlength: 31, placeholder: 'MIDI-Trumpet-XXXX' }),
        'Empty derives it from the board’s MAC address.'));
      wrap.appendChild(hotspotPasswordField());
      wrap.appendChild(UI.field('Hostname', UI.text(w.hostname,
        (v) => set('wifi.hostname', v), { maxlength: 31 }),
        'Reachable as http://<hostname>.local on a network with mDNS.'));
      wrap.appendChild(UI.field('Hotspot channel', UI.number(w.apChannel,
        (v) => set('wifi.apChannel', v), { min: 1, max: 13 })));
      wrap.appendChild(UI.toggle('Captive portal on the hotspot', w.captivePortal,
        (v) => set('wifi.captivePortal', v)));
      return wrap;
    }));
  }

  function hotspotPasswordField() {
    const w = cfg().wifi;
    const note = UI.el('div', { class: 'help',
      text: w.apPasswordSet ? 'A password is set. Type a new one to change it, or clear the field '
                            + 'and save to open the network.'
                            : 'Empty means an open hotspot, which is what a first setup needs.' });
    const input = UI.text('', (v) => { pendingApPassword = v; },
                          { password: true, maxlength: 63, placeholder: '8 characters minimum' });
    return UI.el('div', { class: 'field' }, [
      UI.el('label', {}, [document.createTextNode('Hotspot password'),
        UI.el('span', { class: 'ctrl-val', text: w.apPasswordSet ? 'set' : 'open' })]),
      input, note,
      UI.el('div', { class: 'btn-row', style: 'margin-top:8px' }, [
        UI.el('button', { class: 'btn small', text: 'Apply password', onclick: async () => {
          if (pendingApPassword && pendingApPassword.length < 8) {
            UI.toast('A hotspot password needs at least 8 characters, or none at all', 'bad');
            return;
          }
          try {
            await API.wifiCredentials({ apPassword: pendingApPassword || '' });
            UI.toast('Hotspot password stored — reboot to apply', 'ok');
            await App.reload();
          } catch (err) { UI.toast(err.message, 'bad'); }
        } })
      ])
    ]);
  }

  let pendingApPassword = '';
  let pendingPassword = '';

  async function openNetworkPicker(body) {
    const host = body.querySelector('#net-picker') || document.getElementById('net-picker');
    UI.clear(host);
    host.appendChild(UI.el('div', { class: 'help', text: 'Scanning…' }));

    let selected = cfg().wifi.ssid || '';
    const draw = (networks, scanning) => {
      UI.clear(host);
      if (scanning) {
        host.appendChild(UI.el('div', { class: 'help', text: 'Scanning…' }));
        return;
      }
      host.appendChild(UI.el('div', { class: 'section-title', text: 'Nearby networks' }));
      host.appendChild(UI.el('div', { class: 'net-list' }, networks.map((n) =>
        UI.el('div', {
          class: 'net-row' + (n.ssid === selected ? ' selected' : ''),
          onclick: () => { selected = n.ssid; draw(networks, false); }
        }, [
          UI.el('span', { class: 'ssid', text: n.ssid }),
          n.secured ? UI.badge('locked', 'flat') : UI.badge('open', 'warn'),
          UI.el('span', { class: 'meta', text: n.rssi + ' dBm · ch ' + n.channel })
        ]))));
      host.appendChild(UI.field('Password', UI.text('', (v) => { pendingPassword = v; },
        { password: true, maxlength: 63 })));
      host.appendChild(UI.el('div', { class: 'btn-row' }, [
        UI.el('button', { class: 'btn primary', text: 'Join this network', onclick: async () => {
          if (!selected) { UI.toast('Pick a network first', 'warn'); return; }
          try {
            await API.wifiCredentials({ ssid: selected, password: pendingPassword,
                                        mode: 'AP_STA' });
            UI.toast('Stored — the instrument joins “' + selected + '” after a reboot', 'ok');
            await App.reload();
            render();
          } catch (err) { UI.toast(err.message, 'bad'); }
        } }),
        UI.el('button', { class: 'btn', text: 'Rescan', onclick: () => poll(true) })
      ]));
    };

    async function poll(refresh) {
      try {
        const r = await API.wifiScan(refresh);
        draw(r.networks || [], r.scanning);
        if (r.scanning) window.setTimeout(() => poll(false), 900);
      } catch (err) {
        UI.clear(host);
        host.appendChild(UI.el('div', { class: 'help', text: err.message }));
      }
    }
    poll(true);
  }

  // =========================================================================
  // MIDI
  // =========================================================================
  function midiTab(body) {
    const caps = (hw() && hw().midi) || {};
    const m = cfg().midi;

    const port = (label, path, available, help) => {
      if (!available) {
        return UI.el('div', { class: 'field' }, [
          UI.el('div', { class: 'row' }, [
            UI.toggle(label, false, () => {}, true),
            UI.badge('not available on this board', 'flat')
          ]),
          help ? UI.el('div', { class: 'help', text: help }) : null
        ]);
      }
      return UI.el('div', { class: 'field' }, [
        UI.toggle(label, !!App.get(path), (v) => set(path, v)),
        help ? UI.el('div', { class: 'help', text: help }) : null
      ]);
    };

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Inputs' }));
    body.appendChild(port('USB-MIDI', 'midi.usb.in', caps.usbAvailable,
      'Class compliant: no driver on Windows, macOS, Linux, a Raspberry Pi or a DAW.'));
    body.appendChild(port('BLE MIDI', 'midi.ble.in', caps.bleAvailable));
    body.appendChild(port('RTP-MIDI over Wi-Fi', 'midi.rtp.in', caps.rtpAvailable,
      'Only the peer that opened the session may send notes.'));
    body.appendChild(port('DIN MIDI', 'midi.din.in', true,
      'The input must be opto-isolated on the hardware side.'));
    body.appendChild(port('Web keyboard', 'midi.web.in', true));

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Outputs' }));
    body.appendChild(port('DIN OUT', 'midi.din.out', true));
    body.appendChild(port('DIN THRU', 'midi.din.thru', true,
      'Echoes every received byte straight back out, before any interpretation.'));
    body.appendChild(port('BLE OUT', 'midi.ble.out', caps.bleAvailable));
    body.appendChild(port('USB OUT', 'midi.usb.out', caps.usbAvailable));

    body.appendChild(UI.el('div', { class: 'section-title', text: 'MIDI parameters' }));
    body.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Channel', UI.select(
        [{ value: '65535', label: 'OMNI — every channel' }].concat(
          Array.from({ length: 16 }, (_, i) => ({ value: String(1 << i),
                                                  label: 'Channel ' + (i + 1) }))),
        String(m.channelMask), (v) => set('midi.channelMask', parseInt(v, 10))),
        'The channel the instrument answers on.'),
      UI.field('Bluetooth name', UI.text(m.ble.name, (v) => set('midi.ble.name', v),
        { maxlength: 31 }), 'How the instrument appears to a BLE MIDI host.'),
      UI.field('Pitch bend range', UI.select(
        [1, 2, 3, 12].map((n) => ({ value: String(n), label: '± ' + n + ' semitones' })),
        String(cfg().audio.pitchBendRange), (v) => set('audio.pitchBendRange', parseInt(v, 10)))),
      UI.el('div', { class: 'field' }, [
        UI.el('label', { text: 'Loops' }),
        UI.toggle('Suppress MIDI loops', m.suppressLoops, (v) => set('midi.suppressLoops', v))
      ])
    ]));

    body.appendChild(UI.disclosure('Routing matrix', () => routingMatrix()));
    body.appendChild(UI.disclosure('Per-route filters (channel, transpose, velocity, range)',
                                   () => routeFilters()));
  }

  const ROUTE_SOURCES = ['USB', 'BLE', 'RTP', 'DIN', 'WEB'];
  const ROUTE_DESTS = ['SOUND_ENGINE', 'VALVE_ENGINE', 'USB', 'BLE', 'RTP', 'DIN'];
  const DEST_LABEL = { SOUND_ENGINE: 'Sound', VALVE_ENGINE: 'Valves', USB: 'USB out',
                       BLE: 'BLE out', RTP: 'RTP out', DIN: 'DIN out' };

  const findRoute = (source, destination) =>
    cfg().midi.routes.find((r) => r.source === source && r.destination === destination);

  function routingMatrix() {
    const wrap = UI.el('div');
    wrap.appendChild(UI.el('p', { class: 'help',
      text: 'Tick a cell to send everything a source receives to that destination.' }));
    const head = UI.el('tr', {}, [UI.el('th', { text: 'Source' })].concat(
      ROUTE_DESTS.map((d) => UI.el('th', { text: DEST_LABEL[d] }))));
    const rows = ROUTE_SOURCES.map((source) => {
      const cells = [UI.el('td', { text: source })];
      for (const dest of ROUTE_DESTS) {
        const input = UI.el('input', { type: 'checkbox' });
        const route = findRoute(source, dest);
        input.checked = !!(route && route.enabled);
        input.addEventListener('change', () => {
          let target = findRoute(source, dest);
          if (!target) {
            target = { source, destination: dest, enabled: false, channelMask: 65535,
                       transpose: 0, velocityCurve: 'LINEAR', fixedVelocity: 100,
                       noteMin: 0, noteMax: 127 };
            cfg().midi.routes.push(target);
          }
          target.enabled = input.checked;
          App.markDirty();
        });
        cells.push(UI.el('td', {}, [input]));
      }
      return UI.el('tr', {}, cells);
    });
    wrap.appendChild(UI.el('div', { class: 'table-wrap' },
      [UI.el('table', {}, [UI.el('thead', {}, [head]), UI.el('tbody', {}, rows)])]));
    return wrap;
  }

  function routeFilters() {
    const wrap = UI.el('div');
    const enabled = cfg().midi.routes.filter((r) => r.enabled);
    if (!enabled.length) {
      wrap.appendChild(UI.el('div', { class: 'help', text: 'No route is enabled.' }));
      return wrap;
    }
    for (const route of enabled) {
      wrap.appendChild(UI.el('div', { style: 'margin-bottom:14px' }, [
        UI.el('div', { class: 'section-title',
                       text: route.source + ' → ' + (DEST_LABEL[route.destination] || route.destination) }),
        UI.el('div', { class: 'form-grid' }, [
          UI.field('Transpose', UI.number(route.transpose,
            (v) => { route.transpose = v; App.markDirty(); }, { min: -48, max: 48 })),
          UI.field('Velocity curve', UI.select(
            ['LINEAR', 'SOFT', 'HARD', 'FIXED'].map((v) => ({ value: v, label: v.toLowerCase() })),
            route.velocityCurve, (v) => { route.velocityCurve = v; App.markDirty(); })),
          UI.field('Lowest note', UI.number(route.noteMin,
            (v) => { route.noteMin = v; App.markDirty(); }, { min: 0, max: 127 })),
          UI.field('Highest note', UI.number(route.noteMax,
            (v) => { route.noteMax = v; App.markDirty(); }, { min: 0, max: 127 }))
        ])
      ]));
    }
    return wrap;
  }

  // =========================================================================
  // Audio
  // =========================================================================
  function audioTab(body) {
    const a = cfg().audio;
    const catalogue = hw() || { audioBackends: [], amplifiers: [], speakers: [] };
    const backend = catalogue.audioBackends.find((b) => b.key === a.backend);

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Output chain' }));
    body.appendChild(UI.field('Audio backend', UI.select(
      catalogue.audioBackends.map((b) => ({
        value: b.key, disabled: !b.available,
        label: b.title + (b.available ? (b.maturity === 'STABLE' ? '' : ' — ' + b.maturity.toLowerCase())
                                      : ' (not available on this board)')
      })), a.backend, (v) => { set('audio.backend', v); render(); }),
      backend ? backend.summary : ''));

    if (backend && backend.maturity !== 'STABLE') {
      body.appendChild(UI.el('div', { class: 'issues' }, [
        UI.el('div', { class: 'issue', 'data-sev': 'WARNING' }, [
          UI.el('span', { class: 'tag', text: 'WARNING' }),
          UI.el('div', { text: backend.maturity === 'PROTOTYPE'
            ? 'This backend is prototype quality by design and is not meant for playing.'
            : 'Implemented from the datasheet and compiled, but not validated on silicon by the '
              + 'project. Report what you find.' })
        ])
      ]));
    }

    body.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Amplifier', UI.select(
        catalogue.amplifiers.map((x) => ({ value: x.key, label: x.key })),
        cfg().amplifier.type, (v) => {
          const preset = catalogue.amplifiers.find((x) => x.key === v);
          set('amplifier.type', v);
          if (preset && v !== 'CUSTOM') {
            set('amplifier.maxPower', preset.maxPower);
            set('amplifier.gainDb', preset.gainDb);
            set('amplifier.speakerImpedance', preset.impedance);
          }
          render();
        })),
      UI.field('Speaker', UI.select(
        catalogue.speakers.map((x) => ({ value: x.key, label: x.name })),
        cfg().speaker.profile, (v) => {
          const preset = catalogue.speakers.find((x) => x.key === v);
          set('speaker.profile', v);
          if (preset && v !== 'CUSTOM') {
            set('speaker.name', preset.name);
            set('speaker.impedance', preset.impedance);
            set('speaker.powerRms', preset.powerRms);
            set('speaker.powerMax', preset.powerMax);
            set('speaker.fsHz', preset.fsHz || 0);
            set('speaker.minFrequency', preset.minFrequency);
            set('speaker.recommendedHighPass', preset.recommendedHighPass);
            set('speaker.powerLimit', preset.powerLimit);
          }
          render();
        })),
      UI.field('Acoustic coupling', UI.select([
        { value: 'SEALED_CHAMBER', label: 'Sealed chamber → leadpipe' },
        { value: 'OPEN', label: 'Open (free air)' },
        { value: 'CUSTOM_CHAMBER', label: 'Custom chamber' }
      ], cfg().acoustic.coupling, (v) => set('acoustic.coupling', v))),
      UI.field('Sample rate', UI.select(
        [22050, 32000, 44100, 48000].map((n) => ({ value: String(n), label: n + ' Hz' })),
        String(a.sampleRate), (v) => set('audio.sampleRate', parseInt(v, 10)))),
      UI.field('Bit depth', UI.select(
        [16, 24, 32].map((n) => ({ value: String(n), label: n + ' bit' })),
        String(a.bitDepth), (v) => set('audio.bitDepth', parseInt(v, 10))),
        'A backend that cannot reach it reduces it and says so.'),
      UI.field('Speaker protection', UI.el('div', {}, [
        UI.toggle('Soft limiter', a.limiter.enabled, (v) => set('audio.limiter.enabled', v))
      ]), 'The safe peak level is derived from the speaker and the amplifier and cannot be '
        + 'disabled; only the soft limiter can.')
    ]));

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Sound' }));
    body.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Generator', UI.select([
        { value: 'ADDITIVE', label: 'Additive — recommended' },
        { value: 'WAVETABLE', label: 'Wavetable — cheaper on CPU' },
        { value: 'HYBRID', label: 'Hybrid' },
        { value: 'SINE', label: 'Sine — test tone' }
      ], a.engine, (v) => set('audio.engine', v))),
      UI.field('Harmonics', UI.number(a.additive.harmonicCount,
        (v) => set('audio.additive.harmonicCount', v), { min: 1, max: 16 })),
      UI.field('Vibrato source', UI.select([
        { value: 'CC1', label: 'Modulation wheel (CC1)' },
        { value: 'AFTERTOUCH', label: 'Channel pressure' },
        { value: 'AUTOMATIC', label: 'Always on' },
        { value: 'OFF', label: 'Off' }
      ], a.vibrato.source, (v) => set('audio.vibrato.source', v)))
    ]));

    body.appendChild(UI.disclosure('Envelope and vibrato', () => {
      const wrap = UI.el('div', { class: 'form-grid' }, [
        UI.field('Attack (ms)', UI.number(a.envelope.attackMs,
          (v) => set('audio.envelope.attackMs', v), { min: 1, max: 300 })),
        UI.field('Decay (ms)', UI.number(a.envelope.decayMs,
          (v) => set('audio.envelope.decayMs', v), { min: 1, max: 800 })),
        UI.field('Sustain', UI.number(a.envelope.sustain,
          (v) => set('audio.envelope.sustain', v), { min: 0, max: 1, step: 0.01 })),
        UI.field('Release (ms)', UI.number(a.envelope.releaseMs,
          (v) => set('audio.envelope.releaseMs', v), { min: 1, max: 1200 })),
        UI.field('Attack noise', UI.number(a.envelope.attackNoise,
          (v) => set('audio.envelope.attackNoise', v), { min: 0, max: 0.6, step: 0.01 })),
        UI.field('Breath noise', UI.number(a.envelope.breathNoise,
          (v) => set('audio.envelope.breathNoise', v), { min: 0, max: 0.3, step: 0.01 })),
        UI.field('Vibrato rate (Hz)', UI.number(a.vibrato.frequencyHz,
          (v) => set('audio.vibrato.frequencyHz', v), { min: 1, max: 12, step: 0.1 })),
        UI.field('Vibrato depth (cents)', UI.number(a.vibrato.depthCents,
          (v) => set('audio.vibrato.depthCents', v), { min: 0, max: 100 })),
        UI.field('Vibrato delay (ms)', UI.number(a.vibrato.delayMs,
          (v) => set('audio.vibrato.delayMs', v), { min: 0, max: 1500 }))
      ]);
      return wrap;
    }));

    body.appendChild(UI.disclosure('Chamber, cone and leadpipe', () => acousticEditor()));

    body.appendChild(UI.disclosure('I²S / I²C pins and DMA', () => UI.el('div', { class: 'form-grid' }, [
      UI.field('I²S BCLK', UI.number(a.i2s.bclk, (v) => set('audio.i2s.bclk', v), { min: -1, max: 48 })),
      UI.field('I²S WS / LRCK', UI.number(a.i2s.ws, (v) => set('audio.i2s.ws', v), { min: -1, max: 48 })),
      UI.field('I²S DOUT', UI.number(a.i2s.dout, (v) => set('audio.i2s.dout', v), { min: -1, max: 48 })),
      UI.field('I²S DIN (codec ADC)', UI.number(a.i2s.din, (v) => set('audio.i2s.din', v), { min: -1, max: 48 })),
      UI.field('I²S MCLK', UI.number(a.i2s.mclk, (v) => set('audio.i2s.mclk', v), { min: -1, max: 48 })),
      UI.field('DAC mute / SD_MODE', UI.number(a.sdModePin, (v) => set('audio.sdModePin', v), { min: -1, max: 48 })),
      UI.field('Codec I²C SDA', UI.number(a.i2c.sda, (v) => set('audio.i2c.sda', v), { min: -1, max: 48 })),
      UI.field('Codec I²C SCL', UI.number(a.i2c.scl, (v) => set('audio.i2c.scl', v), { min: -1, max: 48 })),
      UI.field('Codec address', UI.number(a.codecAddress, (v) => set('audio.codecAddress', v), { min: 0, max: 127 })),
      UI.field('Block size (frames)', UI.number(a.blockSize, (v) => set('audio.blockSize', v), { min: 32, max: 512, step: 32 })),
      UI.field('DMA buffers', UI.number(a.dmaBuffers, (v) => set('audio.dmaBuffers', v), { min: 2, max: 16 })),
      UI.field('High pass (Hz)', UI.number(a.highPassHz, (v) => set('audio.highPassHz', v), { min: 40, max: 600 }))
    ])));

    body.appendChild(UI.disclosure('Limiter internals', () => UI.el('div', { class: 'form-grid' }, [
      UI.field('Threshold (dB)', UI.number(a.limiter.thresholdDb,
        (v) => set('audio.limiter.thresholdDb', v), { min: -24, max: 0, step: 0.5 })),
      UI.field('Attack (ms)', UI.number(a.limiter.attackMs,
        (v) => set('audio.limiter.attackMs', v), { min: 0.1, max: 20, step: 0.1 })),
      UI.field('Release (ms)', UI.number(a.limiter.releaseMs,
        (v) => set('audio.limiter.releaseMs', v), { min: 5, max: 500, step: 5 })),
      UI.field('Hard ceiling', UI.number(a.limiter.hardCeiling,
        (v) => set('audio.limiter.hardCeiling', v), { min: 0.2, max: 1, step: 0.005 }))
    ])));
  }

  // The coupling geometry, and what the firmware derives from it.  Every
  // derived figure is labelled with where it came from: an estimate must never
  // be mistaken for a measurement.
  function acousticEditor() {
    const a = cfg().acoustic;
    const wrap = UI.el('div');

    if (a.coupling === 'OPEN') {
      wrap.appendChild(UI.el('p', { class: 'help',
        text: 'Open air: nothing loads the driver, so there is no geometry to describe.' }));
      return wrap;
    }

    const stage = (key, label) => UI.el('div', {}, [
      UI.el('div', { class: 'section-title', text: label }),
      UI.el('div', { class: 'form-grid' }, [
        UI.field('Inlet Ø (mm)', UI.number(a[key].inletDiameterMm,
          (v) => set('acoustic.' + key + '.inletDiameterMm', v), { min: 1, max: 200, step: 0.5 })),
        UI.field('Outlet Ø (mm)', UI.number(a[key].outletDiameterMm,
          (v) => set('acoustic.' + key + '.outletDiameterMm', v), { min: 1, max: 200, step: 0.5 })),
        UI.field('Length (mm)', UI.number(a[key].lengthMm,
          (v) => set('acoustic.' + key + '.lengthMm', v), { min: 1, max: 400, step: 0.5 }))
      ])
    ]);

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Chambers' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Rear chamber (ml)', UI.number(a.rearChamberVolumeMl,
        (v) => set('acoustic.rearChamberVolumeMl', v), { min: 0, max: 2000 }),
        'Sealed volume behind the cone.'),
      UI.field('Front chamber (ml)', UI.number(a.frontChamberVolumeMl,
        (v) => set('acoustic.frontChamberVolumeMl', v), { min: 0, max: 500 }),
        'Trapped volume in front of the cone, before the first stage.'),
      UI.field('Front chamber depth (mm)', UI.number(a.frontChamberDepthMm,
        (v) => set('acoustic.frontChamberDepthMm', v), { min: 0, max: 100, step: 0.5 }))
    ]));

    wrap.appendChild(stage('stage1', 'Stage 1 — driver to intermediate tube'));
    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Intermediate tube' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Diameter (mm)', UI.number(a.intermediateDiameterMm,
        (v) => set('acoustic.intermediateDiameterMm', v), { min: 1, max: 120, step: 0.5 })),
      UI.field('Length (mm)', UI.number(a.intermediateLengthMm,
        (v) => set('acoustic.intermediateLengthMm', v), { min: 0, max: 300, step: 0.5 }))
    ]));
    wrap.appendChild(stage('stage2', 'Stage 2 — intermediate tube to leadpipe'));

    wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Leadpipe and bench figures' }));
    wrap.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Leadpipe Ø (mm)', UI.number(a.leadpipeDiameterMm,
        (v) => set('acoustic.leadpipeDiameterMm', v), { min: 1, max: 60, step: 0.5 })),
      UI.field('Measured high pass (Hz)', UI.number(a.measuredHighPassHz,
        (v) => set('acoustic.measuredHighPassHz', v), { min: 0, max: 2000 }),
        '0 = derive it from the geometry. A bench measurement always wins.'),
      UI.field('Coupling gain (dB)', UI.number(a.eqGainDb,
        (v) => set('acoustic.eqGainDb', v), { min: -12, max: 12, step: 0.5 }))
    ]));

    // ---- what the model makes of it ----
    const model = MOCK.acousticModel(cfg());
    if (model) {
      const SOURCE = { MEASURED: 'measured at the bench', DERIVED: 'derived from the geometry',
                       SPEAKER_PROFILE: "the driver's own recommendation" };
      wrap.appendChild(UI.el('div', { class: 'section-title', text: 'Derived — not measured' }));
      wrap.appendChild(UI.kv([
        ['Compression ratio', model.compressionRatio.toFixed(1) + ' : 1'],
        ['Cone half angles', model.stage1HalfAngleDeg.toFixed(0) + '° / '
                             + model.stage2HalfAngleDeg.toFixed(0) + '°'],
        ['Path length', Math.round(model.totalPathLengthMm) + ' mm'],
        ['Front chamber corner', Math.round(model.frontChamberCornerHz) + ' Hz'],
        ['Sealed resonance', model.sealedResonanceKnown
            ? Math.round(model.sealedResonanceHz) + ' Hz'
            : 'unknown — enter the driver fs and Vas'],
        ['High pass in force', Math.round(model.highPassHz) + ' Hz · '
                               + (SOURCE[model.highPassSource] || model.highPassSource)]
      ]));
      wrap.appendChild(UI.el('p', { class: 'help',
        text: 'These follow from the dimensions above and from the speed of sound. They are '
            + 'starting points for the bench, not measurements: enter the measured high pass '
            + 'once you have swept the assembly and it takes over.' }));
    }
    return wrap;
  }

  // =========================================================================
  // Pistons
  // =========================================================================
  function pistonsTab(body) {
    const v = cfg().valves;

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Valves' }));
    body.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.field('Number of valves', UI.select(
        [1, 2, 3, 4].map((n) => ({ value: String(n), label: String(n) })), String(v.count),
        (value) => { set('valves.count', parseInt(value, 10)); render(); })),
      UI.field('Mode', UI.select([
        { value: 'AUTO', label: 'AUTO — follow the MIDI notes' },
        { value: 'MANUAL', label: 'MANUAL — from this interface' },
        { value: 'MIDI_CC', label: 'MIDI_CC — one controller per valve' },
        { value: 'DISABLED', label: 'DISABLED — valves parked' }
      ], v.mode, (value) => {
        set('valves.mode', value);
        API.valveMode(value).catch((e) => UI.toast(e.message, 'bad'));
        render();
      }))
    ]));

    for (let i = 0; i < v.count; i++) body.appendChild(valveEditor(v.items[i], i));

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Attack synchronisation' }));
    body.appendChild(UI.el('p', { class: 'help', style: 'margin-top:0',
      text: 'The pistons change the resonator, so a note that starts before they arrive is '
          + 'played through the wrong bore. The attack waits for the valves that actually have '
          + 'to move — and only for those.' }));
    const worst = Math.max(0, ...v.items.slice(0, v.count).map(settleEstimateMs));
    body.appendChild(UI.el('div', { class: 'form-grid' }, [
      UI.el('div', { class: 'field' }, [
        UI.el('label', { text: 'Synchronisation' }),
        UI.toggle('Wait for the pistons', v.sync.enabled,
                  (x) => { set('valves.sync.enabled', x); render(); }),
        UI.toggle('Only when the fingering changes', v.sync.onlyWhenFingeringChanges,
                  (x) => set('valves.sync.onlyWhenFingeringChanges', x))
      ]),
      UI.field('Trim (ms)', UI.number(v.sync.trimMs,
        (x) => set('valves.sync.trimMs', x), { min: -100, max: 200 }),
        'Added to the computed delay. Negative starts the note earlier.'),
      UI.field('Ceiling (ms)', UI.number(v.sync.maxDelayMs,
        (x) => set('valves.sync.maxDelayMs', x), { min: 0, max: 400 }),
        'Nothing is ever delayed by more than this.')
    ]));
    body.appendChild(UI.el('div', { class: 'help',
      text: 'Slowest piston as configured: about ' + worst + ' ms'
          + (worst > v.sync.maxDelayMs
              ? ' — above the ceiling, so the note will still start early.'
              : '.') }));

    body.appendChild(UI.disclosure('Servo and solenoid timing', () => UI.el('div', { class: 'form-grid' }, [
      UI.field('Servo PWM (Hz)', UI.number(v.servoFrequencyHz,
        (x) => set('valves.servoFrequencyHz', x), { min: 50, max: 333 })),
      UI.field('Solenoid PWM (Hz)', UI.number(v.solenoidPwmFrequencyHz,
        (x) => set('valves.solenoidPwmFrequencyHz', x), { min: 1000, max: 40000, step: 1000 }),
        'Above 20 kHz the PWM stays out of the audio band.'),
      UI.field('PCA9685 address', UI.number(v.pca9685Address,
        (x) => set('valves.pca9685Address', x), { min: 0, max: 127 })),
      UI.field('PCA9685 OE pin', UI.number(v.pca9685OePin,
        (x) => set('valves.pca9685OePin', x), { min: -1, max: 48 }),
        'Active low. Wire it: the firmware holds it high before the bus is configured.')
    ])));
  }

  // Mirrors valveSettleMs() in src/valves/ValveTiming.cpp.
  function settleEstimateMs(item) {
    if (item.measuredSettleMs > 0) return item.measuredSettleMs;
    if (item.type === 'SERVO') {
      if (!item.speed) return 400;
      return Math.min(400, Math.round(Math.abs(item.pressedAngle - item.releasedAngle)
                                      * 1000 / item.speed) + 12);
    }
    if (item.type === 'SOLENOID') return Math.min(400, item.pullInMs + 12);
    return 0;
  }

  function valveEditor(item, index) {
    const path = 'valves.items.' + index + '.';
    const fields = [
      UI.field('Actuator', UI.select([
        { value: 'SERVO', label: 'Servo' },
        { value: 'SOLENOID', label: 'Solenoid' },
        { value: 'DISABLED', label: 'Not fitted' }
      ], item.type, (v) => { set(path + 'type', v); render(); })),
      item.type === 'DISABLED' ? null : UI.field('Measured settle (ms)',
        UI.number(item.measuredSettleMs, (v) => { set(path + 'measuredSettleMs', v); render(); },
                  { min: 0, max: 400 }),
        '0 = estimate it (' + settleEstimateMs(item) + ' ms). Time the piston at the bench and '
        + 'enter it here: the estimate ignores load and linkage slop.')
    ];

    if (item.type === 'SERVO') {
      fields.push(UI.field('Driver', UI.select([
        { value: 'ESP32_PWM', label: 'ESP32 PWM' },
        { value: 'PCA9685', label: 'PCA9685' }
      ], item.driver, (v) => { set(path + 'driver', v); render(); })));
      fields.push(item.driver === 'PCA9685'
        ? UI.field('Channel', UI.number(item.channel, (v) => set(path + 'channel', v),
                                        { min: 0, max: 15 }))
        : UI.field('GPIO', UI.number(item.gpio, (v) => set(path + 'gpio', v),
                                     { min: -1, max: 48 })));
      fields.push(UI.field('Released angle', UI.number(item.releasedAngle,
        (v) => set(path + 'releasedAngle', v), { min: 0, max: 180 })));
      fields.push(UI.field('Pressed angle', UI.number(item.pressedAngle,
        (v) => set(path + 'pressedAngle', v), { min: 0, max: 180 })));
      fields.push(UI.field('Speed (°/s)', UI.number(item.speed,
        (v) => set(path + 'speed', v), { min: 60, max: 3000 })));
    } else if (item.type === 'SOLENOID') {
      fields.push(UI.field('GPIO (MOSFET gate)', UI.number(item.gpio,
        (v) => set(path + 'gpio', v), { min: -1, max: 48 })));
      fields.push(UI.field('Pull-in level (%)', UI.number(item.pullInPwm,
        (v) => set(path + 'pullInPwm', v), { min: 10, max: 100 })));
      fields.push(UI.field('Pull-in time (ms)', UI.number(item.pullInMs,
        (v) => set(path + 'pullInMs', v), { min: 5, max: 300 })));
      fields.push(UI.field('Hold level (%)', UI.number(item.holdPwm,
        (v) => set(path + 'holdPwm', v), { min: 0, max: 100 })));
      fields.push(UI.field('Maximum ON (ms)', UI.number(item.maxOnMs,
        (v) => set(path + 'maxOnMs', v), { min: 200, max: 20000, step: 100 }),
        'Mandatory. Past it the coil is released and a fault is raised, even if the note never ends.'));
      fields.push(UI.field('Cooldown (ms)', UI.number(item.cooldownMs,
        (v) => set(path + 'cooldownMs', v), { min: 100, max: 20000, step: 100 })));
    }

    if (cfg().valves.mode === 'MIDI_CC') {
      fields.push(UI.field('Controller number', UI.number(item.cc,
        (v) => set(path + 'cc', v), { min: 0, max: 127 })));
    }

    return UI.el('div', { style: 'margin-bottom:16px' }, [
      UI.el('div', { class: 'section-title', text: 'Valve ' + (index + 1) }),
      UI.el('div', { class: 'form-grid' }, fields),
      UI.el('div', { class: 'btn-row', style: 'margin-top:10px' }, [
        UI.el('button', { class: 'btn small', text: 'Test pulse',
          onclick: () => API.valveTest(index, 350).catch((e) => UI.toast(e.message, 'bad')) }),
        item.type === 'SERVO' ? UI.el('button', { class: 'btn small', text: 'Go released',
          onclick: () => API.valveCalibrate(index, item.releasedAngle) }) : null,
        item.type === 'SERVO' ? UI.el('button', { class: 'btn small', text: 'Go pressed',
          onclick: () => API.valveCalibrate(index, item.pressedAngle) }) : null
      ])
    ]);
  }

  // =========================================================================
  // Diagnostics
  // =========================================================================
  function diagnosticsTab(body) {
    const host = UI.el('div');
    body.appendChild(host);
    body.appendChild(UI.el('div', { class: 'section-title', text: 'MIDI monitor' }));
    // Pause / clear / filter are handled by the firmware ring buffer itself
    // (MidiMonitor), so the browser never has to hold the backlog.
    const controls = UI.el('div', { class: 'btn-row', style: 'margin-bottom:10px' });
    const monitor = UI.el('div', { class: 'mon' });
    const monitorMeta = UI.el('div', { class: 'help' });
    body.appendChild(controls);
    body.appendChild(monitorMeta);
    body.appendChild(monitor);

    let filter = { sources: ['USB', 'BLE', 'RTP', 'DIN', 'WEB'],
                   notes: true, controllers: true, other: true };
    let paused = false;

    async function push(patch) {
      try {
        await API.midiMonitorSet(patch);
        await refresh();
      } catch (err) { UI.toast(err.message, 'bad'); }
    }

    function drawControls() {
      UI.clear(controls);
      controls.appendChild(UI.el('button', {
        class: 'btn small' + (paused ? ' danger' : ''),
        text: paused ? 'Resume' : 'Pause',
        onclick: () => push({ paused: !paused })
      }));
      controls.appendChild(UI.el('button', {
        class: 'btn small', text: 'Clear',
        onclick: async () => {
          try { await API.midiMonitorClear(); await refresh(); }
          catch (err) { UI.toast(err.message, 'bad'); }
        }
      }));
      controls.appendChild(UI.el('button', { class: 'btn small', text: 'Refresh',
                                             onclick: refresh }));
      for (const src of ['USB', 'BLE', 'RTP', 'DIN', 'WEB']) {
        const on = filter.sources.indexOf(src) >= 0;
        controls.appendChild(UI.el('button', {
          class: 'btn small chip' + (on ? ' on' : ''), text: src,
          onclick: () => push({ sources: on ? filter.sources.filter((s) => s !== src)
                                            : filter.sources.concat([src]) })
        }));
      }
      for (const kind of [['notes', 'Notes'], ['controllers', 'CC / bend'], ['other', 'Other']]) {
        const on = !!filter[kind[0]];
        const patch = {};
        patch[kind[0]] = !on;
        controls.appendChild(UI.el('button', {
          class: 'btn small chip' + (on ? ' on' : ''), text: kind[1],
          onclick: () => push(patch)
        }));
      }
    }

    async function refresh() {
      try {
        const d = await API.diagnostics();
        UI.clear(host);
        host.appendChild(UI.el('div', { class: 'grid' }, [
          UI.stat('Firmware', d.firmware),
          UI.stat('Board', d.board),
          UI.stat('Uptime', UI.uptime(d.uptime)),
          UI.stat('Audio CPU', (d.cpuLoad || 0).toFixed(1), '%'),
          UI.stat('Underruns', String(d.audioUnderruns)),
          UI.stat('Free heap', (d.freeHeap / 1024).toFixed(0), 'KB')
        ]));
        host.appendChild(UI.el('div', { class: 'section-title', text: 'Detail' }));
        host.appendChild(UI.kv([
          ['Audio backend', d.audioBackend + ' (' + d.audioMaturity.toLowerCase() + ')'],
          ['Sample rate', d.audioSampleRate + ' Hz'],
          ['Blocks rendered', d.audioBlocks],
          ['PSRAM free', d.freePsram ? (d.freePsram / 1048576).toFixed(1) + ' MB' : '—'],
          ['MIDI received', d.midiRx + '  (' + d.midiRxPerSecond + '/s)'],
          ['MIDI sent', d.midiTx + '  (' + d.midiTxPerSecond + '/s)'],
          ['Dropped by filters', d.router.droppedByFilter],
          ['MIDI loops suppressed', d.router.loopsSuppressed],
          ['RTP datagrams refused', d.router.rtpRejectedSources || 0],
          ['Last attack delay', (d.attackDelayMs || 0) + ' ms  (waiting for the pistons)'],
          ['OTA', d.otaAvailable ? 'available' : 'not available on this partition table']
        ]));
        host.appendChild(UI.el('div', { class: 'section-title', text: 'Valves' }));
        host.appendChild(UI.table(['#', 'Type', 'State', 'Angle', 'Duty', 'Fault'],
          d.valves.map((v, i) => [i + 1, v.type, v.pressed ? 'pressed' : 'released',
                                  v.type === 'SERVO' ? Math.round(v.angle) + '°' : '—',
                                  v.type === 'SOLENOID' ? v.duty + ' %' : '—',
                                  v.fault ? (v.faultText || 'fault') : '—'])));
      } catch (err) {
        UI.clear(host);
        host.appendChild(UI.el('div', { class: 'help', text: err.message }));
      }

      try {
        const m = await API.midiMonitor();
        paused = !!m.paused;
        if (m.filter) {
          filter = { sources: (m.filter.sources || []).slice ? m.filter.sources.slice()
                                                             : filter.sources,
                     notes: !!m.filter.notes, controllers: !!m.filter.controllers,
                     other: !!m.filter.other };
        }
        drawControls();
        monitorMeta.textContent =
          m.items.length + ' message(s) kept · ' + (m.perSecond || 0) + '/s'
          + (m.overflow ? ' · ' + m.overflow + ' older message(s) dropped' : '')
          + (paused ? ' · capture paused' : '');
        UI.clear(monitor);
        monitor.appendChild(UI.table(['Time', 'Source', 'Type', 'Ch', 'D1', 'D2'],
          m.items.map((r) => [(r.ts / 1000).toFixed(2), r.src, r.type, r.ch, r.d1, r.d2])));
      } catch (_) { /* the panel above already reports the failure */ }
    }
    drawControls();
    refresh();
  }

  // =========================================================================
  // Firmware
  // =========================================================================
  function firmwareTab(body) {
    const status = App.status() || {};
    body.appendChild(UI.kv([['Installed version', status.firmware || '—'],
                            ['Board', (hw() && hw().board.chip) || '—']]));

    if (status.otaAvailable === false) {
      body.appendChild(UI.el('div', { class: 'issues' }, [
        UI.el('div', { class: 'issue', 'data-sev': 'WARNING' }, [
          UI.el('span', { class: 'tag', text: 'WARNING' }),
          UI.el('div', { text: 'This build has a single application partition, so an '
            + 'over-the-air update is not possible. On a 4 MB ESP32-WROOM the firmware does not '
            + 'leave room for two OTA slots: flash over USB, or use an 8 MB module.' })
        ])
      ]));
    } else {
      const progress = UI.el('div', { class: 'meter' }, [UI.el('i')]);
      const file = UI.el('input', { type: 'file', accept: '.bin' });
      body.appendChild(UI.field('Firmware image (.bin)', file));
      body.appendChild(UI.el('p', { class: 'help',
        text: 'Before the first byte is written the instrument mutes the audio, releases every '
            + 'valve and de-energises the solenoids.' }));
      body.appendChild(progress);
      body.appendChild(UI.el('div', { class: 'btn-row', style: 'margin-top:12px' }, [
        UI.el('button', { class: 'btn primary', text: 'Upload and install', onclick: async () => {
          if (!file.files[0] && !API.isMock()) {
            UI.toast('Pick a .bin file first', 'warn');
            return;
          }
          try {
            await API.uploadFirmware(file.files[0] || new Blob(),
              (f) => { progress.querySelector('i').style.width = (f * 100).toFixed(0) + '%'; });
            UI.toast('Update installed, rebooting…', 'ok');
          } catch (err) { UI.toast(err.message, 'bad'); }
        } })
      ]));
    }

    body.appendChild(UI.el('div', { class: 'section-title', text: 'Configuration file' }));
    body.appendChild(UI.el('p', { class: 'help',
      text: 'An exported file carries no Wi-Fi password, so it can be shared or committed to a '
          + 'repository safely.' }));
    body.appendChild(UI.el('div', { class: 'btn-row' }, [
      UI.el('button', { class: 'btn', text: 'Export JSON', onclick: () => App.exportConfig() }),
      UI.el('label', { class: 'btn' }, [
        document.createTextNode('Import JSON'),
        (() => {
          const input = UI.el('input', { type: 'file', accept: '.json', style: 'display:none' });
          input.addEventListener('change', (e) => App.importConfig(e.target.files[0]));
          return input;
        })()
      ]),
      UI.el('button', { class: 'btn', text: 'Reboot',
        onclick: () => API.reboot().then(() => UI.toast('Rebooting…', 'warn')) }),
      UI.el('button', { class: 'btn danger', text: 'Factory reset', onclick: () => {
        if (!window.confirm('Erase the configuration and reboot with the defaults?')) return;
        API.factoryReset().then(() => UI.toast('Reset, rebooting…', 'warn'));
      } })
    ]));
  }

  const BUILDERS = {
    Device: deviceTab, MIDI: midiTab, Audio: audioTab,
    Pistons: pistonsTab, Diagnostics: diagnosticsTab, Firmware: firmwareTab
  };

  return { open, close, isOpen: () => !!root };
})();
