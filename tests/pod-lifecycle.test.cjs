const {test}=require('node:test');
const assert=require('node:assert/strict');
const {runInNewContext}=require('node:vm');
const fs=require('node:fs');
const path=require('node:path');
const State=require('../pod-plugin/widget/State.js');
const source=fs.readFileSync(path.join(__dirname,'../pod-plugin/widget/MqttDashboard.js'),'utf8');
const profile=JSON.parse(fs.readFileSync(path.join(__dirname,'../pod-plugin/config/brewery.json')));

function harness({run=true,delayed=false}={}) {
  const stats={root:0,html:0,connect:0,views:0,viewDestroy:0,connectionDestroy:0,baseInit:0,baseExit:0,fetches:0},doms=new Map();let Type,resolveFetch;
  class Base {constructor(config){this.config=config;}getConfig(){return this.config;}getPropertyValue(k){return this.config.properties?.[k];}getProperties(){return [];}async onInit(){stats.baseInit++;}onExit(){stats.baseExit++;}}
  class VBox {constructor(id,options){this.id=id;this.options=options;stats.root++;}}
  class HTML {constructor(id,options){this.id=id;this.options=options;stats.html++;}getDomRef(){return doms.get(this.id)||null;}}
  class Property {constructor(options){Object.assign(this,options);}}
  class Renderer {constructor(root,state){this.root=root;this.state=state;stats.views++;}schedule(){}destroy(){stats.viewDestroy++;}}
  class Connection {start(){stats.connect++;}destroy(){stats.connectionDestroy++;}requestSnapshot(){}reconnect(){}}
  const deps=[VBox,HTML,Base,Property,class {},class {},{Main:'main'},{isRunMode:()=>run},class {},State,Renderer,Connection,{}];
  const sap={ui:{define:(_names,factory)=>{Type=factory(...deps);},require:{toUrl:p=>'/modules/'+p}}};
  const fetch=async()=>{stats.fetches++;if(delayed)return new Promise(resolve=>{resolveFetch=()=>resolve({ok:true,json:async()=>profile});});return {ok:true,json:async()=>profile};};
  runInNewContext(source,{sap,document:{getElementById:()=>null,createElement:()=>({}),head:{append(){}}},location:{protocol:'https:'},fetch,AbortController,URL,console});
  const widget=new Type({id:'mqtt-widget-1',properties:{brokerUrl:'wss://mqtt.example.com'}});
  return {widget,stats,doms,resolve:()=>resolveFetch?.()};
}
test('repeated POD view requests reuse one exact root and HTML control',()=>{
  const {widget,stats}=harness();const a=widget._createView(),b=widget._createView();
  assert.equal(a,b);assert.equal(a.id,'mqtt-widget-1');assert.equal(stats.root,1);assert.equal(stats.html,1);
  assert(widget._html.options.content.includes('id="'+widget._html.id+'"'));
});
test('repeat init/render callbacks start one connection and mount one dashboard',async()=>{
  const {widget,stats,doms}=harness();widget._createView();const dom={};doms.set(widget._html.id,dom);
  await Promise.all([widget.onInit(),widget.onInit()]);widget._mount();widget._html.options.afterRendering();
  assert.equal(stats.baseInit,1);assert.equal(stats.fetches,1);assert.equal(stats.connect,1);assert.equal(stats.views,1);
  doms.set(widget._html.id,{});widget._mount();assert.equal(stats.views,2);assert.equal(stats.viewDestroy,1);assert.equal(stats.connect,1);
  widget.onExit();assert.equal(stats.connectionDestroy,1);assert.equal(stats.viewDestroy,2);assert.equal(stats.baseExit,1);
});
test('POD Designer previews sample values without opening a broker connection',async()=>{
  const {widget,stats}=harness({run:false});widget._createView();await widget.onInit();
  assert.equal(stats.connect,0);assert.equal(widget._state.connection,'preview');assert([...widget._state.metrics.values()].every(m=>m.value!==null));widget.onExit();
});
test('exiting during asynchronous profile loading cannot resurrect the widget',async()=>{
  const {widget,stats,resolve}=harness({delayed:true});widget._createView();const pending=widget.onInit();await Promise.resolve();await Promise.resolve();widget.onExit();resolve();await pending;
  assert.equal(stats.connect,0);assert.equal(stats.views,0);assert.equal(widget._state,null);
});
