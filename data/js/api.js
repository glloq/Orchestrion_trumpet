/* ===========================================================================
   api.js - thin wrapper around the REST API.
   Everything that is not live goes through here; live data uses the WebSocket.
   =========================================================================== */
'use strict';

const API = (() => {
  const base = '';

  async function request(method, path, body) {
    const init = { method, headers: {} };
    if (body !== undefined) {
      init.headers['Content-Type'] = 'application/json';
      init.body = typeof body === 'string' ? body : JSON.stringify(body);
    }
    let response;
    try {
      response = await fetch(base + path, init);
    } catch (err) {
      throw new Error('The trumpet is not reachable (' + err.message + ')');
    }
    const text = await response.text();
    let data = null;
    if (text) {
      try { data = JSON.parse(text); } catch (_) { data = { raw: text }; }
    }
    if (!response.ok) {
      const message = (data && data.error) || ('HTTP ' + response.status);
      const error = new Error(message);
      error.status = response.status;
      error.payload = data;
      throw error;
    }
    return data;
  }

  return {
    status:        () => request('GET', '/api/status'),
    diagnostics:   () => request('GET', '/api/diagnostics'),
    hardware:      () => request('GET', '/api/hardware'),
    getConfig:     () => request('GET', '/api/config'),
    putConfig:     (cfg) => request('PUT', '/api/config', cfg),
    validate:      (cfg) => request('POST', '/api/config/validate', cfg),
    exportConfig:  () => request('GET', '/api/config/export'),
    importConfig:  (json) => request('POST', '/api/config/import', json),
    applyPreset:   (key) => request('POST', '/api/config/preset', { preset: key }),
    panic:         () => request('POST', '/api/panic', {}),
    releasePanic:  () => request('POST', '/api/panic/release', {}),
    audioTest:     (opts) => request('POST', '/api/audio/test', opts),
    audioMute:     (muted) => request('POST', '/api/audio/mute', { muted }),
    audioVolume:   (volume) => request('POST', '/api/audio/volume', { volume }),
    valveTest:     (valve, durationMs) => request('POST', '/api/valve/test', { valve, durationMs }),
    valveMode:     (mode) => request('POST', '/api/valve/mode', { mode }),
    valveManual:   (valve, pressed) => request('POST', '/api/valve/manual', { valve, pressed }),
    valveCalibrate:(valve, angle) => request('POST', '/api/valve/calibrate', { valve, angle }),
    midiStatus:    () => request('GET', '/api/midi/status'),
    midiMonitor:   () => request('GET', '/api/midi/monitor'),
    fingering:     () => request('GET', '/api/fingering'),
    putFingering:  (notes) => request('PUT', '/api/fingering', { notes }),
    resetFingering:() => request('POST', '/api/fingering/reset', {}),
    reboot:        () => request('POST', '/api/system/reboot', {}),
    factoryReset:  () => request('POST', '/api/system/factory-reset', {}),

    // OTA needs a multipart upload with progress, so it does not use request().
    uploadFirmware(file, onProgress) {
      return new Promise((resolve, reject) => {
        const form = new FormData();
        form.append('firmware', file, file.name);
        const xhr = new XMLHttpRequest();
        xhr.open('POST', base + '/api/ota');
        xhr.upload.onprogress = (e) => {
          if (e.lengthComputable && onProgress) onProgress(e.loaded / e.total);
        };
        xhr.onload = () => {
          let payload = null;
          try { payload = JSON.parse(xhr.responseText); } catch (_) { /* ignore */ }
          if (xhr.status >= 200 && xhr.status < 300) resolve(payload);
          else reject(new Error((payload && payload.error) || ('HTTP ' + xhr.status)));
        };
        xhr.onerror = () => reject(new Error('the upload was interrupted'));
        xhr.send(form);
      });
    }
  };
})();
