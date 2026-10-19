const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {Store,severity,demoTick,HISTORY_LIMIT,HEARTBEAT_MS} = require('../dashboard/html/twin/state.js');
const profile = require('../dashboard/html/twin/brewery.json');
const start = 100000;
function live() {const s=new Store(profile);s.connection(true);s.ingest('factory/device/status','online',start);return s;}
test('twin profile exactly matches the standard firmware brewery profile',()=>{
  const firmware=JSON.parse(fs.readFileSync(path.join(__dirname,'../firmware/main/spiffs_image/config_brewery.json'),'utf8'));
  assert.deepEqual(profile,firmware);assert.equal(new Store(profile).assets.size,8);
});
test('live mode starts unknown and rejects foreign IDs, malformed values and unsafe counters',()=>{
  const s=live(),a=s.assets.get('mash01');assert.equal(a.values.size,0);assert.equal(a.status,null);assert.equal(a.pulses,null);
  for(const value of ['',' ','65abc','Infinity','NaN','{}','0x15'])assert.equal(s.ingest('factory/machines/mash01/mashTemp',value,start),false);
  for(const topic of ['factory/tanks/mash01/mashTemp','factory/machines/cnc01/rpm','factory/machines/mash01/invented','factory/machines/mash01/mashTemp/extra'])assert.equal(s.ingest(topic,'10',start),false);
  for(const value of ['-1','3.5','9007199254740992'])assert.equal(s.ingest('factory/machines/mash01/pulses',value,start),false);
  assert.equal(s.ingest('factory/machines/mash01/status','running',start),false);
  assert.equal(s.ingest('factory/machines/mash01/mashTemp','66.5',start),true);
  assert.equal(a.values.get('mashTemp').value,66.5);
});
test('change-only metrics stay current through heartbeat, then become stale on silence or offline',()=>{
  const s=live(),a=s.assets.get('mash01');s.ingest('factory/machines/mash01/status','RUNNING',start);s.ingest('factory/machines/mash01/mashTemp','66.5',start);
  const sample=a.values.get('mashTemp');
  s.ingest('factory/device/performance','{"rtt_ms":12}',start+120000);
  assert.equal(s.quality(sample,start+120100),'live');assert.equal(s.status(a,start+120100),'running');
  assert.equal(s.quality(sample,start+120000+HEARTBEAT_MS+1),'stale');
  s.ingest('factory/device/status','offline',start+120200);assert.equal(s.status(a,start+120201),'stale');
  assert.equal(sample.value,66.5);
});
test('reconnect invalidates old fields and retained messages until a fresh snapshot arrives',()=>{
  const s=live(),a=s.assets.get('mash01');s.ingest('factory/machines/mash01/status','RUNNING',start);s.ingest('factory/machines/mash01/mashTemp','66.5',start);
  s.connection(false);assert.equal(s.status(a,start+1),'stale');s.connection(true);s.ingest('factory/device/status','online',start+2);
  assert.equal(s.quality(a.values.get('mashTemp'),start+3),'previous');
  s.ingest('factory/machines/mash01/status','RUNNING',start+4,true);assert.equal(s.status(a,start+5),'stale');
  s.ingest('factory/machines/mash01/status','STOPPED',start+6);assert.equal(s.status(a,start+7),'stopped');
  assert.equal(s.quality(a.values.get('mashTemp'),start+7),'previous');
  s.ingest('factory/machines/mash01/mashTemp','67',start+8);assert.equal(s.quality(a.values.get('mashTemp'),start+9),'live');assert.equal(a.values.get('mashTemp').history.length,1);
});
test('bursts preserve the last exact value and bound histories, assets and event log',()=>{
  const s=live();for(let i=0;i<10000;i++)s.ingest('factory/machines/mash01/mashTemp',String(i/100),start+i);
  const sample=s.assets.get('mash01').values.get('mashTemp');assert.equal(sample.value,99.99);assert.equal(sample.history.length,HISTORY_LIMIT);assert.equal(s.assets.size,8);
  for(let i=0;i<100;i++)s.ingest('factory/machines/mash01/status',i%2?'RUNNING':'STOPPED',start+i);
  assert.equal(s.events.length,30);
});
test('a device reconnect cannot revive pre-outage fields on an open broker connection',()=>{
  const s=live(),a=s.assets.get('mash01');s.ingest('factory/machines/mash01/status','RUNNING',start);s.ingest('factory/machines/mash01/mashTemp','66.5',start);
  s.ingest('factory/device/status','offline',start+1);s.ingest('factory/device/status','online',start+2);
  assert.equal(s.quality(a.values.get('mashTemp'),start+3),'previous');assert.equal(s.status(a,start+3),'stale');
  s.ingest('factory/machines/mash01/status','RUNNING',start+4);assert.equal(s.status(a,start+5),'running');
  assert.equal(s.quality(a.values.get('mashTemp'),start+5),'previous');
});
test('thresholds respect low stock, high spent grain, pressure and foam loss ranges',()=>{
  const metric=(id,key)=>new Store(profile).assets.get(id).metrics.find(m=>m.key===key);
  assert.equal(severity(metric('malt','level'),10),'err');assert.equal(severity(metric('malt','level'),25),'warn');assert.equal(severity(metric('malt','level'),58),'ok');
  assert.equal(severity(metric('spent','level'),34),'ok');assert.equal(severity(metric('spent','level'),85),'warn');assert.equal(severity(metric('spent','level'),95),'err');
  assert.equal(severity(metric('ferm01','pressure'),1.1),'ok');assert.equal(severity(metric('ferm01','pressure'),2),'warn');assert.equal(severity(metric('fill01','foamLoss'),5),'err');
});
test('demo is isolated, deterministic, bounded by profile ranges and never seeds live state',()=>{
  const a=new Store(profile,{demo:true}),b=new Store(profile,{demo:true});demoTick(a,100,start);demoTick(b,100,start);
  assert.deepEqual(a.assets,b.assets);assert.equal(a.status(a.assets.get('clean01'),start),'stopped');
  assert.equal(a.assets.get('mash01').pulses.value,82+33);
  for(const asset of a.assets.values())for(const metric of asset.metrics){const v=asset.values.get(metric.key).value;assert(v>=metric.min_val&&v<=metric.max_val);}
  const liveStore=new Store(profile);assert.equal(liveStore.assets.get('bright').values.size,0);
});
test('operator payloads are bounded text and boot timestamps are not treated as wall-clock time',()=>{
  const s=live();assert.equal(s.ingest('factory/operators/login','null',start),false);
  assert.equal(s.ingest('factory/operators/login',JSON.stringify({name:'<img src=x>',role:'Lead',ts:5}),start),true);
  assert.equal(s.operator.name,'<img src=x>');assert.equal(s.operator.at,start);
  assert.equal(s.ingest('factory/operators/login',JSON.stringify({name:'A'.repeat(200)}),start),true);assert.equal(s.operator.name.length,100);
});
