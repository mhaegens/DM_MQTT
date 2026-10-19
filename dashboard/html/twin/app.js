const $ = id => document.getElementById(id);
const {Store,severity,demoTick} = window.BreweryTwin;
const {Motion,primaryMetric}=window.BreweryMotion;
const text = (node,value) => {if(node.textContent!==String(value))node.textContent=String(value);};
const fmt = value => value == null ? '—' : Number(value).toLocaleString(undefined,{maximumFractionDigits:2});
const clock = at => new Date(at).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit'});
const descriptions = {
  mash01:'Warm water meets malt. Temperature and agitation shape the mash.',
  boil01:'The wort kettle brings the brew to a boil. Follow heat, steam pressure and progress.',
  ferm01:'Yeast turns wort into beer. Follow temperature, CO₂ activity and head pressure.',
  fill01:'Bright beer reaches the bottling line. Follow filling rate, flow and foam loss.',
  clean01:'The cleaning skid supports the brewhouse. Follow cleaner temperature and caustic concentration.',
  malt:'Malt storage supplies the brewhouse. The visible volume represents the measured fill level.',
  bright:'Finished beer waits for packaging. Follow storage level, serving temperature and head pressure.',
  spent:'Spent grain leaves the mash process. Follow the measured collection level.'
};
const order = ['malt','mash01','boil01','ferm01','bright','fill01','clean01','spent'];
const states = {running:'Running',stopped:'Stopped',waiting:'Waiting',stale:'Last known',ok:'Available',warn:'Attention',err:'Threshold'};
let profile,store,view,selected='mash01',client=null,epoch=0,subscribed=false,connectionError='',demoStart=0;
let timer,raf,lastFrame=0,lastDemo=0,eventSignature='',operatorSignature='',disposed=false;
let motion=new Motion(),activity=null,motionChoice=null;
const reducedMotion=matchMedia('(prefers-reduced-motion: reduce)');
try{const saved=localStorage.getItem('sapdm-twin-motion');if(saved==='on'||saved==='off')motionChoice=saved==='on';}catch(_){}
const animationEnabled=()=>motionChoice??!reducedMotion.matches;
const metricNodes=new Map(),equipmentNodes=new Map();
function el(tag,cls,value) {const node=document.createElement(tag);if(cls)node.className=cls;if(value!==undefined)node.textContent=value;return node;}
function svgEl(tag,attrs={}){const node=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [key,value] of Object.entries(attrs))node.setAttribute(key,value);return node;}
function buildActivity(asset){
  const panel=$('activity-panel');panel.replaceChildren();activity=null;panel.hidden=asset.group!=='machines';if(panel.hidden)return;
  const heading=el('div','activity-heading'),title=el('span','','PROCESS ACTIVITY'),state=el('span');heading.append(title,state);
  const metric=primaryMetric(asset),line=el('div','activity-reading'),value=el('strong'),unit=el('small','',metric.key==='bottleCnt'?'bottles/min':metric.unit);line.append(value,unit);
  const svg=svgEl('svg',{viewBox:'0 0 280 82',role:'img','aria-label':`${asset.type} activity`});
  let bottles=[],marks=[],rotor,gas=[],steam=[],progress,jet;
  if(asset.id==='fill01'){
    svg.innerHTML='<rect x="8" y="58" width="264" height="11" rx="5" fill="#d3e3da"/><path d="M14 72h252M36 72v7m208-7v7" stroke="#739a87" stroke-width="3"/><path d="M115 5h50v8h-50zM124 13v10m32-10v10" fill="#aacbbb" stroke="#6d9c83" stroke-width="2"/>';
    for(let i=0;i<12;i++){
      const mark=svgEl('rect',{width:3,height:6,y:60,fill:i%3===0?'#bd8a32':'#81a58e'});svg.append(mark);marks.push(mark);
      const bottle=svgEl('g',{'data-part':'bottle'});
      bottle.innerHTML=`<path d="M6 2h4v11l3 5v24H3V18l3-5z" fill="#9c732e" stroke="#7e642c" stroke-width="1"/><rect x="3" y="26" width="10" height="9" fill="${i%3===0?'#e6b84e':'#f5e8bf'}"/><path d="M5 2h6" stroke="#d3b454" stroke-width="3"/>`;
      svg.append(bottle);bottles.push(bottle);
    }
    jet=svgEl('path',{d:'M125 24v16m30-16v16',stroke:'#dfb14e','stroke-width':3,'stroke-dasharray':'4 3','data-part':'liquid-flow'});svg.append(jet);
  }else if(asset.id==='mash01'){
    svg.innerHTML='<circle cx="140" cy="42" r="32" fill="#e4eee7" stroke="#a7c2b0" stroke-width="2"/><circle cx="140" cy="42" r="25" fill="#f0dfbb"/>';
    rotor=svgEl('g',{'data-part':'rotor'});rotor.innerHTML='<path d="M120 42h40m-20-20v40" stroke="#6c957c" stroke-width="8" stroke-linecap="round"/><circle cx="140" cy="42" r="6" fill="#b78b45"/>';svg.append(rotor);
  }else if(asset.id==='boil01'){
    svg.innerHTML='<circle cx="49" cy="43" r="27" fill="none" stroke="#dfe7dd" stroke-width="7"/><path d="M137 37h89v29q-44 15-89 0zm-7-1q51-22 103 0z" fill="#c39859" stroke="#a7824e" stroke-width="2"/>';
    progress=svgEl('circle',{cx:49,cy:43,r:27,fill:'none',stroke:'#c89836','stroke-width':7,'stroke-dasharray':Math.PI*54,transform:'rotate(-90 49 43)'});svg.append(progress);
    for(const x of [155,181,208]){const s=svgEl('path',{d:`M${x} 26q-7-7 0-14t0-12`,fill:'none',stroke:'#adbdaf','stroke-width':3,'stroke-linecap':'round'});svg.append(s);steam.push(s);}
  }else if(asset.id==='ferm01'){
    svg.innerHTML='<path d="M104 12q35-13 70 0v42l-35 24-35-24z" fill="#d0dfd7" stroke="#9ab6a5" stroke-width="2"/><rect x="124" y="17" width="30" height="40" rx="8" fill="#eaf5ee"/><path d="M109 72v8m60-8v8" stroke="#85a58f" stroke-width="3"/>';
    for(let i=0;i<5;i++){const bubble=svgEl('circle',{cx:132+(i%3)*7,r:2+i%2,fill:'#65a6aa'});svg.append(bubble);gas.push(bubble);}
  }else{
    svg.innerHTML='<path d="M20 44h65m53 0h123v22H20" fill="none" stroke="#96b9c7" stroke-width="6"/><circle cx="111" cy="44" r="26" fill="#ddebf0" stroke="#85aaba" stroke-width="3"/>';
    rotor=svgEl('g',{'data-part':'rotor'});rotor.innerHTML='<path d="M93 44h36m-18-18v36" stroke="#6894a8" stroke-width="7" stroke-linecap="round"/>';svg.append(rotor);unit.textContent='';
  }
  const details=el('p','activity-details');panel.append(heading,line,svg,details);
  activity={id:asset.id,metric,value,unit,state,svg,bottles,marks,rotor,gas,steam,progress,jet,details};
}
function paintActivity(asset,now){
  if(!activity)return;
  const metric=activity.metric,sample=asset.values.get(metric.key),status=store.status(asset,now),current=store.current(sample,now);
  text(activity.value,asset.id==='clean01'?states[status]:sample?fmt(sample.value):'—');
  text(activity.state,!animationEnabled()?'Motion paused':status==='running'?'Running':states[status]);
  activity.value.classList.toggle('muted',Boolean(sample)&&!current);
  const keys={fill01:['flowRate','foamLoss'],mash01:['mashTemp','gravity'],boil01:['boilTemp','steamPres'],ferm01:['fermTemp','pressure'],clean01:['cleanTemp','causticConc']}[asset.id];
  text(activity.details,keys.map(key=>{const m=asset.metrics.find(m=>m.key===key),s=asset.values.get(key);return `${m.label} ${s?fmt(s.value):'—'} ${m.unit}`;}).join(' · '));
}
function renderActivity(){
  if(!activity)return;
  const f=motion.get(activity.id);if(!f)return;
  for(const [i,bottle] of activity.bottles.entries())bottle.setAttribute('transform',`translate(${-14+((f.bottles+i)%12)*25} 12)`);
  for(const [i,mark] of activity.marks.entries())mark.setAttribute('x',8+((f.bottles+i)%12)*22);
  if(activity.rotor){const mash=activity.id==='mash01';activity.rotor.setAttribute('transform',`rotate(${(mash?f.stirrer:f.pump)*180/Math.PI} ${mash?140:111} ${mash?42:44})`);}
  for(const [i,bubble] of activity.gas.entries()){bubble.setAttribute('cy',55-((f.gas+i/5)%1)*35);bubble.setAttribute('opacity',f.running&&f.co2Rate>0?1:0);}
  for(const [i,steam] of activity.steam.entries()){steam.setAttribute('transform',`translate(0 ${-((f.steam+i/3)%1)*5})`);steam.setAttribute('opacity',f.boiling?1:0);}
  if(activity.progress){const asset=store.assets.get(activity.id),s=asset.values.get('boilProg'),p=Math.max(0,Math.min(100,s?.value||0));activity.progress.setAttribute('stroke-dashoffset',Math.PI*54*(1-p/100));}
  if(activity.jet){activity.jet.setAttribute('opacity',f.running&&f.flowRate>0?1:0);activity.jet.setAttribute('stroke-width',1+Math.min(f.flowRate,30)/10);activity.jet.setAttribute('stroke-dashoffset',-f.liquid*50);}
}
function syncMotion(){const on=animationEnabled();text($('view-motion'),on?'Motion on':'Motion off');$('view-motion').setAttribute('aria-pressed',String(on));$('view-motion').title=on?'Pause process animations':'Enable process animations';}
function setSelected(id) {
  selected=id;const asset=store.assets.get(id);
  if($('view-focus').getAttribute('aria-pressed')==='true')view?.focus(id);
  text($('asset-id'),asset.name);text($('asset-name'),asset.type||asset.name);
  text($('selected-number'),`${String(order.indexOf(id)+1).padStart(2,'0')} / 08`);
  text($('asset-description'),descriptions[id]);metricNodes.clear();$('metric-list').replaceChildren();
  buildActivity(asset);
  for(const metric of asset.metrics) {
    const card=el('article','metric-card');card.dataset.metric=metric.key;
    const top=el('div','metric-top'), label=el('span','',metric.label),quality=el('span','','Waiting');top.append(label,quality);
    const line=el('div','metric-value'),value=el('strong','','—'),unit=el('small','',metric.unit);line.append(value,unit);
    const range=el('div','metric-range'),bar=el('span');range.append(bar);range.setAttribute('aria-hidden','true');
    const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');svg.setAttribute('viewBox','0 0 80 30');svg.setAttribute('aria-label',`${metric.label}: recent observations`);svg.setAttribute('role','img');
    const chart=document.createElementNS(svg.namespaceURI,'polyline');svg.append(chart);card.append(top,line,range,svg);$('metric-list').append(card);
    metricNodes.set(metric.key,{card,value,quality,bar,chart});
  }
  $('pulse-row').hidden=asset.group!=='machines';
  for(const [key,n] of equipmentNodes)n.button.setAttribute('aria-pressed',String(key===id));
  paint();
}
function buildEquipment() {
  for(const [index,id] of order.entries()) {
    const asset=store.assets.get(id),button=el('button','equipment-card');button.dataset.asset=id;
    button.setAttribute('aria-label',`Inspect ${asset.name}`);button.setAttribute('aria-pressed',String(id===selected));
    const top=el('div','equipment-top'),number=el('span','',`${String(index+1).padStart(2,'0')} / ${asset.group==='machines'?'PROCESS':'STORAGE'}`),dot=el('i');top.append(number,dot);
    const metric=primaryMetric(asset),name=el('h3','',asset.name),type=el('span','',asset.type||'Storage vessel'),reading=el('div','equipment-reading'),value=el('span','','—'),unit=el('small','',metric.key==='bottleCnt'?'bottles/min':metric.unit);
    reading.append(value,unit);button.append(top,name,type,reading);button.addEventListener('click',()=>setSelected(id));$('equipment-list').append(button);
    equipmentNodes.set(id,{button,value,dot});
  }
}
function stopClient() {
  epoch++;subscribed=false;
  if(client){client.removeAllListeners();client.on('error',()=>{});client.end(true);client=null;}
}
function snapshot() {
  if(store.demo||!client?.connected||!subscribed)return;
  const current=client;
  current.publish('factory/device/request_snapshot','state',{qos:0,retain:false},error=>{
    if(current!==client)return;
    if(error)store.event('Snapshot request failed · check the connection','warn');
    else store.event('Fresh readings requested','info');
  });
}
function connect() {
  stopClient();store.connection(false);connectionError='';const myEpoch=epoch;
  try {const suffix=globalThis.crypto?.randomUUID?.().slice(0,12)||Math.random().toString(16).slice(2);client=mqtt.connect(window.dmMqtt.url,{...window.dmMqtt.options,clientId:`brewery-twin-${suffix}`});}
  catch(error){connectionError=error.message;paint();return;}
  const current=client;
  current.on('connect',()=>{
    if(myEpoch!==epoch)return;
    connectionError='';store.connection(true);store.event('Broker connected · synchronising brewery','info');
    current.subscribe(['factory/machines/+/#','factory/tanks/+/#','factory/operators/login','factory/device/status','factory/device/performance'],{qos:0},(error,grants)=>{
      if(myEpoch!==epoch||!current.connected)return;
      if(error||grants?.some(g=>g.qos===128)){connectionError='Subscription refused';store.event('Broker refused the telemetry subscription','err');store.connection(false);paint();return;}
      subscribed=true;snapshot();paint();
    });
  });
  current.on('message',(topic,payload,packet)=>{
    if(myEpoch!==epoch)return;
    const wasOffline=store.availability==='offline',value=payload.toString();
    store.ingest(topic,value,Date.now(),packet.retain);
    if(wasOffline&&topic==='factory/device/status'&&value.trim()==='online')snapshot();
  });
  const offline=()=>{if(myEpoch!==epoch)return;subscribed=false;if(store.connected)store.event('Broker disconnected · readings held','warn');store.connection(false);paint();};
  current.on('close',offline);current.on('offline',offline);
  current.on('error',error=>{if(myEpoch===epoch){connectionError=error.message||'Connection failed';paint();}});
  paint();
}
function mode(demo) {
  if(store.demo===demo)return;
  stopClient();store=new Store(profile,{demo});motion=new Motion();connectionError='';eventSignature='';operatorSignature='';
  if(demo){demoStart=performance.now();lastDemo=0;demoTick(store,0);const person=profile.people[0];store.ingest('factory/operators/login',JSON.stringify({name:person.name,role:person.role}));store.event('Demo mode · local simulation, no rig connection','info');}
  else connect();
  $('live-mode').setAttribute('aria-pressed',String(!demo));$('demo-mode').setAttribute('aria-pressed',String(demo));
  setSelected(selected);paint();
}
function paint() {
  if(!store)return;
  const now=Date.now(),demo=store.demo;
  const machines=[...store.assets.values()].filter(a=>a.group==='machines');
  const fresh=machines.filter(a=>store.current(a.status,now)), running=fresh.filter(a=>a.status.value==='RUNNING').length;
  text($('kpi-running'),fresh.length?running:'—');text($('running-note'),demo?'Simulated machine states':fresh.length?`${fresh.length} of 5 machine states received`:'Waiting for machine states');
  function kpi(assetId,key,valueId,noteId,name) {
    const sample=store.assets.get(assetId).values.get(key),q=store.quality(sample,now);
    text($(valueId),sample?fmt(sample.value):'—');$(valueId).classList.toggle('muted',Boolean(sample)&&!store.current(sample,now));
    text($(noteId),demo?`${name} · simulated`:q==='live'?`${name} · observed`:sample?`${name} · last known`:`${name} · awaiting signal`);
  }
  kpi('mash01','mashTemp','kpi-mash','mash-note','MASH-01');kpi('bright','level','kpi-bright','bright-note','Storage');kpi('fill01','bottleCnt','kpi-fill','fill-note','FILL-01');
  let connection='Connecting…',badge='',scene='Waiting for rig';
  const hasCurrent=[...store.assets.values()].some(a=>[...a.values.values()].some(s=>store.current(s,now)));
  if(demo){connection='Demo · simulated';badge='demo';scene='Demo mode · simulated readings';}
  else if(connectionError){connection='Connection error';badge='error';scene='Connection needs attention';}
  else if(!store.connected){connection=store.received?'Reconnecting…':'Connecting…';scene=store.received?'Connection lost · showing last known readings':'Connecting to broker';}
  else if(store.availability==='offline'){connection='Rig offline';badge='error';scene='Rig offline · showing last known readings';}
  else if(hasCurrent){connection='Live rig';badge='live';scene='Live MQTT · brewery signals received';}
  else if(store.received){connection='No recent rig signal';badge='error';scene='Rig silent · showing last known readings';}
  else {connection='Broker connected';scene='Waiting for brewery signals · activate the brewery profile';}
  $('conn-badge').className=`connection-badge ${badge}`;text($('conn-text'),connection);
  $('scene-data-badge').className=`scene-data-badge ${badge}`;text($('scene-data-badge').querySelector('span'),scene);
  const asset=store.assets.get(selected),status=store.status(asset,now);
  paintActivity(asset,now);
  text($('asset-status'),states[status]);$('asset-status').className=`state-pill ${status}`;
  for(const metric of asset.metrics) {
    const node=metricNodes.get(metric.key),sample=asset.values.get(metric.key),q=store.quality(sample,now);
    text(node.value,sample?fmt(sample.value):'—');text(node.quality,q==='demo'?'SIMULATED':q==='live'?'OBSERVED':q==='waiting'?'WAITING':'LAST KNOWN');
    node.card.className=`metric-card ${sample?severity(metric,sample.value):''}`;node.value.classList.toggle('muted',Boolean(sample)&&!store.current(sample,now));
    node.bar.style.width=`${sample?Math.max(0,Math.min(100,(sample.value-metric.min_val)/(metric.max_val-metric.min_val)*100)):0}%`;
    const samples=sample?.history||[],values=samples.map(s=>s.value),lo=Math.min(...values),hi=Math.max(...values),start=samples[0]?.at||0,end=samples.at(-1)?.at||0;
    node.chart.setAttribute('points',samples.map(s=>`${end===start?40:(s.at-start)/(end-start)*80},${hi===lo?15:27-(s.value-lo)/(hi-lo)*24}`).join(' '));
    node.card.title=sample?`${metric.label}: ${sample.value} ${metric.unit}. Received ${clock(sample.at)}. ${q==='live'?'Change-only telemetry: unchanged values stay valid while rig signals continue.':'Data quality: '+q}.`:`Awaiting factory/${asset.group}/${asset.id}/${metric.key}`;
  }
  text($('asset-pulses'),asset.pulses?fmt(asset.pulses.value):'—');$('asset-pulses').classList.toggle('muted',Boolean(asset.pulses)&&!store.current(asset.pulses,now));
  const note=demo?'Local simulation uses the standard brewery profile. Values are illustrative.':status==='waiting'?'Waiting for this asset. Activate Brewery GE01 on the rig and check the broker address.':status==='stale'?'Last known readings. Activity is paused until the rig sends fresh signals.':'Observed values update when the rig changes them. Open Connection to request a fresh snapshot.';
  text($('inspector-note'),note);
  for(const [id,node] of equipmentNodes) {
    const a=store.assets.get(id),s=a.values.get(primaryMetric(a).key);node.dot.className=store.status(a,now);text(node.value,s?fmt(s.value):'—');node.value.classList.toggle('muted',Boolean(s)&&!store.current(s,now));
  }
  text($('signal-count'),`${store.received.toLocaleString()} ${demo?'simulated':'MQTT'} readings`);
  const signature=JSON.stringify(store.events);
  if(eventSignature!==signature) {
    eventSignature=signature;
    const rows=store.events.slice(0,10).map(event=>{const row=el('div',`signal-event ${event.kind}`),dot=el('i'),message=el('span','',event.message),time=el('time','',clock(event.at));time.dateTime=new Date(event.at).toISOString();row.append(dot,message,time);return row;});
    $('signal-events').replaceChildren(...(rows.length?rows:[el('p','empty-message','Waiting for the brewery’s first signals.')]));
  }
  const operator=store.operator,opSignature=JSON.stringify([operator,demo,store.generation,hasCurrent]);
  if(operatorSignature!==opSignature) {
    operatorSignature=opSignature;text($('operator-name'),operator?.name||'No badge received');text($('operator-role'),operator?.role||'Tap an NFC badge on the rig');
    text($('operator-avatar'),operator?operator.name.split(/\s+/).slice(0,2).map(s=>s[0]).join(''):'—');
    text($('operator-time'),operator?`${demo?'Demo operator':'Last badge received'} · ${clock(operator.at)}${!demo&&(!hasCurrent||operator.generation!==store.generation)?' · previous observation':''}`:'Badge events appear here when received.');
  }
  $('snapshot-request').disabled=demo||!client?.connected||!subscribed;
}
async function init() {
  const response=await fetch('./brewery.json',{cache:'no-cache'});if(!response.ok)throw Error('The brewery profile could not be loaded.');
  profile=await response.json();if(disposed)return;store=new Store(profile);buildEquipment();setSelected(selected);
  // Geometry failure must leave the telemetry inspector and asset buttons usable.
  try {
    const {createBrewery}=await import('./brewery.js?v=20261019.3');
    if(disposed)return;
    view=createBrewery($('canvas-host'),$('asset-labels'),profile,setSelected);$('scene-loading').hidden=true;
    $('canvas-host').addEventListener('webglcontextlost',event=>{event.preventDefault();$('scene-loading').hidden=false;text($('scene-loading'),'3D view paused. Reload to restore it; readings remain available below.');});
  } catch(error) {text($('scene-loading'),'3D is unavailable in this browser. Use the equipment buttons below to inspect live readings.');console.warn('Brewery 3D:',error.message);}
  $('live-mode').addEventListener('click',()=>mode(false));$('demo-mode').addEventListener('click',()=>mode(true));
  $('view-reset').addEventListener('click',()=>{view?.home();$('view-top').setAttribute('aria-pressed','false');$('view-focus').setAttribute('aria-pressed','false');});
  $('view-focus').addEventListener('click',()=>{const focused=$('view-focus').getAttribute('aria-pressed')!=='true';if(focused)view?.focus(selected);else view?.home();$('view-focus').setAttribute('aria-pressed',String(focused));$('view-top').setAttribute('aria-pressed','false');});
  $('view-motion').addEventListener('click',()=>{motionChoice=!animationEnabled();try{localStorage.setItem('sapdm-twin-motion',motionChoice?'on':'off');}catch(_){}syncMotion();paint();});
  reducedMotion.addEventListener('change',syncMotion);syncMotion();
  $('view-top').addEventListener('click',()=>{$('view-top').setAttribute('aria-pressed',String(view?.top()||false));$('view-focus').setAttribute('aria-pressed','false');});
  $('view-labels').addEventListener('click',()=>{$('view-labels').setAttribute('aria-pressed',String(view?.labels()||false));});
  $('connection-open').addEventListener('click',()=>{$('broker-url').value=window.dmMqtt.url;text($('connection-error'),'');$('connection-dialog').showModal();});
  $('connection-close').addEventListener('click',()=>$('connection-dialog').close());
  $('connection-form').addEventListener('submit',event=>{
    event.preventDefault();try {window.dmMqtt.save($('broker-url').value.trim());if(store.demo)mode(false);else{store=new Store(profile);motion=new Motion();eventSignature='';operatorSignature='';setSelected(selected);connect();}$('connection-dialog').close();}catch(error){text($('connection-error'),error.message);}
  });
  $('snapshot-request').addEventListener('click',snapshot);
  if(new URLSearchParams(location.search).get('demo')==='1')mode(true);else connect();
  timer=setInterval(()=>{if(store.demo){const elapsed=(performance.now()-demoStart)/1000;if(elapsed-lastDemo>=1){lastDemo=elapsed;demoTick(store,elapsed);}}paint();},250);
  function frame(time){raf=requestAnimationFrame(frame);if(document.hidden){lastFrame=time;return;}if(time-lastFrame<33)return;motion.step(store,(time-lastFrame)/1000,animationEnabled());lastFrame=time;view?.render(store,selected,motion,animationEnabled());renderActivity();}
  raf=requestAnimationFrame(frame);
}
window.addEventListener('pagehide',()=>{disposed=true;clearInterval(timer);cancelAnimationFrame(raf);stopClient();view?.dispose();});
window.addEventListener('pageshow',event=>{if(event.persisted)location.reload();});
init().catch(error=>{text($('scene-loading'),error.message);text($('conn-text'),'Unable to start');$('conn-badge').classList.add('error');});
