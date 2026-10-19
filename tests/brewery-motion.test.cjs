const {test}=require('node:test');
const assert=require('node:assert/strict');
const {Store}=require('../dashboard/html/twin/state.js');
const {Motion,primaryMetric}=require('../dashboard/html/twin/motion.js');
const profile=require('../dashboard/html/twin/brewery.json');
const now=100000;
function live(){const s=new Store(profile);s.connection(true);s.ingest('factory/device/status','online',now);for(const id of ['fill01','mash01','boil01','ferm01'])s.ingest(`factory/machines/${id}/status`,'RUNNING',now);return s;}
function value(s,id,key,n){s.ingest(`factory/machines/${id}/${key}`,String(n),now);}
function advance(m,s,seconds){for(let i=0;i<Math.round(seconds*10);i++)m.step(s,.1,true,now);}
function near(a,b){assert(Math.abs(a-b)<1e-8,`${a} != ${b}`);}
test('bottle travel follows bottles/min proportionally without a speed cap',()=>{
  for(const rate of [0,60,180,400]){const s=live(),m=new Motion();value(s,'fill01','bottleCnt',rate);advance(m,s,1);near(m.get('fill01').bottles,rate/60);}
});
test('changing speed advances from the current position without teleporting or reversing',()=>{
  const s=live(),m=new Motion();value(s,'fill01','bottleCnt',240);advance(m,s,1);value(s,'fill01','bottleCnt',60);m.step(s,.1,true,now);near(m.get('fill01').bottles,4.1);value(s,'fill01','bottleCnt',300);m.step(s,.1,true,now);near(m.get('fill01').bottles,4.6);
});
test('liquid flow changes filling streams independently of conveyor speed',()=>{
  const s=live(),m=new Motion();value(s,'fill01','bottleCnt',120);value(s,'fill01','flowRate',1.3);advance(m,s,1);near(m.get('fill01').bottles,2);near(m.get('fill01').liquid,1.3/30);
  value(s,'fill01','flowRate',26);m.step(s,.1,true,now);near(m.get('fill01').bottles,2.2);near(m.get('fill01').liquid,1.3/30+26/30*.1);
});
test('stopped, offline, reconnecting and zero-rate equipment stops moving',()=>{
  const s=live(),m=new Motion();value(s,'fill01','bottleCnt',180);advance(m,s,1);const before=m.get('fill01').bottles;
  value(s,'fill01','bottleCnt',0);advance(m,s,1);near(m.get('fill01').bottles,before);
  value(s,'fill01','bottleCnt',180);s.ingest('factory/machines/fill01/status','STOPPED',now);advance(m,s,1);near(m.get('fill01').bottles,before);
  s.ingest('factory/machines/fill01/status','RUNNING',now);s.ingest('factory/device/status','offline',now);advance(m,s,1);near(m.get('fill01').bottles,before);
  s.ingest('factory/device/status','online',now);advance(m,s,1);near(m.get('fill01').bottles,before);
});
test('motion pause preserves phase and readings; resuming does not replay a hidden-tab gap',()=>{
  const s=live(),m=new Motion();value(s,'fill01','bottleCnt',120);advance(m,s,1);m.step(s,.1,false,now);near(m.get('fill01').bottles,2);assert.equal(m.get('fill01').bottleRate,120);
  m.step(s,5000,true,now);near(m.get('fill01').bottles,2.2);
});
test('stirring, CO2 and steam use their own observed signals',()=>{
  const s=live(),m=new Motion();value(s,'mash01','stirRpm',30);value(s,'ferm01','co2Rate',7.5);value(s,'boil01','boilTemp',22);advance(m,s,1);
  near(m.get('mash01').stirrer,Math.PI);near(m.get('ferm01').gas,.5);near(m.get('boil01').steam,0);assert.equal(m.get('boil01').boiling,false);
  value(s,'boil01','boilTemp',98);m.step(s,.1,true,now);assert.equal(m.get('boil01').boiling,true);assert(m.get('boil01').steam>0);
});
test('equipment summaries use operating rates, including bottles/min for the filler',()=>{
  const s=live();for(const [id,key] of Object.entries({mash01:'stirRpm',boil01:'boilProg',ferm01:'co2Rate',fill01:'bottleCnt',bright:'level'}))assert.equal(primaryMetric(s.assets.get(id)).key,key);
});
