const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {join} = require('node:path');
const {runInNewContext} = require('node:vm');
const source = readFileSync(join(__dirname,'../dashboard/html/assets/mqtt-connection.js'),'utf8');
function connection({protocol='https:',hostname='mqtt.example.com',site='',saved=''}={}) {
  const window = {DM_MQTT_PUBLIC_BROKER:site};
  runInNewContext(source,{window,location:{protocol,hostname},URL,
    localStorage:{getItem:()=>saved,setItem:()=>{}}});
  return window.dmMqtt;
}
test('public deployment derives WSS from its hostname without a private endpoint in source',()=>{
  assert.equal(connection().url,'wss://mqtt.example.com');
});
test('LAN browser uses the public WSS site override',()=>{
  assert.equal(connection({protocol:'http:',hostname:'192.0.2.10',site:'wss://mqtt.example.com'}).url,'wss://mqtt.example.com');
});
test('explicitly saved broker preference remains available',()=>{
  assert.equal(connection({saved:'wss://alternate.example.com'}).url,'wss://alternate.example.com');
});
test('broker editor rejects mixed content and non-WebSocket protocols',()=>{
  const c=connection();
  assert.throws(()=>c.save('ws://mqtt.example.com'),/secure page/);
  assert.throws(()=>c.save('https://mqtt.example.com'),/ws:\/\//);
  c.save('wss://alternate.example.com');
  assert.equal(c.url,'wss://alternate.example.com');
});
