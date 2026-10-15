(async function() {
  'use strict';
  let connection,view,timer,version=0;
  const field=id=>document.getElementById(id);
  async function load(live=false) {
    const generation=++version;clearInterval(timer);connection?.destroy();view?.destroy();
    const profile=await fetch('../config/'+field('profile').value+'.json').then(r=>r.json());
    if(generation!==version)return;
    const state=new DmMqttState(profile);
    connection=new DmMqttConnection(DmMqttClient,state,{brokerUrl:field('broker').value,secure:location.protocol==='https:',changed:()=>view?.schedule()});
    view=new DmMqttDashboard(field('dashboard'),state,{logoUrl:'../assets/sap-logo.svg',refresh:()=>connection.requestSnapshot(),reconnect:()=>connection.reconnect()});
    if(live)connection.start();else {
      state.seedPreview(profile);view.schedule();
      timer=setInterval(()=>{
        for(const m of state.metrics.values()) {
          const next=Math.max(m.min,Math.min(m.max,(m.value??m.min)+(Math.random()-.5)*(m.max-m.min)*.025));
          state.apply(m.topic,next.toFixed(2));
        }
        view.schedule();
      },1400);
    }
    // Explicit test hook exists only in this excluded-from-ZIP preview page.
    window.preview={state,view,connection,load};
  }
  field('profile').addEventListener('change',()=>load());
  field('simulate').addEventListener('click',()=>load());
  field('connect').addEventListener('click',()=>load(true));
  window.addEventListener('pagehide',()=>{version++;clearInterval(timer);connection?.destroy();view?.destroy();});
  await load();
})();
