(() => {
  'use strict';
  const key = 'sapdm-broker-url';
  const fallback = window.DM_MQTT_PUBLIC_BROKER || `wss://${location.hostname}`;
  let saved = '';
  try { saved = localStorage.getItem(key) || ''; } catch (_) {}
  window.dmMqtt = {
    url: saved || fallback,
    options: { reconnectPeriod: 1000, connectTimeout: 10000, keepalive: 30, clean: true, queueQoSZero: false },
    save(url) {
      const parsed = new URL(url);
      if (!['ws:', 'wss:'].includes(parsed.protocol)) throw Error('Use a ws:// or wss:// broker address.');
      if (location.protocol === 'https:' && parsed.protocol !== 'wss:') throw Error('This secure page needs a wss:// broker address.');
      try { localStorage.setItem(key, url); } catch (_) {}
      this.url = url;
    }
  };
})();
