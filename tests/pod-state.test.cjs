const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {EventEmitter}=require('node:events');
const State=require('../pod-plugin/widget/State.js');
const Connection=require('../pod-plugin/widget/Connection.js');
const profile=JSON.parse(fs.readFileSync(path.join(__dirname,'../pod-plugin/config/brewery.json')));
const first='factory/machines/mash01/mashTemp';

test('runtime starts with unknown values, not configured sample readings',()=>{
  const state=new State(profile);assert([...state.metrics.values()].every(m=>m.value===null));assert.equal(state.assets.get('machines/mash01').status,null);
});
test('retained online availability alone does not pretend readings are fresh',()=>{
  const state=new State(profile);state.setConnection('connected');state.apply('factory/device/status','online');
  assert.equal(state.health().label,'Waiting for device');state.apply(first,'66.5');assert.equal(state.health().label,'Live readings');
});
test('10,000 changes preserve metric count and bound chart history',()=>{
  const state=new State(profile);const size=state.metrics.size;
  for(let i=0;i<10000;i++)assert(state.apply(first,String(i)));
  assert.equal(state.metrics.size,size);assert.equal(state.metrics.get(first).value,9999);assert.equal(state.metrics.get(first).history.length,60);
  for(let i=0;i<10000;i++)state.apply('factory/machines/unknown/'+i,String(i));
  assert.equal(state.metrics.size,size);assert.equal(state.unmapped,10000);
});
test('quiet unchanged metrics remain live through device diagnostics',()=>{
  let now=1000;const state=new State(profile,{now:()=>now});state.setConnection('connected');
  state.apply(first,'66.50');now=90000;state.apply('factory/device/performance','{"rtt_ms":54}');
  assert.equal(state.metrics.get(first).value,66.5);assert.equal(state.health().stale,false);
  now+=31000;assert.equal(state.health().label,'Readings stale');
  state.apply('factory/device/status','offline');assert.equal(state.health().label,'Device offline');
});
test('numeric validation rejects malformed payloads without replacing last good value',()=>{
  const state=new State(profile);state.apply(first,'66.50');
  for(const value of ['', ' ', 'NaN','Infinity','12oops','0x10','[]'])assert.equal(state.apply(first,value),false);
  assert.equal(state.metrics.get(first).value,66.5);assert.equal(state.invalid,7);
});
test('identical metrics do not grow histories; identical operator events remain events',()=>{
  const state=new State(profile);for(let i=0;i<2000;i++)state.apply(first,'66.50');
  assert.equal(state.metrics.get(first).history.length,1);
  const event='{"name":"Test operator","role":"Operator"}';state.apply('factory/operators/login',event);state.apply('factory/operators/login',event);assert.equal(state.operatorCount,2);
});
test('custom nested prefix, pulse aliases and independent instances',()=>{
  const a=new State(profile,{prefix:'demo/line'}),b=new State(profile,{prefix:'demo/line'});
  a.apply('demo/line/machines/mash01/pulses','123');assert.equal(a.assets.get('machines/mash01').pulses,123);assert.equal(b.assets.get('machines/mash01').pulses,null);
  a.apply('demo/line/machines/mash01/pulse','124');assert.equal(a.assets.get('machines/mash01').pulses,124);
  a.apply('demo/line/machines/mash01/status','RUNNING');assert.equal(a.assets.get('machines/mash01').status,'RUNNING');
});
test('profiles reject duplicate/unsafe identifiers, excessive assets and invalid ranges',()=>{
  let bad=structuredClone(profile);bad.machines[1].id=bad.machines[0].id;assert.throws(()=>new State(bad));
  bad=structuredClone(profile);bad.machines[0].metrics[0].key='rpm/+';assert.throws(()=>new State(bad));
  bad=structuredClone(profile);bad.tanks=Array(4).fill(bad.tanks[0]);assert.throws(()=>new State(bad));
  bad=structuredClone(profile);bad.machines[0].metrics[0].max_val=30;assert.throws(()=>new State(bad));
  assert.throws(()=>new State(profile,{prefix:'factory/#'}));assert.throws(()=>new State(profile,{staleSeconds:1}));
});
test('all bundled profiles and the maximum 104-metric scenario load',()=>{
  for(const file of ['factory','brewery','bakery','ev_battery'])assert(new State(JSON.parse(fs.readFileSync(path.join(__dirname,'../pod-plugin/config',file+'.json')))).metrics.size>0);
  const max=JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures/performance-104-metrics.json')));assert.equal(new State(max).metrics.size,104);
});

class Client extends EventEmitter {
  constructor(){super();this.connected=true;this.subscriptions=[];this.published=[];this.ended=false;}
  subscribe(topic,options,done){this.subscriptions.push({topic,options});this.ack=done;}
  publish(topic,payload,options,done){this.published.push({topic,payload,options});done?.();}
  end(force){this.ended=force;}
}
function setup(url='wss://mqtt.example.com') {
  const clients=[],mqtt={connect:()=>{const c=new Client();clients.push(c);return c;}},state=new State(profile);
  const connection=new Connection(mqtt,state,{brokerUrl:url});return {clients,state,connection};
}
test('one client/listener; full state is requested only after successful subscription',()=>{
  const {clients,state,connection}=setup();connection.start();connection.start();assert.equal(clients.length,1);
  const c=clients[0];c.emit('connect');assert.equal(c.listenerCount('message'),1);assert.equal(c.published.length,0);
  c.ack(null,[{topic:'factory/#',qos:0}]);assert.deepEqual(c.published,[{topic:'factory/device/request_snapshot',payload:'state',options:{qos:0,retain:false}}]);
  c.emit('message',first,Buffer.from('72.5'));assert.equal(state.metrics.get(first).value,72.5);
  c.emit('connect');assert.equal(state.metrics.get(first).value,null);assert.equal(c.listenerCount('message'),1);connection.destroy();
});
test('reconnect/exit close clients, detach listeners, and ignore late callbacks',()=>{
  const {clients,state,connection}=setup();connection.start();const old=clients[0];old.emit('connect');const lateAck=old.ack;
  connection.reconnect();assert.equal(old.ended,true);assert.equal(old.eventNames().length,0);assert.equal(clients.length,2);
  lateAck(null,[{topic:'factory/#',qos:0}]);assert.equal(old.published.length,0);
  const current=clients[1];current.emit('connect');const pending=current.ack;connection.destroy();pending(null,[{qos:0}]);
  assert.equal(current.ended,true);assert.equal(current.eventNames().length,0);assert.equal(current.published.length,0);connection.start();assert.equal(clients.length,2);assert.equal(state.metrics.get(first).value,null);
});
test('WSS, no embedded credentials, and denied subscriptions are enforced',()=>{
  for(const url of ['ws://mqtt.example.com','https://mqtt.example.com','wss://user:example@mqtt.example.com']){const s=setup(url);s.connection.start();assert.equal(s.clients.length,0);assert.equal(s.state.connection,'error');}
  const s=setup();s.connection.start();s.clients[0].emit('connect');s.clients[0].ack(null,[{qos:128}]);assert.equal(s.state.connection,'error');assert.equal(s.clients[0].published.length,0);s.connection.destroy();
});
