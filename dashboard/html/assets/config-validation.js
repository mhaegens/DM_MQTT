(function (root) {
  'use strict';
  const bytes = s => new TextEncoder().encode(s).length;
  function errors(config) {
    const out = [];
    const object = v => v && typeof v === 'object' && !Array.isArray(v);
    const fail = message => out.push(message);
    const text = (v, max, label, required = false) => {
      if (v === undefined && !required) return;
      if (typeof v !== 'string' || (required && !v) || (typeof v === 'string' && bytes(v) > max)) fail(`${label}: expected ${required ? 'nonempty ' : ''}text up to ${max} UTF-8 bytes`);
    };
    const token = (v, max, label) => { text(v, max, label, true); if (typeof v === 'string' && !/^[A-Za-z0-9_-]+$/.test(v)) fail(`${label}: use letters, numbers, _ or -`); };
    if (!object(config)) return ['Configuration must be a JSON object'];
    if (bytes(JSON.stringify(config, null, 2)) >= 32768) fail('Configuration must be smaller than 32 KB');
    text(config.plant_name, 39, 'plant_name');
    let hasArray = false;
    for (const [kind, limit] of [['machines', 10], ['tanks', 3], ['people', 12]]) {
      const list = config[kind];
      if (list === undefined) continue;
      if (!Array.isArray(list)) { fail(`${kind} must be an array`); continue; }
      hasArray = true;
      if (list.length > limit) fail(`${kind}: maximum ${limit} entries`);
      const ids = new Set();
      for (const [index, item] of list.entries()) {
        const label = `${kind}[${index}]`;
        if (!object(item)) { fail(`${label} must be an object`); continue; }
        token(item.id, 15, `${label}.id`);
        if (ids.has(item.id)) fail(`${label}: duplicate id`); ids.add(item.id);
        text(item.name, kind === 'people' ? 47 : 31, `${label}.name`);
        if (item.avatar_color !== undefined && !/^0x[0-9A-Fa-f]{6}$/.test(item.avatar_color)) fail(`${label}.avatar_color: use 0xRRGGBB`);
        if (kind === 'people') {
          for (const [field, max] of [['role',47],['role_key',15],['dept',31],['plant',31],['uid',23]]) text(item[field], max, `${label}.${field}`);
          if (item.certs !== undefined) {
            if (!Array.isArray(item.certs) || item.certs.length > 6) fail(`${label}.certs: maximum 6 strings`);
            else item.certs.forEach(c => text(c, 31, `${label}.certs`, true));
          }
          continue;
        }
        token(item.id, 15, `${label}.id`);
        if (kind === 'machines') text(item.type, 47, `${label}.type`);
        if (item.running !== undefined && typeof item.running !== 'boolean') fail(`${label}.running must be boolean`);
        if (item.auto_pulse !== undefined && typeof item.auto_pulse !== 'boolean') fail(`${label}.auto_pulse must be boolean`);
        if (item.pulses !== undefined && (!Number.isInteger(item.pulses) || item.pulses < 0 || item.pulses > 2147483647)) fail(`${label}.pulses must be a nonnegative 32-bit integer`);
        const metrics = item.metrics;
        if (!Array.isArray(metrics) || metrics.length > 8 || (kind === 'tanks' && !metrics.length)) { fail(`${label}.metrics: ${kind === 'tanks' ? '1–8' : '0–8'} metrics required`); continue; }
        const keys = new Set();
        for (const [k, metric] of metrics.entries()) {
          const ml = `${label}.metrics[${k}]`;
          if (!object(metric)) { fail(`${ml} must be an object`); continue; }
          token(metric.key, 31, `${ml}.key`);
          if (keys.has(metric.key)) fail(`${ml}: duplicate metric key`); keys.add(metric.key);
          if (metric.viz !== undefined && !['spark','arc','thermo','ph','tank','bullet'].includes(metric.viz)) fail(`${ml}.viz: unsupported visualization`);
          if (metric.input !== undefined && !['step','slider'].includes(metric.input)) fail(`${ml}.input: use step or slider`);
          if (metric.color !== undefined && !/^0x[0-9A-Fa-f]{6}$/.test(metric.color)) fail(`${ml}.color: use 0xRRGGBB`);
          text(metric.label, 47, `${ml}.label`); text(metric.unit, 15, `${ml}.unit`);
          for (const field of ['init_val','min_val','max_val','step']) if (!Number.isFinite(metric[field]) || Math.abs(metric[field]) > 1e9) fail(`${ml}.${field}: finite number within ±1e9 required`);
          if (!(metric.min_val < metric.max_val)) fail(`${ml}: min_val must be below max_val`);
          if (!(metric.step > 0)) fail(`${ml}: step must be positive`);
          if (metric.init_val < metric.min_val || metric.init_val > metric.max_val) fail(`${ml}: initial value outside bounds`);
          if (kind === 'tanks' && k === 0 && (metric.viz !== 'tank' || metric.min_val !== 0 || metric.max_val !== 100)) fail(`${ml}: tank level must be the first metric, viz tank, range 0–100`);
          if (metric.thresholds !== undefined) {
            if (!Array.isArray(metric.thresholds) || metric.thresholds.length > 3) fail(`${ml}.thresholds: maximum 3 entries`);
            else { let last = -Infinity; for (const threshold of metric.thresholds) {
              if (!object(threshold) || !Number.isFinite(threshold.limit) || Math.abs(threshold.limit) > 1e9 || threshold.limit < last || !/^0x[0-9A-Fa-f]{6}$/.test(threshold.color)) fail(`${ml}.thresholds: ascending finite limits and 0xRRGGBB colors required`);
              last = threshold?.limit;
            } }
          }
          const sim = metric.sim;
          if (sim !== undefined && !object(sim)) fail(`${ml}.sim must be an object`);
          if (sim?.profile !== undefined && !['random_walk','sine','ramp','steady'].includes(sim.profile)) fail(`${ml}: unsupported simulation profile`);
          for (const field of ['rand_min','rand_max','amplitude','period_s','offset']) if (sim?.[field] !== undefined && (!Number.isFinite(sim[field]) || Math.abs(sim[field]) > 1e9)) fail(`${ml}.sim.${field}: finite number within ±1e9 required`);
          if (sim?.period_s !== undefined && !(Number.isFinite(sim.period_s) && sim.period_s > 0)) fail(`${ml}: sine period must be positive`);
        }
      }
    }
    if (!hasArray) fail('Include at least one of machines, tanks or people');
    return out;
  }
  const api = { errors, assert(config) { const found = errors(config); if (found.length) throw Error(found.slice(0, 5).join('\n')); return config; } };
  if (typeof module !== 'undefined') module.exports = api;
  else root.dmConfigValidation = api;
})(typeof window === 'undefined' ? globalThis : window);
