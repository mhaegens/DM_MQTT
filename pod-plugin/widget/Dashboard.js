(function(factory) {
  if(typeof sap!=='undefined' && sap.ui?.define)sap.ui.define([],factory);
  else if(typeof module==='object' && module.exports)module.exports=factory();
  else globalThis.DmMqttDashboard=factory();
})(function() {
  'use strict';
  const ns='http://www.w3.org/2000/svg';let sequence=0;
  const formatter=new Intl.NumberFormat('en-GB',{maximumFractionDigits:2});
  const format=n=>n===null || n===undefined?'—':formatter.format(n);
  function el(tag,cls,text) {const n=document.createElement(tag);if(cls)n.className=cls;if(text!==undefined)n.textContent=text;return n;}
  function svg(tag,attrs={}) {const n=document.createElementNS(ns,tag);for(const [k,v] of Object.entries(attrs))n.setAttribute(k,v);return n;}
  function write(n,value) {const next=String(value);if(n.textContent!==next)n.textContent=next;}
  function attr(n,key,value) {const next=String(value);if(n.getAttribute(key)!==next)n.setAttribute(key,next);}

  class Dashboard {
    constructor(root,state,{title='Shop floor live',logoUrl='',refresh=()=>{},reconnect=()=>{},showDiagnostics=true}={}) {
      this.root=root;this.state=state;this.closed=false;this.frame=0;this.metricViews=new Map();this.assetViews=new Map();this.tankViews=[];this.uid='dm-mqtt-'+(++sequence)+'-'+Math.random().toString(36).slice(2,8);
      root.classList.add('dm-mqtt-dashboard');root.dataset.view='all';
      const frame=el('div','dm-frame'),header=el('header','dm-header'),brand=el('div','dm-brand');
      if(logoUrl){const img=el('img','dm-logo');img.src=logoUrl;img.alt='SAP';img.width=52;img.height=26;brand.append(img);}
      const heading=el('div');heading.append(el('div','dm-kicker','Connected shop floor'),el('h2','dm-title',title));brand.append(heading);
      const headerRight=el('div','dm-header-right');this.health=el('span','dm-health','Waiting');this.updated=el('span','dm-updated','No readings yet');headerRight.append(this.health,this.updated);header.append(brand,headerRight);
      this.notice=el('div','dm-notice');this.notice.setAttribute('role','status');
      const intro=el('div','dm-intro');intro.append(el('h3','dm-plant',state.profile.plant_name),el('p','dm-description','One view of your equipment, vessels and latest readings.'));
      const actions=el('div','dm-actions');this.refresh=el('button','dm-button dm-primary','Refresh readings');this.reconnect=el('button','dm-button','Reconnect');actions.append(this.refresh,this.reconnect);intro.append(actions);
      this.onRefresh=()=>refresh();this.onReconnect=()=>reconnect();this.refresh.addEventListener('click',this.onRefresh);this.reconnect.addEventListener('click',this.onReconnect);
      const kpis=el('div','dm-kpis');this.kpis={};
      for(const [key,label,note] of [['running','Machines running','Current machine state'],['readings','Signals received','Latest values, updated in place'],['rate','Messages / sec','Received by this dashboard'],['rtt','Device round trip','Device ↔ broker']]) {
        const card=el('article','dm-kpi');this.kpis[key]=el('strong','','—');card.append(el('span','dm-label',label),this.kpis[key],el('small','',note));kpis.append(card);
      }
      const toolbar=el('div','dm-viewbar');toolbar.setAttribute('role','group');toolbar.setAttribute('aria-label','Visible assets');this.tabs=[];
      for(const [view,label] of [['all','All assets'],['machines','Machines'],['tanks','Tanks']]) {
        const button=el('button','dm-tab',label);button.type='button';button.setAttribute('aria-pressed',String(view==='all'));
        const handler=()=>{root.dataset.view=view;for(const t of this.tabs)t.button.setAttribute('aria-pressed',String(t.view===view));};
        button.addEventListener('click',handler);this.tabs.push({button,handler,view});toolbar.append(button);
      }
      const machines=this.section('Machines',state.profile.machines.length,'machines'),tanks=this.section('Vessels',state.profile.tanks.length,'tanks');
      for(const asset of state.assets.values()) {
        if(asset.kind==='machines')machines.grid.append(this.machine(asset));else tanks.grid.append(this.tank(asset));
      }
      if(!state.profile.machines.length)machines.grid.append(el('p','dm-empty','No machines in this profile.'));
      if(!state.profile.tanks.length)tanks.grid.append(el('p','dm-empty','No vessels in this profile.'));
      const operator=el('section','dm-operator');operator.setAttribute('aria-label','Latest operator badge');this.operatorName=el('strong','','No badge recorded');this.operatorDetail=el('span','','Waiting for a badge scan');this.operatorTime=el('span','dm-operator-time');
      const identity=el('div','dm-operator-identity');identity.append(el('span','dm-label','Latest operator'),this.operatorName,this.operatorDetail);operator.append(el('span','dm-avatar','OP'),identity,this.operatorTime);
      this.details=el('details','dm-details');this.details.hidden=!showDiagnostics;this.details.append(el('summary','','Connection details'));const diagnostics=el('dl','dm-diagnostics');this.diagnostics={};
      for(const [key,label] of [['pending','Queued messages'],['queue_ms','Device queue wait'],['failed','Failed sends'],['rejected','Rejected sends'],['internal_free','Internal memory'],['invalid','Invalid readings'],['unmapped','Unmapped signals']]) {
        const box=el('div');this.diagnostics[key]=el('dd','','—');box.append(el('dt','',label),this.diagnostics[key]);diagnostics.append(box);
      }
      this.details.append(diagnostics);this.mapping=el('p','dm-mapping');this.details.append(this.mapping);
      frame.append(header,this.notice,intro,kpis,toolbar,machines.root,tanks.root,operator,this.details,el('footer','dm-footer','Connected shop floor · MQTT live'));
      root.replaceChildren(frame);
      this.visibility=()=>{if(!document.hidden)this.schedule();};document.addEventListener('visibilitychange',this.visibility);
      this.timer=setInterval(()=>{if(!document.hidden)this.schedule();},1000);this.render();
    }
    section(title,count,kind) {
      const root=el('section','dm-section');root.dataset.section=kind;
      const heading=el('div','dm-section-heading');heading.append(el('h3','',title),el('span','dm-count',String(count)));
      const grid=el('div','dm-assets '+(kind==='tanks'?'dm-tanks':''));root.append(heading,grid);return {root,grid};
    }
    machine(asset) {
      const card=el('article','dm-machine');card.dataset.asset=asset.id;
      const top=el('div','dm-card-top'),heading=el('div');heading.append(el('h4','',asset.name),el('p','dm-asset-type',asset.type||'Machine'));
      const status=el('span','dm-status','Waiting');top.append(heading,status);const metrics=el('div','dm-metrics');
      for(const m of asset.metrics)metrics.append(this.metric(asset,m));
      const foot=el('div','dm-card-foot'),pulses=el('span','','Pulses —');foot.append(pulses,el('span','','Live telemetry'));card.append(top,metrics,foot);
      this.assetViews.set('machines/'+asset.id,{card,status,pulses});return card;
    }
    metric(asset,definition) {
      const topic=`${this.state.prefix}/${asset.kind}/${asset.id}/${definition.key}`;
      const box=el('div','dm-metric');box.dataset.topic=topic;
      const value=el('strong','dm-reading','—'),unit=el('span','dm-unit',definition.unit),row=el('div','dm-value-row');row.append(value,unit);
      const chart=svg('svg',{viewBox:'0 0 160 30','aria-hidden':'true',preserveAspectRatio:'none'});chart.classList.add('dm-chart');let graphic;
      if(definition.viz==='spark') {graphic=svg('polyline',{fill:'none',stroke:'currentColor','stroke-width':2,'vector-effect':'non-scaling-stroke'});chart.append(graphic);}
      else if(definition.viz==='arc') {
        chart.setAttribute('viewBox','0 0 100 50');chart.classList.add('dm-chart-arc');
        chart.append(svg('path',{d:'M8 44 A36 36 0 0 1 80 44',fill:'none',stroke:'#e5edf5','stroke-width':7,'stroke-linecap':'round'}));
        graphic=svg('path',{d:'M8 44 A36 36 0 0 1 80 44',fill:'none',stroke:'currentColor','stroke-width':7,'stroke-linecap':'round',pathLength:100,'stroke-dasharray':'0 100'});chart.append(graphic);
      } else {
        chart.append(svg('rect',{x:0,y:12,width:160,height:6,rx:3,fill:'#e5edf5'}));graphic=svg('rect',{x:0,y:12,width:0,height:6,rx:3,fill:'currentColor'});chart.append(graphic);
      }
      box.append(el('span','dm-metric-label',definition.label),row,chart);
      this.metricViews.set(topic,{box,value,graphic,definition,lastValue:undefined});return box;
    }
    tank(asset) {
      const card=el('article','dm-tank');card.dataset.asset=asset.id;
      const top=el('div','dm-card-top'),heading=el('div');heading.append(el('h4','',asset.name),el('p','dm-asset-type','Conical vessel'));const level=el('strong','dm-tank-level','—');top.append(heading,level);
      const body=el('div','dm-tank-body'),vessel=svg('svg',{viewBox:'0 0 152 218',role:'img','aria-label':asset.name+' level'});vessel.classList.add('dm-vessel');
      const clipId=this.uid+'-'+asset.id,shape='M32 28 Q32 18 42 18 H110 Q120 18 120 28 V132 L76 186 L32 132 Z';
      const defs=svg('defs'),clip=svg('clipPath',{id:clipId});clip.append(svg('path',{d:shape}));defs.append(clip);vessel.append(defs);
      vessel.append(svg('path',{d:'M39 129 V204 M113 129 V204 M76 186 V207',stroke:'#9db0c2','stroke-width':5,'stroke-linecap':'round',fill:'none'}));
      vessel.append(svg('path',{d:shape,fill:'#edf3f8',stroke:'#b5c9dc','stroke-width':2}));
      const fill=svg('rect',{x:32,y:186,width:88,height:0,fill:'#0070f2','fill-opacity':'.84','clip-path':`url(#${clipId})`});vessel.append(fill);
      vessel.append(svg('path',{d:shape,fill:'none',stroke:'#9db0c2','stroke-width':2}));
      vessel.append(svg('path',{d:'M39 18 Q76 -1 113 18 M66 7 V3 H86 V7 M67 208 H85',stroke:'#9db0c2','stroke-width':3,fill:'none','stroke-linecap':'round'}));
      for(const y of [50,80,110])vessel.append(svg('path',{d:`M126 ${y} H133`,stroke:'#b5c9dc','stroke-width':2}));
      const metrics=el('div','dm-tank-metrics');for(const m of asset.metrics)metrics.append(this.metric(asset,m));body.append(vessel,metrics);card.append(top,body);
      const primary=asset.metrics.find(m=>m.key==='level'||m.viz==='tank')||asset.metrics[0];
      this.tankViews.push({fill,level,primary,topic:`${this.state.prefix}/tanks/${asset.id}/${primary.key}`,vessel,lastValue:undefined});return card;
    }
    schedule() {
      if(this.closed || this.frame || document.hidden)return;
      this.frame=requestAnimationFrame(()=>{this.frame=0;this.render();});
    }
    render() {
      if(this.closed)return;
      const state=this.state;state.tick();const health=state.health();this.root.dataset.stale=String(health.stale);
      write(this.health,health.label);this.health.dataset.tone=health.tone;
      write(this.updated,state.lastDeviceSeen?'Updated '+new Date(state.lastDeviceSeen).toLocaleTimeString('en-GB'):'No readings yet');
      let notice=state.message;
      if(state.connection==='preview')notice='Design preview · Sample readings. No live broker connection.';
      else if(health.stale && state.received && !notice)notice='Showing last known readings. Waiting for the device to reconnect.';
      write(this.notice,notice);this.notice.hidden=!notice;this.refresh.disabled=state.connection!=='connected';this.reconnect.disabled=['preview','idle'].includes(state.connection);
      const machines=[...state.assets.values()].filter(a=>a.kind==='machines');
      write(this.kpis.running,`${machines.filter(a=>a.status==='RUNNING').length} / ${machines.length}`);
      write(this.kpis.readings,`${[...state.metrics.values()].filter(m=>m.value!==null).length} / ${state.metrics.size}`);
      write(this.kpis.rate,format(Math.round(state.rate*10)/10));write(this.kpis.rtt,state.diagnostics.rtt_ms===undefined?'—':format(state.diagnostics.rtt_ms)+' ms');
      for(const [topic,view] of this.metricViews) {
        const m=state.metrics.get(topic);
        if(m.value===view.lastValue)continue;
        view.lastValue=m.value;write(view.value,format(m.value));const percent=state.percent(m);view.box.style.setProperty('--metric-color',state.color(m));
        if(m.viz==='spark') {
          const values=m.history,points=values.map((v,i)=>`${values.length===1?0:i/(values.length-1)*160},${28-Math.max(0,Math.min(1,(v-m.min)/(m.max-m.min)))*26}`).join(' ');attr(view.graphic,'points',points);
        } else if(m.viz==='arc')attr(view.graphic,'stroke-dasharray',`${percent} 100`);
        else attr(view.graphic,'width',percent*1.6);
      }
      for(const [key,view] of this.assetViews) {
        const a=state.assets.get(key);write(view.status,a.status==='RUNNING'?'Running':a.status==='STOPPED'?'Stopped':'Waiting');view.status.dataset.running=String(a.status==='RUNNING');write(view.pulses,'Pulses '+format(a.pulses));
      }
      for(const view of this.tankViews) {
        const m=state.metrics.get(view.topic);if(m.value===view.lastValue)continue;view.lastValue=m.value;const p=state.percent(m);
        attr(view.fill,'y',186-p*1.68);attr(view.fill,'height',p*1.68);attr(view.fill,'fill',state.color(m));write(view.level,m.value===null?'—':format(m.value)+' '+m.unit);
        attr(view.vessel,'aria-label',state.assets.get('tanks/'+view.topic.split('/').at(-2))?.name+' level '+format(m.value)+' '+m.unit);
      }
      write(this.operatorName,state.operator?.name||'No badge recorded');write(this.operatorDetail,state.operator?[state.operator.role,state.operator.plant].filter(Boolean).join(' · '):'Waiting for a badge scan');write(this.operatorTime,state.operator?new Date(state.operator.time).toLocaleTimeString('en-GB'):'');
      for(const [key,node] of Object.entries(this.diagnostics)) {
        const n=key==='invalid'?state.invalid:key==='unmapped'?state.unmapped:state.diagnostics[key];
        write(node,n===undefined?'—':key==='internal_free'?format(Math.round(n/1024))+' KB':format(n)+(key==='queue_ms'?' ms':''));
      }
      write(this.mapping,state.unmapped?'Some signals do not match this profile. Choose the matching device profile in POD Designer.':'Profile matches '+state.profile.plant_name+'.');
    }
    destroy() {
      if(this.closed)return;this.closed=true;if(this.frame)cancelAnimationFrame(this.frame);clearInterval(this.timer);document.removeEventListener('visibilitychange',this.visibility);
      this.refresh.removeEventListener('click',this.onRefresh);this.reconnect.removeEventListener('click',this.onReconnect);for(const t of this.tabs)t.button.removeEventListener('click',t.handler);
      this.metricViews.clear();this.assetViews.clear();this.tankViews.length=0;
    }
  }
  return Dashboard;
});
