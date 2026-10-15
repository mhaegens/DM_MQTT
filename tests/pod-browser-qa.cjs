/* Exercise the exact packaged renderer and connection against a disposable broker. */
const fs=require('node:fs');
const fsp=require('node:fs/promises');
const path=require('node:path');
const assert=require('node:assert/strict');
const {createServer}=require('node:http');
const dependency=n=>require(process.env.DM_QA_MODULES?path.join(process.env.DM_QA_MODULES,n):n);
const mqtt=dependency('mqtt'),{chromium}=dependency('playwright');
const {WebSocketServer,createWebSocketStream}=dependency('ws');
const root=path.resolve(__dirname,'../pod-plugin');
const output=process.env.DM_QA_OUTPUT||path.join(root,'.qa');
const profile=JSON.parse(fs.readFileSync(path.join(root,'config/brewery.json')));

(async()=>{
  fs.mkdirSync(output,{recursive:true});
  const broker=dependency('aedes')();
  const server=createServer(async(req,res)=>{
    try {
      let url=decodeURIComponent(new URL(req.url,'http://localhost').pathname);if(url.endsWith('/'))url+='index.html';
      const file=path.resolve(root,'.'+url);if(!file.startsWith(root+path.sep))throw Error('path');
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.svg':'image/svg+xml'})[path.extname(file)]||'text/plain');
      res.end(await fsp.readFile(file));
    } catch(_){res.writeHead(404);res.end('Not found');}
  });
  const sockets=new WebSocketServer({server,path:'/mqtt'});sockets.on('connection',socket=>broker.handle(createWebSocketStream(socket)));
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const port=server.address().port,base=`http://127.0.0.1:${port}`;
  let browser,pub;const errors=[];let snapshots=0;
  try {
    pub=mqtt.connect(`ws://127.0.0.1:${port}/mqtt`);await new Promise((resolve,reject)=>{pub.once('connect',resolve);pub.once('error',reject);});
    const send=(topic,payload,options={})=>new Promise((resolve,reject)=>pub.publish(topic,String(payload),{qos:1,...options},error=>error?reject(error):resolve()));
    const snapshot=async()=>{
      await send('factory/device/status','online',{retain:true});
      for(const kind of ['machines','tanks'])for(const e of profile[kind]) {
        if(kind==='machines'){await send(`factory/${kind}/${e.id}/status`,e.running?'RUNNING':'STOPPED');await send(`factory/${kind}/${e.id}/pulses`,e.pulses||0);}
        for(const m of e.metrics)await send(`factory/${kind}/${e.id}/${m.key}`,m.init_val??m.min_val);
      }
      await send('factory/device/performance','{"rtt_ms":24,"queue_ms":7,"pending":0,"failed":0,"rejected":0,"internal_free":123456}');
    };
    pub.on('message',(topic,payload)=>{if(topic==='factory/device/request_snapshot' && payload.toString()==='state'){snapshots++;snapshot().catch(e=>errors.push(e.message));}});
    await new Promise((resolve,reject)=>pub.subscribe('factory/device/request_snapshot',error=>error?reject(error):resolve()));
    browser=await chromium.launch({headless:true,executablePath:process.env.DM_QA_CHROME||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
    const context=await browser.newContext({viewport:{width:1440,height:1040}});
    const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
    await page.goto(base+'/preview/');await page.waitForFunction(()=>window.preview?.state.connection==='preview');
    await page.screenshot({path:path.join(output,'pod-desktop.png'),fullPage:true});
    await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.join(output,'pod-mobile.png'),fullPage:true});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Mobile preview overflows horizontally');
    await page.setViewportSize({width:1440,height:1040});
    await page.locator('#broker').fill(`ws://127.0.0.1:${port}/mqtt`);await page.locator('#connect').click();
    await page.waitForFunction(()=>window.preview?.state.connection==='connected' && [...preview.state.metrics.values()].every(m=>m.value!==null));
    const topic='factory/machines/mash01/mashTemp',selector=`#dashboard [data-topic="${topic}"]`;
    const card=await page.locator('.dm-machine[data-asset=mash01]').elementHandle();
    const metric=await page.locator(selector).elementHandle();
    const tank=await page.locator('.dm-tank[data-asset=bright] .dm-vessel').elementHandle();
    const before=await page.evaluate(()=>({elements:document.querySelectorAll('#dashboard *').length,height:document.getElementById('dashboard').scrollHeight}));
    const start=Date.now();for(let i=0;i<5000;i++)pub.publish(topic,String(60+(i%200)/10),{qos:0});await send(topic,'67.25');
    await page.waitForFunction(()=>document.querySelector('[data-topic="factory/machines/mash01/mashTemp"] .dm-reading').textContent==='67.25');
    const burstMs=Date.now()-start;
    assert(await card.evaluate(n=>n===document.querySelector('.dm-machine[data-asset=mash01]')),'Burst replaced machine card');
    assert(await metric.evaluate(n=>n===document.querySelector('[data-topic="factory/machines/mash01/mashTemp"]')),'Burst replaced metric');
    assert(await tank.evaluate(n=>n===document.querySelector('.dm-tank[data-asset=bright] .dm-vessel')),'Burst replaced tank SVG');
    assert.equal(await page.locator('#dashboard .dm-frame').count(),1);assert.equal(await page.locator('#dashboard .dm-machine').count(),5);assert.equal(await page.locator('#dashboard .dm-tank').count(),3);
    const after=await page.evaluate(()=>({elements:document.querySelectorAll('#dashboard *').length,height:document.getElementById('dashboard').scrollHeight,history:preview.state.metrics.get('factory/machines/mash01/mashTemp').history.length}));
    assert.equal(after.elements,before.elements,'Signal burst grew DOM');assert.equal(after.height,before.height,'Signal burst grew dashboard height');assert(after.history<=60);
    await send('factory/operators/login',JSON.stringify({name:'<img src=x onerror="window.injected=true">',role:'Operator'}));
    await page.waitForFunction(()=>document.querySelector('.dm-operator-identity strong').textContent.startsWith('<img'));
    assert.equal(await page.evaluate(()=>window.injected),undefined);assert.equal(await page.locator('.dm-operator img').count(),0);
    await page.evaluate(()=>{preview.state.lastDeviceSeen=Date.now()-31000;preview.view.render();});assert.equal(await page.locator('.dm-health').textContent(),'Readings stale');
    await send('factory/device/performance','{"rtt_ms":25}');await page.waitForFunction(()=>document.querySelector('.dm-health').textContent==='Live readings');
    assert.equal(await page.locator(selector+' .dm-reading').textContent(),'67.25','Quiet metric disappeared while device was alive');
    const previousSnapshots=snapshots;
    await page.getByRole('button',{name:'Reconnect',exact:true}).click();
    await page.waitForFunction(()=>preview.state.connection==='connected' && preview.state.metrics.get('factory/machines/mash01/mashTemp').value===66.5);
    assert(snapshots>previousSnapshots,'Reconnect did not request full state');assert(await card.evaluate(n=>n===document.querySelector('.dm-machine[data-asset=mash01]')));
    await page.evaluate(profile=>{
      const root=document.createElement('div');root.id='secondary-widget';root.style.cssText='position:absolute;left:-9999px;top:0;width:1000px';document.body.append(root);
      const state=new DmMqttState(profile);state.seedPreview(profile);const view=new DmMqttDashboard(root,state);window.secondary={state,view};
    },profile);
    const clipIds=await page.locator('clipPath').evaluateAll(nodes=>nodes.map(n=>n.id));assert.equal(new Set(clipIds).size,clipIds.length,'Widget instances share SVG clip IDs');
    await page.evaluate(()=>{secondary.state.apply('factory/machines/mash01/mashTemp','42');secondary.view.render();});
    assert.equal(await page.locator(selector+' .dm-reading').textContent(),'66.5','Second widget changed first widget values');
    await page.evaluate(()=>{secondary.view.destroy();document.getElementById('secondary-widget').remove();});
    for(const name of ['factory','bakery','ev_battery','brewery']) {
      const plant=JSON.parse(fs.readFileSync(path.join(root,'config',name+'.json'))).plant_name;
      await page.locator('#profile').selectOption(name);await page.waitForFunction(plant=>preview.state.connection==='preview' && preview.state.profile.plant_name===plant,plant);
      await page.waitForTimeout(80);assert.equal(await page.locator('#dashboard .dm-frame').count(),1);
    }
    const max=JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures/performance-104-metrics.json')));
    await page.evaluate(profile=>{
      preview.view.destroy();preview.connection.destroy();const state=new DmMqttState(profile);state.seedPreview(profile);const view=new DmMqttDashboard(document.getElementById('dashboard'),state);window.maxWidget={state,view};
    },max);
    assert.equal(await page.locator('#dashboard .dm-metric').count(),104);assert.equal(await page.locator('#dashboard .dm-machine').count(),10);assert.equal(await page.locator('#dashboard .dm-tank').count(),3);
    const maxBefore=await page.locator('#dashboard *').count();
    await page.evaluate(()=>{let i=0;for(let n=0;n<10000;n++){const m=[...maxWidget.state.metrics.values()][i++%104];maxWidget.state.apply(m.topic,String(m.min+(m.max-m.min)*.5));}maxWidget.view.render();});
    assert.equal(await page.locator('#dashboard *').count(),maxBefore);assert.equal(await page.locator('#dashboard .dm-frame').count(),1);
    await page.setViewportSize({width:390,height:844});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await page.evaluate(()=>maxWidget.view.destroy());assert.equal(errors.length,0,errors.join('\n'));
    const ui5=await context.newPage();ui5.on('pageerror',e=>{errors.push(e.message);console.error('UI5 harness:',e.message);});
    await ui5.goto(base+'/preview/ui5.html?broker='+encodeURIComponent(`ws://127.0.0.1:${port}/mqtt`));
    await ui5.waitForFunction(()=>window.ui5Harness?.widget._state?.metrics.get('factory/machines/mash01/mashTemp').value===66.5,{},{timeout:60000});
    const preserved=await ui5.locator('.dm-machine[data-asset=mash01]').elementHandle();
    for(let i=0;i<10;i++)await ui5.evaluate(()=>{ui5Harness.root.invalidate();ui5Harness.Core.applyChanges();});
    await send(topic,'68.75');await ui5.waitForFunction(()=>document.querySelector('.dm-machine[data-asset=mash01] .dm-reading').textContent==='68.75');
    assert.equal(await ui5.locator('.dm-frame').count(),1);assert.equal(await ui5.locator('.dm-machine').count(),5);
    assert(await preserved.evaluate(n=>n===document.querySelector('.dm-machine[data-asset=mash01]')),'UI5 invalidations recreated the dashboard');
    const ui5Version=await ui5.evaluate(()=>sap.ui.version);
    const ui5Width=await ui5.evaluate(()=>ui5Harness.root.getDomRef().getBoundingClientRect().width);assert(ui5Width>0);
    await ui5.evaluate(()=>{ui5Harness.widget.onExit();ui5Harness.root.destroy();ui5Harness.Core.applyChanges();});
    assert.equal(await ui5.locator('.dm-frame').count(),0);await ui5.close();assert.equal(errors.length,0,errors.join('\n'));
    const result={result:'PASS',burst_messages:5001,burst_ms:burstMs,maximum_metrics:104,dom_elements_before:before.elements,dom_elements_after:after.elements,height_before:before.height,height_after:after.height,snapshot_requests:snapshots,ui5_version:ui5Version,
      checks:['fixed DOM and page height','last reading accuracy','bounded histories','tank SVG reuse','safe operator text','change-only staleness','reconnect snapshots','independent instances','profile switching','104 metrics at desktop/mobile widths','real OpenUI5 container after ten invalidations','UI5 teardown'],screenshots:'Preview screenshots use simulated data.'};
    fs.writeFileSync(path.join(output,'results.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));
  } finally {
    await browser?.close();if(pub)await new Promise(resolve=>pub.end(true,resolve));
    for(const socket of sockets.clients)socket.terminate();await new Promise(resolve=>sockets.close(resolve));await new Promise(resolve=>broker.close(resolve));await new Promise(resolve=>server.close(resolve));
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
