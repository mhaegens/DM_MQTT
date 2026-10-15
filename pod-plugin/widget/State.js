(function (factory) {
  if (typeof sap !== 'undefined' && sap.ui?.define) sap.ui.define([], factory);
  else if (typeof module === 'object' && module.exports) module.exports = factory();
  else globalThis.DmMqttState = factory();
})(function () {
  'use strict';
  const id = /^[A-Za-z0-9_-]{1,32}$/;
  const finite = n => typeof n === 'number' && Number.isFinite(n);
  const text = (v, max = 120) => typeof v === 'string' ? v.slice(0, max) : '';
  const numeric = s => typeof s === 'string' && /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(s.trim()) && Number.isFinite(Number(s)) ? Number(s) : null;

  function validateProfile(profile) {
    if (!profile || typeof profile !== 'object' || !Array.isArray(profile.machines) || !Array.isArray(profile.tanks)) throw Error('The profile needs machines and tanks arrays.');
    if (profile.machines.length > 10 || profile.tanks.length > 3) throw Error('Profiles support up to 10 machines and 3 tanks.');
    const out = {plant_name: text(profile.plant_name) || 'Connected shop floor', machines: [], tanks: []};
    for (const kind of ['machines', 'tanks']) {
      const ids = new Set();
      for (const entity of profile[kind]) {
        if (!entity || !id.test(entity.id) || ids.has(entity.id)) throw Error('Entity identifiers must be unique, short topic-safe names.');
        ids.add(entity.id);
        if (!Array.isArray(entity.metrics) || !entity.metrics.length || entity.metrics.length > 8) throw Error('Each asset needs 1–8 metrics.');
        const keys = new Set();
        const metrics = entity.metrics.map(m => {
          if (!m || !id.test(m.key) || keys.has(m.key)) throw Error('Metric identifiers must be unique topic-safe names.');
          keys.add(m.key);
          if (!finite(m.min_val) || !finite(m.max_val) || m.max_val <= m.min_val) throw Error('Metric ranges must be finite and increasing.');
          const thresholds = m.thresholds ?? [];
          if (!Array.isArray(thresholds) || thresholds.length > 16 || thresholds.some(t => !t || !finite(t.limit) || !/^(?:0x|#)[0-9a-f]{6}$/i.test(t.color))) throw Error('Invalid metric thresholds.');
          return {key:m.key, label:text(m.label) || m.key, unit:text(m.unit,24), min:m.min_val, max:m.max_val,
            viz:['arc','spark','bullet','thermo','tank'].includes(m.viz) ? m.viz : 'bullet',
            color:/^(?:0x|#)[0-9a-f]{6}$/i.test(m.color || '') ? m.color.replace(/^0x/i,'#') : '#0070f2',
            thresholds:thresholds.map(t => ({limit:t.limit,color:t.color.replace(/^0x/i,'#')})).sort((a,b)=>a.limit-b.limit)};
        });
        out[kind].push({id:entity.id,name:text(entity.name) || entity.id,type:text(entity.type),metrics});
      }
    }
    return out;
  }

  class State {
    constructor(profile, {prefix='factory', staleSeconds=30, now=()=>Date.now()}={}) {
      if (!/^[A-Za-z0-9_-]+(?:\/[A-Za-z0-9_-]+)*$/.test(prefix) || prefix.length > 64) throw Error('Use a topic prefix without wildcards or empty segments.');
      if (!finite(Number(staleSeconds)) || Number(staleSeconds) < 15 || Number(staleSeconds) > 3600) throw Error('Stale timeout must be 15–3600 seconds.');
      this.profile=validateProfile(profile); this.prefix=prefix; this.staleMs=Number(staleSeconds)*1000; this.now=now;
      this.metrics=new Map(); this.assets=new Map(); this.connection='idle'; this.message='Set the broker URL in POD Designer to connect.';
      for (const kind of ['machines','tanks']) for (const entity of this.profile[kind]) {
        const asset={...entity,kind,status:null,pulses:null};
        this.assets.set(`${kind}/${entity.id}`,asset);
        for (const m of entity.metrics) this.metrics.set(`${prefix}/${kind}/${entity.id}/${m.key}`, {...m,topic:`${prefix}/${kind}/${entity.id}/${m.key}`,value:null,updatedAt:0,history:[]});
      }
      this.received=0; this.changed=0; this.invalid=0; this.unmapped=0; this.operatorCount=0; this.operator=null;
      this.lastDeviceSeen=0; this.deviceStatus='unknown'; this.diagnostics={}; this.rate=0; this.rateStart=now(); this.rateCount=0;
    }
    resetReadings() {
      for (const m of this.metrics.values()) {m.value=null;m.updatedAt=0;m.history.length=0;}
      for (const a of this.assets.values()) {a.status=null;a.pulses=null;}
      this.lastDeviceSeen=0;this.deviceStatus='unknown';this.diagnostics={};this.operator=null;
    }
    setConnection(state,message='') {this.connection=state;this.message=message;}
    apply(topic,payload) {
      if (typeof topic !== 'string' || topic.length > 192 || typeof payload !== 'string' || payload.length > 8192) {this.invalid++;return false;}
      const metric=this.metrics.get(topic), time=this.now();
      let accepted=false;
      if (metric) {
        const value=numeric(payload);
        if (value===null) {this.invalid++;return false;}
        if (metric.value!==value) {
          metric.value=value;metric.history.push(value);if(metric.history.length>60)metric.history.shift();this.changed++;
        }
        metric.updatedAt=time;accepted=true;
      } else if (topic===`${this.prefix}/device/status`) {
        if (!['online','offline'].includes(payload)) {this.invalid++;return false;}
        this.deviceStatus=payload;accepted=true;
      } else if (topic===`${this.prefix}/device/performance`) {
        let data;try {data=JSON.parse(payload);} catch (_) {this.invalid++;return false;}
        if (!data || typeof data!=='object' || Array.isArray(data)) {this.invalid++;return false;}
        const selected={};
        for (const key of ['rtt_ms','pending','failed','rejected','queue_ms','queue_max_ms','internal_free','suppressed']) if (finite(data[key]) && data[key]>=0) selected[key]=data[key];
        if (!Object.keys(selected).length) {this.invalid++;return false;}
        this.diagnostics=selected;accepted=true;
      } else if (topic===`${this.prefix}/operators/login`) {
        let data;try {data=JSON.parse(payload);} catch (_) {this.invalid++;return false;}
        if (!data || typeof data.name!=='string') {this.invalid++;return false;}
        this.operator={name:text(data.name),role:text(data.role),plant:text(data.plant),time};this.operatorCount++;accepted=true;
      } else if (topic.startsWith(this.prefix+'/')) {
        const rest=topic.slice(this.prefix.length+1), split=rest.lastIndexOf('/');
        const asset=this.assets.get(rest.slice(0,split)), key=rest.slice(split+1);
        if (asset?.kind==='machines' && key==='status' && ['RUNNING','STOPPED'].includes(payload)) {asset.status=payload;accepted=true;}
        else if (asset?.kind==='machines' && ['pulse','pulses'].includes(key) && /^\d{1,15}$/.test(payload) && Number.isSafeInteger(Number(payload))) {asset.pulses=Number(payload);accepted=true;}
        else if (!rest.startsWith('device/')) this.unmapped++;
      }
      if (accepted) {
        this.received++;this.rateCount++;
        // Retained availability has no timestamp. It must not make old data
        // look fresh when a new viewer joins; wait for actual device traffic.
        if (topic!==`${this.prefix}/device/status`) {this.lastDeviceSeen=time;this.deviceStatus='online';}
      }
      return accepted;
    }
    health() {
      if (this.connection==='preview') return {label:'Design preview',tone:'preview',stale:false};
      if (this.connection!=='connected') return {label:this.connection==='connecting'?'Connecting':'Disconnected',tone:'muted',stale:true};
      if (this.deviceStatus==='offline') return {label:'Device offline',tone:'warning',stale:true};
      if (!this.lastDeviceSeen) return {label:'Waiting for device',tone:'muted',stale:true};
      if (this.now()-this.lastDeviceSeen>this.staleMs) return {label:'Readings stale',tone:'warning',stale:true};
      return {label:'Live readings',tone:'good',stale:false};
    }
    tick() {
      const elapsed=this.now()-this.rateStart;
      if (elapsed>=1000) {this.rate=this.rateCount*1000/elapsed;this.rateCount=0;this.rateStart=this.now();}
    }
    color(metric) {return metric.thresholds.find(t=>metric.value<=t.limit)?.color || metric.color;}
    percent(metric) {return metric.value===null?0:Math.max(0,Math.min(100,(metric.value-metric.min)/(metric.max-metric.min)*100));}
    seedPreview(source) {
      this.setConnection('preview');
      for (const kind of ['machines','tanks']) for (const e of source[kind]) {
        if (kind==='machines') {this.apply(`${this.prefix}/${kind}/${e.id}/status`,e.running?'RUNNING':'STOPPED');this.apply(`${this.prefix}/${kind}/${e.id}/pulses`,String(e.pulses||0));}
        for (const m of e.metrics) this.apply(`${this.prefix}/${kind}/${e.id}/${m.key}`,String(m.init_val ?? m.min_val));
      }
    }
  }
  State.validateProfile=validateProfile;
  return State;
});
