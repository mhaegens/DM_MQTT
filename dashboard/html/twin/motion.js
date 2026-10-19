/* Signal-driven animation phases. Advance by elapsed time, never time × rate:
 * a changing rate must not move equipment backwards or teleport it. */
(function(root,factory){
  if(typeof module==='object'&&module.exports)module.exports=factory();
  else root.BreweryMotion=factory();
})(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const TAU=Math.PI*2;
  const PRIMARY={mash01:'stirRpm',boil01:'boilProg',ferm01:'co2Rate',fill01:'bottleCnt'};
  function primaryMetric(asset){return asset.metrics.find(m=>m.key===PRIMARY[asset.id])||asset.metrics[0];}
  function positive(store,asset,key,now){const sample=asset.values.get(key);return store.current(sample,now)?Math.max(0,sample.value):0;}
  class Motion {
    constructor(){this.frames=new Map();this.elapsed=0;}
    step(store,delta,animate=true,now=Date.now()){
      // Hidden-tab time is deliberately not replayed as an animation jump.
      const dt=Number.isFinite(delta)?Math.max(0,Math.min(.1,delta)):0;
      if(animate)this.elapsed+=dt;
      for(const asset of store.assets.values()){
        let f=this.frames.get(asset.id);
        if(!f){f={bottles:0,stirrer:0,gas:0,steam:0,liquid:0,pump:0};this.frames.set(asset.id,f);}
        const running=store.status(asset,now)==='running';
        f.running=running;f.moving=running&&animate;
        f.bottleRate=positive(store,asset,'bottleCnt',now);
        f.flowRate=positive(store,asset,'flowRate',now);
        f.rpm=positive(store,asset,'stirRpm',now);
        f.co2Rate=positive(store,asset,'co2Rate',now);
        const temp=positive(store,asset,'boilTemp',now);
        f.boiling=running&&temp>80;
        if(!f.moving)continue;
        f.bottles=(f.bottles+f.bottleRate/60*dt)%12;
        f.stirrer=(f.stirrer+f.rpm/60*TAU*dt)%TAU;
        f.gas=(f.gas+f.co2Rate/15*dt)%1;
        f.steam=(f.steam+Math.max(0,temp-80)/30*dt)%1;
        f.liquid=(f.liquid+f.flowRate/30*dt)%1;
        f.pump=(f.pump+TAU*.4*dt)%TAU;
      }
      return this;
    }
    get(id){return this.frames.get(id);}
  }
  return {Motion,primaryMetric};
});
