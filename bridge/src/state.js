'use strict';
const fs = require('fs');
const path = require('path');

const STATE_PATH = path.join(__dirname, '../state/state.json');
let _state = {};

function load() {
  try {
    const raw = fs.readFileSync(STATE_PATH, 'utf8');
    _state = JSON.parse(raw);
    log({ event: 'state_loaded', machines: Object.keys(_state).length });
  } catch {
    _state = {};
  }
}

function persist() {
  try {
    fs.mkdirSync(path.dirname(STATE_PATH), { recursive: true });
    fs.writeFileSync(STATE_PATH, JSON.stringify(_state, null, 2));
  } catch (err) {
    log({ event: 'state_persist_error', error: err.message });
  }
}

function getMachineState(machineId) {
  return _state[machineId] || null;
}

function setMachineState(machineId, updates) {
  _state[machineId] = { ...(_state[machineId] || {}), ...updates };
  persist();
}

function clearMachineState(machineId) {
  delete _state[machineId];
  persist();
}

// M3: resolve active or in-queue SFC from DM API
// Returns { sfc, shopOrder, operation } or null
async function resolveSfc(plant, resource, operation) {
  // TODO M3: call dm-client GET /sfc/v1/sfcsInWork?plant=&resource=
  // then fall back to GET /sfc/v1/worklist/sfcs?sfcStatuses=IN_QUEUE&resource=
  return null;
}

function log(obj) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), src: 'state', ...obj }));
}

module.exports = { load, persist, getMachineState, setMachineState, clearMachineState, resolveSfc };
