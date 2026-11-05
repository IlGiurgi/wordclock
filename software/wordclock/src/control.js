// Control panel of the configuration page: live state and the settings that
// are not part of the configuration form (effects, animations, night mode,
// LED limit, WLED, diagnostics, event log). Talks to /state and /control,
// which use the same login as the page.
(function () {
  'use strict';

  const DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  const REFRESH_MS = 5000;
  let state = null;
  let nightDirty = false;

  // Tiny element builder: h('div', {class: 'x', onclick: f}, child, ...).
  function h(tag, attrs, ...children) {
    const el = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs || {})) {
      if (key.startsWith('on')) {
        el.addEventListener(key.substring(2), value);
      } else if (value === true) {
        el.setAttribute(key, '');
      } else if (value !== false && value != null) {
        el.setAttribute(key, value);
      }
    }
    for (const child of children.flat()) {
      if (child != null) {
        el.append(child.nodeType ? child : document.createTextNode(child));
      }
    }
    return el;
  }

  const $ = (id) => document.getElementById(id);

  function hexToRgb(hex) {
    const n = parseInt(hex.substring(1), 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255].join(',');
  }

  function duration(seconds) {
    const d = Math.floor(seconds / 86400);
    const hh = Math.floor((seconds % 86400) / 3600);
    const mm = Math.floor((seconds % 3600) / 60);
    return (d ? d + 'd ' : '') + (d || hh ? hh + 'h ' : '') + mm + 'm';
  }

  function toast(text, isError) {
    const el = $('ctlToast');
    el.textContent = text;
    el.className = 'toast show' + (isError ? ' error' : '');
    clearTimeout(toast.timer);
    toast.timer = setTimeout(() => (el.className = 'toast'), 2000);
  }

  async function send(cmd, value, quiet) {
    try {
      const response = await fetch('control', {
        method: 'POST',
        body: new URLSearchParams({ cmd: cmd, value: value }),
      });
      if (!response.ok) {
        throw new Error(response.status);
      }
      if (!quiet) {
        toast('Done');
      }
      setTimeout(refresh, 400);
    } catch (e) {
      toast('Failed (' + e.message + ')', true);
    }
  }

  // Sends at most one command per interval while a slider or picker moves.
  function throttled(fn, interval) {
    let last = 0;
    let pending = null;
    return function (value) {
      clearTimeout(pending);
      const wait = last + interval - Date.now();
      if (wait <= 0) {
        last = Date.now();
        fn(value);
      } else {
        pending = setTimeout(() => {
          last = Date.now();
          fn(value);
        }, wait);
      }
    };
  }

  function toggle(id, label, onchange) {
    return h('div', { class: 'row' },
      h('span', {}, label),
      h('label', { class: 'switch' },
        h('input', { type: 'checkbox', id: id, onchange: (e) => onchange(e.target.checked) }),
        h('span', {})));
  }

  function badge(id) {
    return h('span', { class: 'badge', id: id });
  }

  function setBadge(id, on, onText, offText) {
    const el = $(id);
    el.textContent = on ? onText : offText;
    el.classList.toggle('on', !!on);
  }

  function section(id, title, ...children) {
    return h('fieldset', { id: id, class: 'ctl' }, h('legend', {}, title), children);
  }

  function buildPanel() {
    const sendColor = throttled((hex) => send('light/color/set', hexToRgb(hex), true), 150);

    const control = section('ctlLight', 'Light',
      h('div', { class: 'status' },
        h('span', { id: 'ctlTime', class: 'time' }),
        badge('ctlMode')),
      toggle('ctlOn', 'Display on', (on) => send('light/switch/set', on ? 'ON' : 'OFF')),
      h('div', { class: 'row' },
        h('span', {}, 'Current color'),
        h('input', { type: 'color', id: 'ctlColor', oninput: (e) => sendColor(e.target.value) })),
      h('p', { class: 'hint' }, 'Kept until the form below is saved again.'),
      h('div', { class: 'row' },
        h('span', {}, 'Effect'),
        h('select', { id: 'ctlEffect', onchange: (e) => send('light/effect/set', e.target.value) })),
      h('label', {}, 'Animations'),
      h('div', { class: 'chips', id: 'ctlAnimations' }),
      h('div', { class: 'inline' },
        h('input', { type: 'color', id: 'ctlFlashColor', value: '#ff0000', title: 'Flash color' }),
        h('button', {
          type: 'button', class: 'small',
          onclick: () => send('light/notify/set', JSON.stringify({ color: hexToRgb($('ctlFlashColor').value), count: 3 })),
        }, 'Flash'),
        h('button', { type: 'button', class: 'small ghost', onclick: () => send('light/animation/set', 'stop') }, 'Stop')),
      h('label', { for: 'ctlText' }, 'Scroll a text'),
      h('div', { class: 'inline' },
        h('input', { type: 'text', id: 'ctlText', maxlength: 199, placeholder: 'Hello!' }),
        h('button', {
          type: 'button', class: 'small',
          onclick: () => {
            const text = $('ctlText').value.trim();
            if (text) {
              send('light/text/set', JSON.stringify({ text: text, delay: 150 }));
            }
          },
        }, 'Send')));

    const dayBoxes = DAYS.map((day, i) =>
      h('label', { class: 'day' },
        h('input', { type: 'checkbox', value: i + 1, class: 'ctlDay', onchange: () => (nightDirty = true) }),
        h('span', {}, day)));
    const night = section('ctlNight', 'Night mode',
      h('div', { class: 'status' }, h('span', {}, 'Dims or switches off the display at night.'), badge('ctlNightActive')),
      toggle('ctlNightEnabled', 'Enabled', () => (nightDirty = true)),
      h('div', { class: 'row' },
        h('span', {}, 'From'),
        h('input', { type: 'time', id: 'ctlNightStart', oninput: () => (nightDirty = true) }),
        h('span', {}, 'to'),
        h('input', { type: 'time', id: 'ctlNightEnd', oninput: () => (nightDirty = true) })),
      h('div', { class: 'days' }, dayBoxes),
      h('label', { for: 'ctlNightBrightness' }, 'Brightness'),
      h('div', { class: 'inline' },
        h('input', {
          type: 'range', id: 'ctlNightBrightness', min: 0, max: 100, step: 5,
          oninput: (e) => {
            nightDirty = true;
            $('ctlNightValue').textContent = e.target.value == 0 ? 'Off' : e.target.value + '%';
          },
        }),
        h('span', { id: 'ctlNightValue', class: 'value' })),
      h('button', { type: 'button', onclick: saveNight }, 'Save night mode'));

    const leds = section('ctlLeds', 'LEDs & WLED',
      h('label', { for: 'ctlLimit' }, 'LED current limit (mA)'),
      h('div', { class: 'inline' },
        h('input', { type: 'number', id: 'ctlLimit', step: 50 }),
        h('button', {
          type: 'button', class: 'small',
          onclick: () => send('light/limit/set', $('ctlLimit').value),
        }, 'Apply')),
      h('p', { class: 'hint' }, 'Dims the whole display to stay within the power supply budget.'),
      h('div', { class: 'status' }, h('span', {}, 'WLED / DDP stream'), badge('ctlLive')),
      toggle('ctlWledRealtime', 'Accept streams (DDP, UDP)', (on) => send('wled/realtime/set', on ? 'ON' : 'OFF')),
      toggle('ctlWledOverlay', 'Color only the time', (on) => send('wled/overlay/set', on ? 'ON' : 'OFF')),
      toggle('ctlWledSync', 'Follow WLED sync (on/off, color)', (on) => send('wled/sync/set', on ? 'ON' : 'OFF')),
      h('p', { class: 'hint' }, 'Senders must use 114 LEDs: 110 letters, then the 4 corner dots.'),
      h('div', { id: 'ctlDiscoveryRow' },
        toggle('ctlDiscovery', 'Home Assistant discovery', (on) => send('discovery/set', on ? 'ON' : 'OFF'))));

    const diag = section('ctlDiag', 'Diagnostics', h('dl', { id: 'ctlDiagList' }));

    const log = section('ctlLog', 'Event log',
      h('pre', { id: 'ctlLogText' }, '…'),
      h('button', { type: 'button', class: 'small ghost', onclick: loadLog }, 'Reload'));

    return h('section', { id: 'ctl' },
      h('div', { class: 'ctlHead' }, h('h1', { id: 'ctlName' }, 'WordClock')),
      control, night, leds, diag, log,
      h('h2', { class: 'formTitle' }, 'Settings'),
      h('div', { id: 'ctlToast', class: 'toast' }));
  }

  function saveNight() {
    const days = Array.from(document.querySelectorAll('.ctlDay'))
      .filter((box) => box.checked)
      .map((box) => box.value)
      .join('');
    send('night/set', JSON.stringify({
      enabled: $('ctlNightEnabled').checked,
      start: $('ctlNightStart').value,
      end: $('ctlNightEnd').value,
      days: days,
      brightness: parseInt($('ctlNightBrightness').value, 10),
    }));
    nightDirty = false;
  }

  async function loadLog() {
    try {
      const response = await fetch('state?log=1', { cache: 'no-store' });
      const data = await response.json();
      const lines = (data.log || '').trim().split('\n').filter((l) => l);
      $('ctlLogText').textContent = lines.length ? lines.reverse().join('\n') : 'No events.';
    } catch (e) {
      $('ctlLogText').textContent = 'Could not load the log.';
    }
  }

  // Sets a value unless the person is editing that field.
  function setValue(id, value) {
    const el = $(id);
    if (document.activeElement !== el && el.value != value) {
      el.value = value;
    }
  }

  function render(s) {
    const first = state === null;
    state = s;

    $('ctlName').textContent = s.name;
    $('ctlTime').textContent = s.time.substring(11, 16);
    $('ctlMode').textContent = s.on ? s.mode : 'off';
    $('ctlMode').classList.toggle('on', s.on);
    $('ctlOn').checked = s.on;
    setValue('ctlColor', s.color.toLowerCase());
    document.querySelector('.logoContainer').style.backgroundColor = s.on ? s.color : '#333';

    if (first) {
      $('ctlEffect').replaceChildren(...s.effects.map((name) => h('option', { value: name }, name)));
      $('ctlAnimations').replaceChildren(...s.animations.map((name) =>
        h('button', { type: 'button', class: 'chip', onclick: () => send('light/animation/set', name) }, name)));
      $('ctlLimit').min = s.limit_min;
      $('ctlLimit').max = s.limit_max;
    }
    setValue('ctlEffect', s.effect);
    setValue('ctlLimit', s.limit);

    if (first || !nightDirty) {
      $('ctlNightEnabled').checked = s.night.enabled;
      $('ctlNightStart').value = s.night.start;
      $('ctlNightEnd').value = s.night.end;
      document.querySelectorAll('.ctlDay').forEach((box) => (box.checked = s.night.days.includes(box.value)));
      $('ctlNightBrightness').value = s.night.brightness;
      $('ctlNightValue').textContent = s.night.brightness == 0 ? 'Off' : s.night.brightness + '%';
    }
    setBadge('ctlNightActive', s.night.active, 'active now', 'inactive');

    $('ctlWledRealtime').checked = s.wled.realtime;
    $('ctlWledOverlay').checked = s.wled.overlay;
    $('ctlWledSync').checked = s.wled.sync;
    setBadge('ctlLive', s.wled.live, 'streaming', 'idle');
    $('ctlDiscoveryRow').style.display = s.mqtt ? '' : 'none';
    $('ctlDiscovery').checked = s.discovery;

    const rssi = s.rssi;
    const signal = rssi >= -60 ? 'good' : rssi >= -72 ? 'fair' : 'weak';
    const items = [
      ['Firmware', s.version],
      ['Clock time', s.time + (s.ntp ? (s.ntp_synced ? ' (NTP)' : ' (waiting for NTP)') : ' (manual)')],
      ['Uptime', duration(s.uptime)],
      ['Last reset', s.reset],
      ['WiFi', s.ssid + ', ' + rssi + ' dBm (' + signal + ')'],
      ['IP / MAC', s.ip + ' / ' + s.mac],
      ['MQTT', s.mqtt ? (s.mqtt_connected ? 'connected' : 'disconnected') : 'disabled'],
      ['Free memory', Math.round(s.heap / 1024) + ' KB (lowest ' + Math.round(s.heap_min / 1024) + ' KB)'],
      ['Temperature', s.temperature != null ? s.temperature.toFixed(1) + ' °C (RTC chip)' : 'n/a'],
      ['Light sensor', s.ldr],
      ['LED limit', s.limit + ' mA'],
    ];
    $('ctlDiagList').replaceChildren(...items.flatMap(([k, v]) => [h('dt', {}, k), h('dd', {}, String(v))]));
  }

  async function refresh() {
    if (document.hidden) {
      return;
    }
    try {
      const response = await fetch('state', { cache: 'no-store' });
      if (response.ok) {
        render(await response.json());
        document.body.classList.remove('offline');
      }
    } catch (e) {
      document.body.classList.add('offline');
    }
  }

  // Collapsible sections, remembered in this browser.
  function makeCollapsible() {
    let collapsed = [];
    try {
      collapsed = JSON.parse(localStorage.getItem('wcCollapsed') || '[]');
    } catch (e) { }
    document.querySelectorAll('fieldset[id]').forEach((fs) => {
      const legend = fs.querySelector(':scope > legend');
      if (!legend) {
        return;
      }
      fs.classList.add('fold');
      fs.classList.toggle('collapsed', collapsed.includes(fs.id));
      legend.addEventListener('click', () => {
        fs.classList.toggle('collapsed');
        const ids = Array.from(document.querySelectorAll('fieldset.collapsed')).map((f) => f.id);
        try {
          localStorage.setItem('wcCollapsed', JSON.stringify(ids));
        } catch (e) { }
      });
    });
  }

  document.addEventListener('DOMContentLoaded', () => {
    // Only on the configuration page itself.
    const form = document.querySelector('form');
    if (!form || !document.getElementById('iwcThingName')) {
      return;
    }
    form.parentElement.insertBefore(buildPanel(), form);
    // The network fieldset has no legend: give it one so it can fold too.
    const sys = document.getElementById('iwcSys');
    if (sys && !sys.querySelector(':scope > legend')) {
      sys.prepend(h('legend', {}, 'Network'));
    }
    makeCollapsible();
    refresh().then(loadLog);
    setInterval(refresh, REFRESH_MS);
    document.addEventListener('visibilitychange', refresh);
  });
})();
