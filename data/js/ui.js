/* ===========================================================================
   ui.js — DOM helpers and the component vocabulary every page is built from:
   cards, toggles, dropdowns, sliders, status badges and disclosures.
   =========================================================================== */
'use strict';

const UI = (() => {

  function el(tag, attrs, children) {
    const node = document.createElement(tag);
    if (attrs) {
      for (const [key, value] of Object.entries(attrs)) {
        if (value === null || value === undefined || value === false) continue;
        if (key === 'class') node.className = value;
        else if (key === 'text') node.textContent = value;
        else if (key === 'html') node.innerHTML = value;
        else if (key.startsWith('on') && typeof value === 'function') {
          node.addEventListener(key.slice(2).toLowerCase(), value);
        } else if (value === true) node.setAttribute(key, '');
        else node.setAttribute(key, value);
      }
    }
    for (const child of [].concat(children || [])) {
      if (child === null || child === undefined || child === false) continue;
      node.appendChild(typeof child === 'string' ? document.createTextNode(child) : child);
    }
    return node;
  }

  const svg = (tag, attrs, children) => {
    const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (const [key, value] of Object.entries(attrs || {})) {
      if (value === null || value === undefined || value === false) continue;
      if (key.startsWith('on') && typeof value === 'function') {
        node.addEventListener(key.slice(2).toLowerCase(), value);
      } else if (key === 'text') node.textContent = value;
      else node.setAttribute(key, value);
    }
    for (const child of [].concat(children || [])) {
      if (child) node.appendChild(child);
    }
    return node;
  };

  const clear = (node) => { while (node.firstChild) node.removeChild(node.firstChild); };

  function card(title, body, note) {
    const head = title
      ? el('div', { class: 'card-head' },
           [el('h2', { text: title }),
            note ? (typeof note === 'string' ? el('span', { class: 'card-note', text: note }) : note)
                 : null])
      : null;
    return el('div', { class: 'card' }, [head].concat(body || []));
  }

  const stat = (label, value, unit) => el('div', { class: 'stat' }, [
    el('div', { class: 'k', text: label }),
    el('div', { class: 'v', text: value }),
    unit ? el('div', { class: 'u', text: unit }) : null
  ]);

  const badge = (text, tone) => el('span', { class: 'badge ' + (tone || ''), text });

  const field = (label, control, help) => el('div', { class: 'field' }, [
    label ? el('label', { text: label }) : null,
    control,
    help ? el('div', { class: 'help', text: help }) : null
  ]);

  function select(options, value, onChange) {
    const node = el('select', { onchange: (e) => onChange(e.target.value) });
    for (const option of options) {
      const item = el('option', { value: option.value, text: option.label });
      if (option.disabled) item.disabled = true;
      if (String(option.value) === String(value)) item.selected = true;
      node.appendChild(item);
    }
    return node;
  }

  function number(value, onChange, opts) {
    const o = opts || {};
    const node = el('input', {
      type: 'number', value,
      min: o.min !== undefined ? o.min : null,
      max: o.max !== undefined ? o.max : null,
      step: o.step !== undefined ? o.step : null
    });
    node.addEventListener('change', () => {
      const parsed = parseFloat(node.value);
      onChange(Number.isFinite(parsed) ? parsed : 0);
    });
    return node;
  }

  function text(value, onChange, opts) {
    const o = opts || {};
    const node = el('input', {
      type: o.password ? 'password' : 'text',
      value: value || '',
      maxlength: o.maxlength || null,
      placeholder: o.placeholder || null,
      autocomplete: o.password ? 'new-password' : 'off'
    });
    node.addEventListener('change', () => onChange(node.value));
    if (o.oninput) node.addEventListener('input', () => o.oninput(node.value));
    return node;
  }

  function toggle(label, checked, onChange, disabled) {
    const input = el('input', { type: 'checkbox' });
    input.checked = !!checked;
    input.disabled = !!disabled;
    input.addEventListener('change', () => onChange(input.checked));
    return el('label', { class: 'toggle' }, [
      input, el('span', { class: 'track' }), el('span', { class: 'lbl', text: label })
    ]);
  }

  function slider(label, value, min, max, step, onInput, format) {
    const readout = el('span', { class: 'ctrl-val', text: (format || String)(value) });
    const input = el('input', { type: 'range', min, max, step, value });
    input.addEventListener('input', () => {
      const v = parseFloat(input.value);
      readout.textContent = (format || String)(v);
      onInput(v);
    });
    const wrap = el('div', { class: 'field' }, [
      el('label', {}, [document.createTextNode(label), readout]), input
    ]);
    wrap.setValue = (v) => { input.value = v; readout.textContent = (format || String)(v); };
    return wrap;
  }

  function table(headers, rows) {
    const thead = el('thead', {}, [
      el('tr', {}, headers.map((h) =>
        el('th', { class: h && h.num ? 'num' : null,
                   text: h && h.label !== undefined ? h.label : h })))
    ]);
    const tbody = el('tbody', {}, rows.map((cells) =>
      el('tr', {}, cells.map((c) =>
        c && c.nodeType ? el('td', {}, [c])
                        : el('td', { text: c === null || c === undefined ? '—' : String(c) })))));
    return el('div', { class: 'table-wrap' }, [el('table', {}, [thead, tbody])]);
  }

  function issues(list) {
    if (!list || !list.length) return null;
    return el('div', { class: 'issues' }, list.map((i) =>
      el('div', { class: 'issue', 'data-sev': i.severity }, [
        el('span', { class: 'tag', text: i.severity }),
        el('div', {}, [
          el('div', { text: i.message }),
          i.field ? el('div', { class: 'fieldname', text: i.field }) : null
        ])
      ])));
  }

  // Progressive disclosure: everything rare or dangerous folds away by default.
  function disclosure(label, buildBody, openByDefault) {
    const body = el('div', { class: 'disclosure-body' });
    let built = false;
    const button = el('button', {
      class: 'disclosure-toggle', type: 'button', 'aria-expanded': 'false', text: label
    });
    const setOpen = (open) => {
      button.setAttribute('aria-expanded', open ? 'true' : 'false');
      body.hidden = !open;
      if (open && !built) { built = true; body.appendChild(buildBody()); }
    };
    button.addEventListener('click', () =>
      setOpen(button.getAttribute('aria-expanded') !== 'true'));
    const wrap = el('div', { class: 'disclosure' }, [button, body]);
    setOpen(!!openByDefault);
    wrap.open = () => setOpen(true);
    return wrap;
  }

  const readout = (key, value, action) => el('div', { class: 'readout' }, [
    el('span', { class: 'k', text: key }),
    typeof value === 'string' ? el('span', { class: 'v', text: value }) : value,
    action || null
  ]);

  function toast(message, tone) {
    const host = document.getElementById('toasts');
    const node = el('div', { class: 'toast ' + (tone || 'info'), text: message });
    host.appendChild(node);
    window.setTimeout(() => {
      node.style.opacity = '0';
      window.setTimeout(() => node.remove(), 250);
    }, tone === 'bad' ? 7000 : 3500);
  }

  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const noteName = (n) => NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);

  const stars = (value) => {
    const full = Math.floor(value / 2);
    const half = value % 2 === 1;
    return '★'.repeat(full) + (half ? '½' : '') + '☆'.repeat(Math.max(0, 5 - full - (half ? 1 : 0)));
  };

  function uptime(seconds) {
    const s = seconds | 0;
    const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600);
    const m = Math.floor(s % 3600 / 60), sec = s % 60;
    if (d) return d + 'd ' + h + 'h';
    if (h) return h + 'h ' + m + 'm';
    if (m) return m + 'm ' + sec + 's';
    return sec + 's';
  }

  const kv = (rows) => el('dl', { class: 'kv' }, rows.flatMap(([k, v]) => [
    el('dt', { text: k }),
    v && v.nodeType ? el('dd', {}, [v]) : el('dd', { text: String(v) })
  ]));

  return { el, svg, clear, card, stat, badge, field, select, number, text, toggle, slider,
           table, issues, disclosure, readout, toast, noteName, stars, uptime, kv };
})();
