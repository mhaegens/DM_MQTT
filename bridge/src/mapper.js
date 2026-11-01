'use strict';
const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');
const { ThrottleMap } = require('./throttle');
const dm = require('./dm-client');

const CONFIG_PATH = path.join(__dirname, '../config/mapping.yaml');
let _mapping = null;

const throttle = new ThrottleMap(5000);
throttle.setFlushCallback(flushIndicator);

function loadMapping() {
  try {
    const raw = fs.readFileSync(CONFIG_PATH, 'utf8');
    _mapping = yaml.load(raw);
    log({
      event: 'mapping_loaded',
      machines: Object.keys(_mapping?.machines || {}).length,
      tanks: Object.keys(_mapping?.tanks || {}).length,
    });
  } catch (err) {
    _mapping = { version: 1, defaults: {}, machines: {}, tanks: {} };
    log({ event: 'mapping_not_found', note: 'edit Bridge Mapping tab in the editor to configure' });
  }
}

function getMapping() { return _mapping; }

function reloadMapping() {
  loadMapping();
}

async function handleSignal(signal) {
  log({ event: 'signal', type: signal.type, id: signal.machineId || signal.tankId, metric: signal.metric, value: signal.value });

  if (signal.type === 'metric') {
    const val = parseFloat(signal.value);
    if (!isNaN(val)) throttle.submit(`${signal.machineId}:${signal.metric}`, val);
  }

  if (signal.type === 'tank_metric') {
    const val = parseFloat(signal.value);
    if (!isNaN(val)) throttle.submit(`tank:${signal.tankId}:${signal.metric}`, val);
  }

  if (signal.type === 'status') {
    // Flush buffered indicators immediately on any status change
    throttle.flushPrefix(`${signal.machineId}:`);
    // M3: RUNNING -> dm.startSfc + dm.startResourceUsage
    // M3: STOPPED -> dm.signoffSfc + dm.stopResourceUsage + dm.recordDowntime
  }

  if (signal.type === 'pulse') {
    // M3: compute delta vs state.lastPulse, queue for quantityConfirmation
  }

  if (signal.type === 'operator_login') {
    // M3: optional labor assignment
  }
}

async function flushIndicator(key, value) {
  if (!_mapping) return;
  const plant = _mapping.defaults?.plant || process.env.DM_PLANT;

  if (key.startsWith('tank:')) {
    // key = "tank:tankId:metric"
    const parts = key.split(':');
    const tankId = parts[1];
    const metric = parts[2];
    const tm = _mapping.tanks?.[tankId];
    if (!tm?.indicators?.[metric]) return;
    const { id: indicator, unit } = tm.indicators[metric];
    await dm.postIndicatorValue(plant, tm.workCenter, tankId, indicator, String(value), unit);
  } else {
    // key = "machineId:metric"
    const colonIdx = key.indexOf(':');
    const machineId = key.slice(0, colonIdx);
    const metric = key.slice(colonIdx + 1);
    const mm = _mapping.machines?.[machineId];
    if (!mm?.indicators?.[metric]) return;
    const { id: indicator, unit } = mm.indicators[metric];
    await dm.postIndicatorValue(plant, mm.workCenter, mm.resource || machineId, indicator, String(value), unit);
  }
}

function log(obj) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), src: 'mapper', ...obj }));
}

module.exports = { loadMapping, getMapping, reloadMapping, handleSignal };
