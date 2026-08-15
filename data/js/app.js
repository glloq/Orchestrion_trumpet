/* ===========================================================================
   app.js - bootstrap, routing and the shared save bar.
   =========================================================================== */
'use strict';

const App = (() => {
  const S = Pages.state;
  let current = 'dashboard';
  let dirty = false;

  const PAGES = {
    dashboard: Pages.dashboard,
    play: Pages.play,
    midi: Pages.midi,
    instrument: Pages.instrument,
    audio: Pages.audio,
    pistons: Pages.pistons,
    hardware: Pages.hardware,
    calibration: Pages.calibration,
    diagnostics: Pages.diagnostics,
    advanced: Pages.advanced,
    firmware: Pages.firmware,
    wizard: Wizard.page
  };

  const pageHost = (name) => document.getElementById('page-' + name);

  function show(name) {
    if (!PAGES[name]) name = 'dashboard';
    const previous = pageHost(current);
    if (previous && previous.onLeave) { previous.onLeave(); previous.onLeave = null; }

    current = name;
    for (const key of Object.keys(PAGES)) {
      pageHost(key).classList.toggle('active', key === name);
    }
    for (const item of document.querySelectorAll('.nav-item')) {
      item.classList.toggle('active', item.dataset.page === name);
    }
    document.getElementById('nav').classList.remove('open');

    const host = pageHost(name);
    host.updateLive = null;
    if (S.config || name === 'dashboard' || name === 'diagnostics' || name === 'firmware') {
      PAGES[name](host);
    }
    if (S.telemetry && host.updateLive) host.updateLive(S.telemetry);
    location.hash = '#' + name;
  }

  function rerender() { show(current); }

  function setDirty(value) {
    dirty = value;
    const bar = document.getElementById('savebar');
    bar.hidden = !value;
  }

  function showIssues(issues) {
    const host = pageHost(current);
    const node = UI.issues(issues);
    if (node) host.insertBefore(node, host.firstChild.nextSibling);
  }

  async function reload() {
    S.config = await API.getConfig();
    S.saved = JSON.parse(JSON.stringify(S.config));
    setDirty(false);
  }

  async function save() {
    try {
      const result = await API.putConfig(S.config);
      if (!result.ok) {
        UI.toast('The configuration was refused', 'bad');
        showIssues(result.issues);
        return;
      }
      const warnings = (result.issues || []).filter((i) => i.severity !== 'ERROR');
      UI.toast('Saved. Reboot to apply the hardware changes.', 'ok');
      if (warnings.length) showIssues(warnings);
      S.saved = JSON.parse(JSON.stringify(S.config));
      setDirty(false);
    } catch (err) {
      UI.toast(err.message, 'bad');
    }
  }

  function discard() {
    S.config = JSON.parse(JSON.stringify(S.saved));
    setDirty(false);
    rerender();
  }

  function applyTelemetry(message) {
    S.telemetry = message;
    const host = pageHost(current);
    if (host && host.updateLive) host.updateLive(message);

    const mode = document.getElementById('badge-mode');
    mode.textContent = message.mode;
    mode.dataset.tone = message.mode === 'RUNNING' ? 'ok'
      : message.mode === 'SAFE_MODE' ? 'warn'
      : message.mode === 'PANIC' ? 'bad' : 'neutral';
  }

  function setLink(connected) {
    const badge = document.getElementById('badge-link');
    badge.textContent = connected ? 'Live' : 'Offline';
    badge.dataset.tone = connected ? 'ok' : 'bad';
  }

  async function boot() {
    const sub = document.getElementById('boot-sub');
    try {
      sub.textContent = 'Reading the configuration…';
      const [status, hardware, config] = await Promise.all([
        API.status(), API.hardware(), API.getConfig()
      ]);
      S.hardware = hardware;
      S.config = config;
      S.saved = JSON.parse(JSON.stringify(config));

      document.getElementById('device-name').textContent = status.deviceName;
      document.getElementById('nav-fw').textContent = 'firmware ' + status.firmware;
      document.getElementById('nav-ip').textContent =
        (status.network.ip || '') + (status.network.hostname
          ? ' · ' + status.network.hostname + '.local' : '');

      const start = (location.hash || '').replace('#', '')
        || (status.wizardCompleted ? 'dashboard' : 'wizard');
      show(start);

      if (!status.wizardCompleted && start === 'wizard') {
        UI.toast('First start: the wizard will take you through the setup.', 'info');
      }
      if (status.mode === 'SAFE_MODE') {
        UI.toast('SAFE MODE: the stored configuration was refused, so no actuator and no audio '
               + 'backend were started. Fix the issues and reboot.', 'bad');
      }

      document.getElementById('boot').classList.add('hidden');
    } catch (err) {
      sub.textContent = err.message;
      window.setTimeout(boot, 2000);
      return;
    }

    WS.on('open', () => setLink(true));
    WS.on('close', () => setLink(false));
    WS.on('telemetry', applyTelemetry);
    WS.connect();
  }

  document.addEventListener('DOMContentLoaded', () => {
    for (const item of document.querySelectorAll('.nav-item')) {
      item.addEventListener('click', () => show(item.dataset.page));
    }
    document.getElementById('nav-toggle').addEventListener('click', () => {
      document.getElementById('nav').classList.toggle('open');
    });
    document.getElementById('savebar-save').addEventListener('click', save);
    document.getElementById('savebar-discard').addEventListener('click', discard);
    document.getElementById('btn-panic').addEventListener('click', async () => {
      // Panic goes over both paths: the WebSocket for latency, HTTP as the
      // guarantee that it lands even if the socket just dropped.
      WS.panic();
      try { await API.panic(); } catch (_) { /* the WebSocket may have carried it */ }
      Keyboard.releaseAll();
      UI.toast('PANIC: audio muted, valves released, solenoids disabled.', 'warn');
    });
    window.addEventListener('hashchange', () => {
      const name = location.hash.replace('#', '');
      if (name && name !== current) show(name);
    });
    window.addEventListener('beforeunload', (e) => {
      if (!dirty) return;
      e.preventDefault();
      e.returnValue = '';
    });
    boot();
  });

  return { show, rerender, setDirty, showIssues, reload, save };
})();
