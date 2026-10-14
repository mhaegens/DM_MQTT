/* Run against a disposable MQTT + HTTP server, never a production broker.
 * DM_QA_URL defaults to http://127.0.0.1:8787; DM_QA_MODULES can point to a
 * temporary node_modules directory. Chrome executable can be overridden. */
const path = require('node:path');
const fs = require('node:fs');
const assert = require('node:assert/strict');
const dependency = name => require(process.env.DM_QA_MODULES ? path.join(process.env.DM_QA_MODULES,name) : name);
const {chromium} = dependency('playwright');
const mqtt = dependency('mqtt');
const base = process.env.DM_QA_URL || 'http://127.0.0.1:8787';
const output = process.env.DM_QA_OUTPUT || '/tmp/dm-mqtt-browser-qa';
(async () => {
 fs.mkdirSync(output,{recursive:true});
 const browser = await chromium.launch({headless:true,executablePath:process.env.DM_QA_CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
 const context = await browser.newContext({viewport:{width:1440,height:1000}});
 // Production defaults to public WSS. This suite explicitly selects its
 // disposable local broker before any page scripts can connect/publish.
 await context.addInitScript(url=>{
   try { localStorage.setItem('sapdm-broker-url',url); } catch (_) {}
 }, base.replace(/^http/, 'ws')+'/mqtt');
 const errors=[]; context.on('page',p=>p.on('pageerror',e=>errors.push(e.message)));
 const monitor=await context.newPage(); await monitor.goto(base+'/dashboard/');
 await monitor.waitForFunction(()=>document.getElementById('status-label').textContent==='Connected');
 const pub=mqtt.connect(base.replace('http:','ws:')+'/mqtt'); await new Promise(resolve=>pub.on('connect',resolve));
 const send=(topic,payload)=>new Promise((resolve,reject)=>pub.publish(topic,String(payload),{qos:1},err=>err?reject(err):resolve()));
 await send('factory/device/status','online');
 await send('factory/device/performance','{"rtt_ms":24}');
 const floor=await context.newPage(); await floor.goto(base+'/shopfloor/');
 await floor.waitForFunction(()=>client?.connected);
 await send('factory/machines/cnc01/status','RUNNING');
 await send('factory/machines/cnc01/rpm','4850');
 await send('factory/tanks/raw/level','68');
 await send('factory/tanks/raw/tankTemp','22.4');
 await send('factory/operators/login',JSON.stringify({name:'Test Operator',role:'Shift Lead',plant:'Plant BE01',uid:'A3:F7:C2'}));
 await floor.waitForFunction(()=>document.getElementById('mv-cnc01-rpm')?.textContent==='4,850'||document.getElementById('mv-cnc01-rpm')?.textContent==='4850');
 await floor.waitForFunction(()=>document.querySelector('.opname')?.textContent==='Test Operator');
 const svg=await floor.locator('#tsvg-raw').elementHandle();
 await send('factory/tanks/raw/level','75');
 assert(await floor.evaluate(old=>old===document.getElementById('tsvg-raw'),svg),'Tank level update replaced SVG');
 await monitor.waitForFunction(()=>document.querySelectorAll('#topic-tbody tr').length>=6);
 const topicRow=await monitor.locator('#topic-tbody tr').filter({hasText:'factory/machines/cnc01/rpm'}).elementHandle();
 const begin=Date.now();
 for(let i=0;i<2000;i++) pub.publish('factory/machines/cnc01/rpm',String(5000+i),{qos:0});
 await monitor.waitForFunction(()=>document.querySelector('#topic-tbody tr td.td-topic')&&[...document.querySelectorAll('#topic-tbody tr')].some(row=>row.children[0].textContent==='factory/machines/cnc01/rpm'&&row.children[1].textContent==='6999'));
 const burst_ms=Date.now()-begin;
 assert(await monitor.evaluate(old=>old.isConnected,topicRow),'Existing topic row was destroyed during burst');
 assert(await monitor.locator('.feed-row').count()<=100,'Feed unbounded');
 await monitor.locator('#probe-btn').click();
 await monitor.waitForFunction(()=>document.getElementById('probe-result').textContent.includes('p95'),null,{timeout:15000});
 const roundtrip=await monitor.locator('#probe-result').textContent();
 await monitor.locator('#pub-topic').fill('factory/test'); await monitor.locator('#pub-payload').fill('publish check');
 await monitor.locator('#pub-btn').click();
 await monitor.waitForFunction(()=>[...document.querySelectorAll('#topic-tbody tr')].some(row=>row.children[0].textContent==='factory/test'&&row.children[1].textContent==='publish check'));
 await send('factory/operators/login',JSON.stringify({name:'<img src=x onerror="window.injected=true">',role:'Operator'}));
 await floor.waitForFunction(()=>document.querySelector('.opname')?.textContent.includes('<img'));
 assert.equal(await floor.evaluate(()=>window.injected),undefined);
 const studio=await context.newPage(); await studio.goto(base+'/config-editor/');
 const deviceBox=await studio.locator('#device').boundingBox(), viewportBox=await studio.locator('#viewport').boundingBox();
 assert(deviceBox.y+deviceBox.height<=viewportBox.y+viewportBox.height+1,'Fit preview clips device footer');
 await studio.locator('[data-a=json]').click();
 await studio.locator('#jsontext').fill('{"machines":{}}'); await studio.locator('[data-a=jsonapply]').click();
 assert((await studio.locator('#jsonerr').textContent()).length>0,'Malformed config accepted');
 await studio.locator('[data-a=jsonclose]').click();
 await studio.locator('[data-a=download]').click();
 await send('factory/operators/login',JSON.stringify({name:'Test Operator',role:'Shift Lead',plant:'Plant BE01'}));
 const editor=await context.newPage(); await editor.goto(base+'/editor/');
 await editor.getByRole('button',{name:'Raw JSON',exact:true}).click();
 await editor.locator('#jsonRaw').fill('{"machines":{}}');
 await editor.getByRole('button',{name:'Load from JSON'}).click();
 assert((await editor.locator('#jsonValidation').textContent()).includes('Invalid configuration'));
 await editor.getByRole('button',{name:'Machines',exact:true}).click();
 await editor.getByRole('button',{name:'Load Sample Config'}).click();
 for(const [name,page] of [['monitor',monitor],['shopfloor',floor],['studio',studio],['editor',editor]]) {
   const logo=await page.locator('.workbench-logo').evaluate(img=>img.complete&&img.naturalWidth>0);
   assert(logo,`${name} official SAP SVG failed to load`);
   await page.screenshot({path:path.join(output,name+'-desktop.png'),fullPage:true});
   await page.setViewportSize({width:390,height:844});
   await page.screenshot({path:path.join(output,name+'-mobile.png'),fullPage:true});
   const overflow=await page.evaluate(()=>({width:document.documentElement.scrollWidth,viewport:innerWidth}));
   assert(overflow.width<=overflow.viewport+2,`${name} horizontal overflow ${JSON.stringify(overflow)}`);
 }
 assert.deepEqual(errors,[],'Browser errors');
 console.log(JSON.stringify({result:'PASS',burst:2000,burst_ms,roundtrip,checks:['topic row reuse','bounded feed','publish','tank SVG reuse','operator text escaping','invalid configuration rejection','all four screens at 1440px and 390px'],screenshots:output},null,2));
 await pub.endAsync(); await browser.close();
})().catch(err=>{console.error(err);process.exit(1);});
