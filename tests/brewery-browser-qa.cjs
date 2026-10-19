/* Self-contained browser QA. All MQTT traffic stays on a disposable broker. */
const path=require('node:path'),fs=require('node:fs/promises'),assert=require('node:assert/strict');
const {createServer}=require('node:http');
const dep=name=>require(process.env.DM_QA_MODULES?path.join(process.env.DM_QA_MODULES,name):name);
const {chromium}=dep('playwright'),mqtt=dep('mqtt'),aedes=dep('aedes'),{WebSocketServer,createWebSocketStream}=dep('ws');
const root=path.resolve(__dirname,'../dashboard/html'),profile=require('../dashboard/html/twin/brewery.json');
const output=process.env.DM_QA_OUTPUT||'/tmp/dm-brewery-browser-qa';
(async()=>{
  await fs.mkdir(output,{recursive:true});
  const broker=aedes(),server=createServer(async(req,res)=>{
    try {
      let url=new URL(req.url,'http://localhost').pathname;if(url.endsWith('/'))url+='index.html';
      const file=path.resolve(root,'.'+decodeURIComponent(url));if(!file.startsWith(root+path.sep))throw Error('path');
      const data=url==='/assets/site-config.js'?'window.DM_MQTT_PUBLIC_BROKER = "ws://" + location.host + "/mqtt";':await fs.readFile(file);
      res.setHeader('Content-Type',({'.html':'text/html','.css':'text/css','.js':'text/javascript','.json':'application/json','.svg':'image/svg+xml'})[path.extname(file)]||'application/octet-stream');res.end(data);
    }catch(_){res.writeHead(404);res.end('Not found');}
  });
  const ws=new WebSocketServer({server,path:'/mqtt'});ws.on('connection',socket=>broker.handle(createWebSocketStream(socket)));
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base=`http://127.0.0.1:${server.address().port}`;
  let browser,publisher;
  try {
    publisher=mqtt.connect(base.replace('http','ws')+'/mqtt');await new Promise(resolve=>publisher.once('connect',resolve));
    const send=(topic,payload)=>new Promise((resolve,reject)=>publisher.publish(topic,String(payload),{qos:1},error=>error?reject(error):resolve()));
    let snapshots=0;await new Promise((resolve,reject)=>publisher.subscribe('factory/device/request_snapshot',error=>error?reject(error):resolve()));
    publisher.on('message',(topic,payload)=>{if(topic==='factory/device/request_snapshot'&&payload.toString()==='state')snapshots++;});
    browser=await chromium.launch({headless:true,executablePath:process.env.DM_QA_CHROME||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
    const context=await browser.newContext({viewport:{width:1440,height:1100},timezoneId:'Europe/Amsterdam'});
    // Never inherit a production URL from a browser preference or deployment override.
    await context.addInitScript(url=>localStorage.setItem('sapdm-broker-url',url),base.replace('http','ws')+'/mqtt');
    const page=await context.newPage(),errors=[],sockets=[];page.on('pageerror',error=>errors.push(error.message));page.on('websocket',socket=>sockets.push(socket.url()));
    await page.goto(base+'/digital-twins/');await page.waitForSelector('[data-twin=brewery-ge01]');
    assert.equal(await page.locator('.twin-card').count(),1);
    assert.deepEqual(await page.locator('.workbench-links a').allTextContents(),['MQTT Monitor','Asset Cards','Digital Twins','Config Studio']);
    assert.equal(await page.locator('.workbench-links [aria-current=page]').textContent(),'Digital Twins');
    await page.waitForFunction(()=>{const image=document.querySelector('.twin-preview img');return image.complete&&image.naturalWidth>0;});
    assert.equal(sockets.length,0,'Selecting a twin must be required before connecting to MQTT');
    for(const width of [1440,390]) {
      await page.setViewportSize({width,height:width===390?844:1000});
      await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
      await page.screenshot({path:path.join(output,`digital-twins-${width}.png`),fullPage:true});
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),`Catalog horizontal overflow at ${width}px`);
    }
    await page.setViewportSize({width:1440,height:1100});
    await page.getByRole('link',{name:'Explore Brewery GE01 demo',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#conn-text')?.textContent==='Demo · simulated');
    assert.equal(sockets.length,0,'Catalog demo selection opened an MQTT connection');
    assert.equal(await page.locator('.workbench-links [aria-current=page]').textContent(),'Digital Twins');
    await page.getByRole('link',{name:'All digital twins'}).click();await page.waitForSelector('[data-twin=brewery-ge01]');
    await page.getByRole('link',{name:'Open Brewery GE01',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#conn-text')?.textContent==='Broker connected');
    assert.equal(await page.locator('#kpi-mash').textContent(),'—','Live mode must not display profile sample values');
    assert.equal(await page.locator('.equipment-card').count(),8);await page.waitForSelector('#canvas-host canvas');
    assert(await page.locator('#scene-loading').evaluate(n=>n.hidden),'3D rendering failed');
    assert(snapshots>=1,'Snapshot was not requested after subscribing');
    const twinId=()=>Object.keys(broker.clients).find(id=>id.startsWith('brewery-twin-'));
    const twin=broker.clients[twinId()];assert(twin&&Object.keys(twin.subscriptions).length>=5,'Telemetry subscriptions absent');
    await send('factory/device/status','online');
    for(const group of ['machines','tanks'])for(const asset of profile[group]) {
      if(group==='machines'){await send(`factory/${group}/${asset.id}/status`,asset.running?'RUNNING':'STOPPED');await send(`factory/${group}/${asset.id}/pulses`,asset.pulses);}
      for(const metric of asset.metrics)await send(`factory/${group}/${asset.id}/${metric.key}`,metric.init_val);
    }
    await page.waitForFunction(()=>document.querySelector('#kpi-mash').textContent==='66.5'&&document.querySelector('#conn-text')?.textContent==='Live rig');
    assert.equal(await page.locator('#kpi-running').textContent(),'4');assert.equal(await page.locator('#kpi-bright').textContent(),'81');assert.equal(await page.locator('#kpi-fill').textContent(),'220');
    // Exercise visible machinery, not just numeric readings. Both the SVG
    // activity view and 3D scene use the same time-integrated motion phases.
    await page.locator('.equipment-card[data-asset=fill01]').click();
    assert.equal(await page.locator('.activity-reading strong').textContent(),'220');
    assert((await page.locator('.asset-label[data-asset=fill01] b').textContent()).includes('bottles/min'));
    const bottle=page.locator('#activity-panel [data-part=bottle]').first();
    const readTravel=()=>bottle.evaluate(n=>({x:Number(n.getAttribute('transform').match(/translate\(([-.\d]+)/)[1]),at:performance.now()}));
    const waitFrames=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    async function speed(rate){
      await send('factory/machines/fill01/bottleCnt',rate);await page.waitForFunction(r=>document.querySelector('.activity-reading strong').textContent===String(r),rate);
      const a=await readTravel();await new Promise(resolve=>setTimeout(resolve,400));const b=await readTravel();return ((b.x-a.x+300)%300)/(b.at-a.at)*1000;
    }
    const slow=await speed(60),fast=await speed(300);assert(slow>0&&fast>slow*2,`Conveyor speed did not follow MQTT rate: ${slow}, ${fast}`);
    await send('factory/machines/fill01/bottleCnt',0);await page.waitForFunction(()=>document.querySelector('.activity-reading strong').textContent==='0');await waitFrames();
    const zero=await bottle.getAttribute('transform');await new Promise(resolve=>setTimeout(resolve,150));assert.equal(await bottle.getAttribute('transform'),zero,'Zero-rate conveyor kept moving');
    await send('factory/machines/fill01/bottleCnt',220);await send('factory/machines/fill01/flowRate',1.3);
    await page.waitForFunction(()=>document.querySelector('.activity-reading strong').textContent==='220');
    await page.locator('#view-motion').click();await waitFrames();const paused=await bottle.getAttribute('transform');await new Promise(resolve=>setTimeout(resolve,150));assert.equal(await bottle.getAttribute('transform'),paused,'Motion toggle did not pause the conveyor');
    await page.locator('#view-motion').click();await page.waitForFunction(before=>document.querySelector('[data-part=bottle]').getAttribute('transform')!==before,paused);
    await page.locator('#view-focus').click();assert.equal(await page.locator('#view-focus').getAttribute('aria-pressed'),'true');
    await page.locator('#view-motion').click();await waitFrames();
    await page.locator('.twin-workspace').screenshot({path:path.join(output,'filler-focus.png')});
    await page.setViewportSize({width:390,height:844});await waitFrames();
    await page.screenshot({path:path.join(output,'filler-mobile.png'),fullPage:true});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),'Focused filler overflows the mobile viewport');
    await page.setViewportSize({width:1440,height:1100});await page.locator('#view-motion').click();await page.locator('#view-reset').click();
    await page.locator('.equipment-card[data-asset=mash01]').click();
    const canvas=await page.locator('canvas').elementHandle(),card=await page.locator('[data-metric=mashTemp]').elementHandle();
    for(let i=0;i<2000;i++)publisher.publish('factory/machines/mash01/mashTemp',String(60+i/100),{qos:0});
    await page.waitForFunction(()=>document.querySelector('#kpi-mash').textContent==='79.99'||document.querySelector('#kpi-mash').textContent==='80');
    assert(await page.evaluate(n=>n.isConnected,card),'Metric card replaced during telemetry burst');assert(await page.evaluate(n=>n.isConnected,canvas),'3D canvas replaced during burst');
    await send('factory/machines/mash01/mashTemp','66.8');await page.waitForFunction(()=>document.querySelector('#kpi-mash').textContent==='66.8');
    await page.locator('.equipment-card[data-asset=bright]').click();await page.waitForFunction(()=>document.querySelector('#asset-name').textContent==='Bright Beer');
    await send('factory/tanks/bright/level','8');await page.waitForFunction(()=>document.querySelector('#asset-status').textContent==='Threshold');
    await send('factory/tanks/bright/level','81');
    await page.locator('.equipment-card[data-asset=clean01]').click();assert.equal(await page.locator('#asset-status').textContent(),'Stopped');
    await page.locator('.equipment-card[data-asset=mash01]').click();
    await send('factory/operators/login',JSON.stringify({name:'<img src=x onerror="window.injected=true">',role:'Test operator',ts:5}));
    await page.waitForFunction(()=>document.querySelector('#operator-name').textContent.startsWith('<img'));
    assert.equal(await page.evaluate(()=>window.injected),undefined);assert.equal(await page.locator('#operator-name img').count(),0);
    await send('factory/operators/login',JSON.stringify({name:'Franz Huber',role:'Braumeister'}));
    await send('factory/device/status','offline');await page.waitForFunction(()=>document.querySelector('#conn-text')?.textContent==='Rig offline');
    assert.equal(await page.locator('#asset-status').textContent(),'Last known');assert.equal(await page.locator('#kpi-mash').textContent(),'66.8');
    await send('factory/device/status','online');
    const before=snapshots;broker.clients[twinId()].conn.destroy();
    await page.waitForFunction(()=>document.querySelector('#asset-status').textContent==='Last known');
    await new Promise((resolve,reject)=>{const started=Date.now(),interval=setInterval(()=>{if(snapshots>before){clearInterval(interval);resolve();}else if(Date.now()-started>10000){clearInterval(interval);reject(Error('Reconnect snapshot missing'));}},50);});
    assert.equal(await page.locator('[data-metric=mashTemp] .metric-top span:last-child').textContent(),'LAST KNOWN','Old fields became live without a fresh snapshot');
    await send('factory/device/status','online');await send('factory/machines/mash01/status','RUNNING');await send('factory/machines/mash01/mashTemp','66.8');
    await page.waitForFunction(()=>document.querySelector('#asset-status').textContent==='Running');
    await page.locator('#view-labels').click();await page.waitForFunction(()=>document.querySelector('.asset-label').hidden);await page.locator('#view-labels').click();
    await page.locator('#view-top').click();assert.equal(await page.locator('#view-top').getAttribute('aria-pressed'),'true');await page.locator('#view-reset').click();
    await page.locator('#demo-mode').click();await page.waitForFunction(()=>document.querySelector('#conn-text')?.textContent==='Demo · simulated');
    assert.equal(await page.locator('#kpi-running').textContent(),'4');
    // Chromium can tile animated WebGL incorrectly during full-page capture.
    await page.emulateMedia({reducedMotion:'reduce'});
    if(await page.locator('#view-motion').getAttribute('aria-pressed')==='true')await page.locator('#view-motion').click();
    await page.screenshot({path:path.join(output,'brewery-desktop.png'),fullPage:true});
    await page.locator('#connection-open').click();await page.locator('#broker-url').fill('https://example.com');await page.locator('#connection-form button[type=submit]').click();
    assert((await page.locator('#connection-error').textContent()).includes('ws://'));await page.locator('#connection-close').click();
    for(const width of [1024,768,390]) {
      await page.setViewportSize({width,height:width===390?844:1100});
      await page.evaluate(()=>new Promise(requestAnimationFrame));
      await page.screenshot({path:path.join(output,`brewery-${width}.png`),fullPage:true});
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),`Horizontal overflow at ${width}px`);
    }
    await page.locator('#live-mode').click();await page.waitForFunction(()=>document.querySelector('#conn-text')?.textContent==='Broker connected');
    assert.equal(await page.locator('#kpi-mash').textContent(),'—','Demo data leaked into live mode');
    await page.getByRole('link',{name:'All digital twins'}).click();await page.waitForSelector('[data-twin=brewery-ge01]');
    await new Promise((resolve,reject)=>{const start=Date.now(),interval=setInterval(()=>{if(!twinId()){clearInterval(interval);resolve();}else if(Date.now()-start>5000){clearInterval(interval);reject(Error('Returning to the catalog leaked the twin MQTT connection'));}},25);});
    assert.deepEqual(errors,[],'Browser errors');
    console.log(JSON.stringify({result:'PASS',checks:['catalog live/demo selection','offline catalog','return navigation and connection cleanup','catalog preview and responsive layouts','eight assets','real WebGL scene','unknown startup','MQTT mapping','rate-driven conveyor motion','zero-speed stop','motion toggle','equipment focus','2000-message burst without replacing canvas/cards','thresholds','operator escaping','offline state','reconnect snapshot and per-field freshness','demo isolation','connection validation','desktop/tablet/mobile'],conveyor:{slow_pixels_per_s:slow,fast_pixels_per_s:fast},snapshots,output},null,2));
  }finally{if(publisher)await publisher.endAsync(true);if(browser)await browser.close();for(const socket of ws.clients)socket.terminate();await new Promise(resolve=>ws.close(resolve));await new Promise(resolve=>server.close(resolve));await new Promise(resolve=>broker.close(resolve));}
})().catch(error=>{console.error(error);process.exit(1);});
