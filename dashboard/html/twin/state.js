/* Telemetry model shared by the browser and Node tests. No network or DOM. */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.BreweryTwin = factory();
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  const HISTORY_LIMIT = 90, HEARTBEAT_MS = 45000;
  const colors = { '0x107E3E': 'ok', '0xE76500': 'warn', '0xBB0000': 'err' };
  function severity(metric, value) {
    if (!Number.isFinite(value)) return 'unknown';
    const thresholds = metric.thresholds || [];
    const band = thresholds.find(t => value <= t.limit);
    if (band) return colors[band.color.toUpperCase().replace('0X', '0x')] || 'ok';
    // Low-stock profiles end below max; their upper band is healthy stock.
    if (thresholds.length && thresholds.at(-1).limit < metric.max_val) return 'ok';
    return thresholds.length ? 'err' : 'ok';
  }
  class Store {
    constructor(profile, {demo = false} = {}) {
      this.profile = profile; this.demo = demo; this.assets = new Map();
      for (const group of ['machines', 'tanks']) for (const definition of profile[group]) {
        this.assets.set(definition.id, { ...definition, group, values: new Map(), status: null, pulses: null, updatedAt: 0 });
      }
      this.connected = demo; this.generation = 0; this.availability = null;
      this.heartbeatAt = 0; this.received = 0; this.operator = null; this.events = [];
    }
    connection(up) {
      if (up && !this.connected) { this.generation++; this.availability = null; this.heartbeatAt = 0; }
      this.connected = up;
    }
    event(message, kind = 'info', at = Date.now()) {
      this.events.unshift({message, kind, at}); this.events.length = Math.min(this.events.length, 30);
    }
    ingest(topic, raw, at = Date.now(), retained = false) {
      const payload = String(raw).trim();
      if (payload.length > 4096) return false;
      if (topic === 'factory/device/status') {
        if (!['online', 'offline'].includes(payload)) return false;
        // A device restart is a new observation session even when the browser's
        // broker connection stayed open. Do not revive pre-outage fields.
        if (payload === 'online' && this.availability === 'offline') this.generation++;
        if (payload !== this.availability) this.event(`Rig ${payload}`, payload === 'online' ? 'ok' : 'warn', at);
        this.availability = payload; this.heartbeatAt = at; return true;
      }
      if (topic === 'factory/device/performance') {
        try { const p = JSON.parse(payload); if (!p || typeof p !== 'object' || Array.isArray(p)) return false; }
        catch (_) { return false; }
        if (!retained) this.heartbeatAt = at;
        return true;
      }
      if (topic === 'factory/operators/login') {
        try {
          const p = JSON.parse(payload);
          if (!p || typeof p.name !== 'string' || !p.name.trim()) return false;
          this.operator = {name: p.name.slice(0, 100), role: typeof p.role === 'string' ? p.role.slice(0, 100) : 'Operator', at, generation: this.generation};
          this.event(`${this.operator.name} · badge received`, 'info', at);
        } catch (_) { return false; }
        if (!retained) this.heartbeatAt = at;
        return true;
      }
      const parts = topic.split('/');
      if (parts.length !== 4 || parts[0] !== 'factory') return false;
      const [, group, id, key] = parts, asset = this.assets.get(id);
      if (!asset || group !== asset.group) return false;
      let sample;
      if (group === 'machines' && key === 'status') {
        if (!['RUNNING', 'STOPPED'].includes(payload)) return false;
        if (asset.status?.value !== payload) this.event(`${asset.name} ${payload.toLowerCase()}`, payload === 'RUNNING' ? 'ok' : 'warn', at);
        asset.status = {value: payload, at, generation: this.generation, retained};
      } else {
        if (!/^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(payload)) return false;
        const value = Number(payload); if (!Number.isFinite(value)) return false;
        if (group === 'machines' && key === 'pulses') {
          if (value < 0 || !Number.isSafeInteger(value)) return false;
          asset.pulses = {value, at, generation: this.generation, retained};
        } else {
          const metric = asset.metrics.find(m => m.key === key); if (!metric) return false;
          const previous = asset.values.get(key);
          const history = previous?.generation === this.generation ? previous.history : [];
          history.push({at, value}); if (history.length > HISTORY_LIMIT) history.shift();
          sample = {value, at, generation: this.generation, retained, history};
          asset.values.set(key, sample);
          if (severity(metric, value) !== 'ok' && (!previous || severity(metric, previous.value) !== severity(metric, value))) {
            this.event(`${asset.name} · ${metric.label} ${value} ${metric.unit}`, severity(metric, value), at);
          }
        }
      }
      asset.updatedAt = at; this.received++;
      if (!retained) this.heartbeatAt = at;
      return true;
    }
    quality(sample, now = Date.now()) {
      if (!sample) return 'waiting';
      if (this.demo) return 'demo';
      if (sample.generation !== this.generation || sample.retained) return 'previous';
      if (!this.connected || this.availability === 'offline' || !this.heartbeatAt || now - this.heartbeatAt > HEARTBEAT_MS) return 'stale';
      return 'live';
    }
    current(sample, now = Date.now()) { return ['live', 'demo'].includes(this.quality(sample, now)); }
    status(asset, now = Date.now()) {
      const sample = asset.group === 'machines' ? asset.status : asset.values.get('level');
      const q = this.quality(sample, now);
      if (q === 'waiting') return 'waiting';
      if (!['live', 'demo'].includes(q)) return 'stale';
      if (asset.group === 'machines') return sample.value === 'RUNNING' ? 'running' : 'stopped';
      return severity(asset.metrics.find(m => m.key === 'level'), sample.value);
    }
  }
  function demoTick(store, elapsed, at = Date.now()) {
    store.availability = 'online'; store.heartbeatAt = at;
    for (const asset of store.assets.values()) {
      const base = `factory/${asset.group}/${asset.id}`;
      if (asset.group === 'machines') {
        store.ingest(`${base}/status`, asset.running ? 'RUNNING' : 'STOPPED', at);
        // Profile counter seeds are held separately from observed samples.
        const seed = store.profile.machines.find(a => a.id === asset.id).pulses || 0;
        store.ingest(`${base}/pulses`, seed + (asset.auto_pulse && asset.running ? Math.floor(elapsed / 3) : 0), at);
      }
      for (const [index, metric] of asset.metrics.entries()) {
        const sim = metric.sim || {}, initial = metric.init_val;
        let value = initial;
        if (sim.profile === 'sine') value = (sim.offset ?? initial) + (sim.amplitude || 0) * Math.sin(elapsed * Math.PI * 2 / (sim.period_s || 60));
        if (sim.profile === 'random_walk') value = (sim.rand_min + sim.rand_max) / 2 + (sim.rand_max - sim.rand_min) / 2 * Math.sin(elapsed / 14 + index * 2 + asset.id.length);
        if (sim.profile === 'ramp') value = metric.min_val + ((initial - metric.min_val + elapsed * (metric.max_val - metric.min_val) / 240) % (metric.max_val - metric.min_val));
        store.ingest(`${base}/${metric.key}`, value.toFixed(2), at);
      }
    }
  }
  return {Store, severity, demoTick, HISTORY_LIMIT, HEARTBEAT_MS};
});
