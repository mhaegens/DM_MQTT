const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const validator = require('../dashboard/html/assets/config-validation.js');
const template = JSON.parse(fs.readFileSync(path.join(__dirname,'../firmware/main/spiffs_image/sapdm_config.json'),'utf8'));
for (const file of ['sapdm_config.json','config_bakery.json','config_brewery.json','config_ev_battery.json']) {
  test(`firmware template ${file} passes editor validation`,()=>assert.deepEqual(validator.errors(JSON.parse(fs.readFileSync(path.join(__dirname,'../firmware/main/spiffs_image',file),'utf8'))),[]));
}
test('invalid bounds, duplicate ids, excess entries and UTF-8 overflow are rejected',()=>{
  const bad = structuredClone(template); bad.machines[0].metrics[0].step=0;
  assert(validator.errors(bad).some(e=>e.includes('step')));
  bad.machines[1].id=bad.machines[0].id;
  assert(validator.errors(bad).some(e=>e.includes('duplicate id')));
  bad.plant_name='é'.repeat(20);
  assert(validator.errors(bad).some(e=>e.includes('UTF-8')));
  bad.tanks=Array(4).fill(template.tanks[0]);
  assert(validator.errors(bad).some(e=>e.includes('maximum 3')));
});
test('unsafe topic identifiers and malformed structures are rejected',()=>{
  const bad=structuredClone(template); bad.machines[0].id='bad/+/id';
  assert.throws(()=>validator.assert(bad));
  assert.throws(()=>validator.assert({machines:{}}));
  assert.throws(()=>validator.assert(null));
});
