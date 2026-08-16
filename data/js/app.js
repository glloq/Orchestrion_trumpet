/* ===========================================================================
   app.js — bootstrap, view switching and the shared save bar.
   =========================================================================== */
'use strict';

const App = (() => {
  const VIEWS = { play: Pages.play, configure: Pages.configure,
                  soundlab: SoundLab.render, wiring: Pages.wiring };
  const TITLES = { play: 'Play', configure: 'Configure',
                   soundlab: 'Sound Lab', wiring: 'Wiring' };

  const state = { config: null, saved: null, hardware: null, status: null,
                  telemetry: null, view: 'play', dirty: false };

  const config = () => state.config;
  const hardware = () => state.hardware;
  const status = () => state.status;

  function get(path) {
    return path.split('.').reduce((node, key) => (node ? node[key] : undefined), state.config);
  }
  function set(path, value) {
    const parts = path.split('.');
    let node = state.config;
    for (let i = 0; i < parts.length - 1; i++) node = node[parts[i]];
    node[parts[parts.length - 1]] = value;
    markDirty();
  }

  function markDirty() {
    state.dirty = true;
    document.getElementById('savebar').hidden = false;
  }
  function clearDirty() {
    state.dirty = false;
    document.getElementById('savebar').hidden = true;
  }

  function show(name) {
    if (!VIEWS[name]) name = 'play';
    const host = document.getElementById('view');
    if (host.onLeave) { host.onLeave(); host.onLeave = null; }

    state.view = name;
    for (const item of document.querySelectorAll('.nav-item')) {
      item.classList.toggle('active', item.dataset.view === name);
    }
    document.getElementById('page-title').textContent = TITLES[name];
    document.getElementById('side').classList.remove('open');
    render();
    location.hash = '#' + name;
  }

  function render() {
    const host = document.getElementById('view');
    if (host.onLeave) { host.onLeave(); host.onLeave = null; }
    UI.clear(host);
    host.updateLive = null;
    if (!state.config) return;
    // A view may be async (Sound Lab fetches the voicing library first); the
    // live telemetry is applied once it has finished building.
    const built = VIEWS[state.view](host);
    const finish = () => { if (state.telemetry && host.updateLive) host.updateLive(state.telemetry); };
    if (built && typeof built.then === 'function') built.then(finish); else finish();
  }

  async function reload() {
    state.config = await API.getConfig();
    state.saved = JSON.parse(JSON.stringify(state.config));
    clearDirty();
  }

  async function refreshStatus() {
    try {
      state.status = await API.status();
      paintHeader();
    } catch (_) { /* the link badge already says so */ }
  }

  async function save() {
    try {
      const result = await API.putConfig(state.config);
      if (!result.ok) {
        UI.toast('The configuration was refused', 'bad');
        const host = document.getElementById('view');
        const node = UI.issues(result.issues);
        if (node) host.insertBefore(node, host.firstChild);
        return false;
      }
      const warnings = (result.issues || []).filter((i) => i.severity !== 'ERROR');
      UI.toast('Saved. Reboot to apply the hardware changes.', 'ok');
      if (warnings.length) {
        const host = document.getElementById('view');
        const node = UI.issues(warnings);
        if (node) host.insertBefore(node, host.firstChild);
      }
      state.saved = JSON.parse(JSON.stringify(state.config));
      clearDirty();
      return true;
    } catch (err) {
      UI.toast(err.message, 'bad');
      return false;
    }
  }

  function discard() {
    state.config = JSON.parse(JSON.stringify(state.saved));
    clearDirty();
    render();
  }

  async function panic() {
    // Both paths: the WebSocket for latency, HTTP as the guarantee that it
    // lands even if the socket just dropped.
    WS.panic();
    try { await API.panic(); } catch (_) { /* the socket may have carried it */ }
    Keyboard.releaseAll();
    UI.toast('STOP: audio muted, valves released, solenoids disabled.', 'warn');
    refreshStatus();
  }

  function exportConfig() {
    const blob = new Blob([JSON.stringify(state.config, null, 2)], { type: 'application/json' });
    const link = UI.el('a', { href: URL.createObjectURL(blob), download: 'trumpet-config.json' });
    link.click();
    URL.revokeObjectURL(link.href);
  }

  function importConfig(file) {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async () => {
      try {
        const result = await API.importConfig(reader.result);
        if (!result.ok) { UI.toast('The file was refused', 'bad'); return; }
        UI.toast('Configuration imported — reboot to apply it', 'ok');
        await reload();
        render();
      } catch (err) { UI.toast(err.message, 'bad'); }
    };
    reader.readAsText(file);
  }

  function paintHeader() {
    const s = state.status;
    if (s) {
      document.getElementById('meta-fw').textContent = 'firmware ' + s.firmware;
      document.getElementById('meta-net').textContent =
        (s.network.ip || '') + (s.network.hostname ? ' · ' + s.network.hostname + '.local' : '');
    }
    const source = document.getElementById('badge-source');
    source.textContent = API.isMock() ? 'DEMO / MOCK DATA' : 'DEVICE';
    source.className = 'badge ' + (API.isMock() ? 'warn' : 'ok');
  }

  function applyTelemetry(message) {
    state.telemetry = message;
    const host = document.getElementById('view');
    if (host && host.updateLive) host.updateLive(message);
    const mode = document.getElementById('badge-mode');
    mode.textContent = message.mode;
    mode.className = 'badge ' + (message.mode === 'RUNNING' ? 'ok'
      : message.mode === 'SAFE_MODE' ? 'warn' : message.mode === 'PANIC' ? 'bad' : '');
  }

  function setLink(connected) {
    const badge = document.getElementById('badge-link');
    badge.textContent = connected ? 'LIVE' : 'OFFLINE';
    badge.className = 'badge ' + (connected ? 'ok' : 'bad');
  }

  async function boot() {
    const sub = document.getElementById('boot-sub');
    try {
      sub.textContent = 'Reading the configuration…';
      const [st, hw, cfg] = await Promise.all([API.status(), API.hardware(), API.getConfig()]);
      state.status = st;
      state.hardware = hw;
      state.config = cfg;
      state.saved = JSON.parse(JSON.stringify(cfg));

      document.querySelector('.brand-name').textContent =
        st.deviceName.split(' ')[0] || 'Orchestrion';
      paintHeader();

      show((location.hash || '').replace('#', '') || 'play');

      if (!st.wizardCompleted) {
        UI.toast('First start: the wizard will take you through the setup.', 'info');
        Wizard.open();
      }
      if (st.mode === 'SAFE_MODE') {
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
      item.addEventListener('click', () => show(item.dataset.view));
    }
    document.getElementById('burger').addEventListener('click', () =>
      document.getElementById('side').classList.toggle('open'));
    document.getElementById('btn-settings').addEventListener('click', () => Settings.open());
    document.getElementById('btn-stop').addEventListener('click', panic);
    document.getElementById('savebar-save').addEventListener('click', save);
    document.getElementById('savebar-discard').addEventListener('click', discard);
    window.addEventListener('hashchange', () => {
      const name = location.hash.replace('#', '');
      if (name && name !== state.view) show(name);
    });
    window.addEventListener('beforeunload', (e) => {
      if (!state.dirty) return;
      e.preventDefault();
      e.returnValue = '';
    });
    API.onModeChange(paintHeader);
    boot();
  });

  return { show, render, reload, save, panic, refreshStatus, exportConfig, importConfig,
           config, hardware, status, get, set, markDirty, clearDirty,
           // Used by the screenshot tool.
           navigate: show, openSettings: (tab) => Settings.open(tab),
           openWizard: (step) => Wizard.open(step), gotoWizardStep: (s) => Wizard.gotoStep(s),
           closeModal: () => { Settings.close(); Wizard.close(); } };
})();
