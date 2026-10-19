import * as THREE from 'three';
import {OrbitControls} from './vendor/OrbitControls.js';
const {primaryMetric}=window.BreweryMotion;

const LAYOUT = {
  malt: {x:-10, z:-4, h:6.1, r:1.05}, mash01: {x:-5.1, z:-.5, h:3.5, r:1.35},
  boil01: {x:0, z:-.5, h:3.9, r:1.35}, ferm01: {x:5, z:-3.2, h:5, r:1.2},
  bright: {x:10, z:-3.2, h:4.8, r:1.2}, fill01: {x:8, z:4.2, h:2.7},
  clean01: {x:-4.6, z:5.2, h:2.7}, spent: {x:-10, z:2.8, h:2.7, r:1.1}
};
const STATUS = {running:0x318565, stopped:0xc48e3e, waiting:0x9caba5, stale:0x9caba5, ok:0x318565, warn:0xc48e3e, err:0xb94e43};

export function createBrewery(host, labelHost, profile, onSelect) {
  const scene = new THREE.Scene(); scene.background = new THREE.Color('#eaf0ec');
  const renderer = new THREE.WebGLRenderer({antialias:true});
  renderer.setPixelRatio(Math.min(devicePixelRatio, 1.8)); renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap; renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.toneMappingExposure = 1.3;
  renderer.domElement.setAttribute('aria-label','Interactive 3D brewery. Equipment can also be selected using the buttons below.');
  host.append(renderer.domElement);
  const camera = new THREE.OrthographicCamera(-20,20,15,-15,.1,160);
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true; controls.dampingFactor = .09; controls.minZoom = .6; controls.maxZoom = 5;
  controls.minPolarAngle = .05; controls.maxPolarAngle = Math.PI * .46;
  controls.mouseButtons = {LEFT:THREE.MOUSE.ROTATE, MIDDLE:THREE.MOUSE.DOLLY, RIGHT:THREE.MOUSE.PAN};
  scene.add(new THREE.HemisphereLight(0xffffff,0xb0c5b8,2.6));
  const sunlight = new THREE.DirectionalLight(0xfff4dc,3.3); sunlight.position.set(-13,25,16); sunlight.castShadow = true;
  sunlight.shadow.mapSize.set(2048,2048); Object.assign(sunlight.shadow.camera,{left:-25,right:25,top:24,bottom:-24,near:1,far:80});
  sunlight.shadow.normalBias = .04; sunlight.shadow.bias = -.0002; scene.add(sunlight);
  const rim = new THREE.DirectionalLight(0xd4e9ff,1.1); rim.position.set(15,15,-15); scene.add(rim);
  const world = new THREE.Group(); scene.add(world);
  const materials = new Map(), geometries = new Set(), textures = new Set();
  function mat(color, extra={}) {
    const key = String(color)+JSON.stringify(extra);
    if (!materials.has(key)) materials.set(key,new THREE.MeshStandardMaterial({color,roughness:.48,...extra}));
    return materials.get(key);
  }
  function mesh(geometry, color, x,y,z, parent=world, extra={}) {
    geometries.add(geometry); const m = new THREE.Mesh(geometry,mat(color,extra));
    m.position.set(x,y,z); m.castShadow = true; m.receiveShadow = true; parent.add(m); return m;
  }
  const box = (w,h,d,c,x,y,z,p=world,o={}) => mesh(new THREE.BoxGeometry(w,h,d),c,x,y,z,p,o);
  const cyl = (r,h,c,x,y,z,p=world,o={}) => mesh(new THREE.CylinderGeometry(r,r,h,32),c,x,y,z,p,o);
  const cone = (a,b,h,c,x,y,z,p=world,o={}) => mesh(new THREE.CylinderGeometry(a,b,h,32),c,x,y,z,p,o);
  function pipe(points,color=0xa8b9b5,r=.06,parent=world) {
    const curve = new THREE.CatmullRomCurve3(points.map(p=>new THREE.Vector3(...p)),false,'catmullrom',.12);
    mesh(new THREE.TubeGeometry(curve,64,r,8,false),color,0,0,0,parent,{metalness:.65,roughness:.35}); return curve;
  }
  function text(value,x,z,width,color='#80998c',size=100) {
    const canvas = document.createElement('canvas'); canvas.width=1024; canvas.height=160;
    const ctx = canvas.getContext('2d'); ctx.fillStyle=color; ctx.textAlign='center'; ctx.textBaseline='middle';
    ctx.font=`600 ${size}px Arial`; ctx.fillText(value,512,80);
    const texture = new THREE.CanvasTexture(canvas); textures.add(texture);
    const geometry = new THREE.PlaneGeometry(width,width/6.4); geometries.add(geometry);
    const material = new THREE.MeshBasicMaterial({map:texture,transparent:true,depthWrite:false}); materials.set(value,material);
    const plane = new THREE.Mesh(geometry,material); plane.rotation.x=-Math.PI/2; plane.position.set(x,.041,z); world.add(plane);
  }
  // A cutaway brewhouse: its layout is schematic, with no position telemetry.
  box(34,.32,22,0xd2ded3,0,-.32,0); box(30,.15,17.5,0xf0f1e7,0,-.09,0);
  box(150,.12,150,0xeaf0ec,0,-.57,0);
  for (let x=-14;x<=14;x+=2) box(.018,.01,17.5,0xdce2d8,x,.001,0);
  for (let z=-8;z<=8;z+=2) box(30,.01,.018,0xdce2d8,0,.001,z);
  box(30,3.4,.2,0xd5e1d5,0,1.7,-8.7); box(.2,3.4,17.4,0xe0e7da,-15,1.7,0);
  box(30,.12,.3,0x7f9f8e,0,3.45,-8.7); box(.3,.12,17.4,0x9db5a2,-15,3.45,0);
  for (let x=-13;x<15;x+=4) {
    box(.16,3.4,.25,0xa5bfae,x,1.7,-8.52);
    box(3.1,1,.055,0xa2c2bb,x+1.75,2.35,-8.55,world,{metalness:.25,roughness:.2});
    box(3.2,.065,.08,0xf4f3e6,x+1.75,2.35,-8.48);
  }
  pipe([[-14,3,-8.2],[13,3,-8.2],[13,.4,-8.2]],0xa8b8ae,.07);
  pipe([[-14,2.7,-8.2],[11.5,2.7,-8.2]],0x7aa6b3,.045);
  for (let x=-13;x<=13;x+=1.5) box(.7,.013,.06,0xffffff,x,.03,7.4);
  for (const z of [6.65,8.1]) box(28,.012,.04,0xc4b76c,0,.02,z);
  text('BREWERY GE01',.4,5.1,9.8,'#bdcabb',85);
  text('BREWHOUSE',-4.5,-5.8,5.2,'#a6b7a5',83); text('CELLAR',7.5,-6.7,4,'#a6b7a5');
  text('GRAIN IN',-11,-6.7,3,'#a1b39e'); text('PACKAGING',9.5,6.1,4.7,'#a1b39e');
  // Sacks, pallets and a small utilities cabinet are scenery.
  for (let i=0;i<3;i++) {
    box(1.5,.16,1.1,0xbda57b,-12.8,.1,-5+i*1.5);
    for (let j=0;j<3;j++) box(.38,.62,.8,0xdccca4,-13.3+j*.47,.45,-5+i*1.5);
  }
  box(1.3,1.9,.7,0xe7ede3,-12.7,.95,6); box(.06,1.5,.03,0x839e92,-12.85,1,6.37);
  for (let j=0;j<3;j++) box(.1,.1,.03,j===0?0x66a77b:0xb2c9bc,-12.4,1.4-j*.2,6.37);
  for (const x of [-14,14]) for (const z of [-6,6]) { cyl(.07,.7,0xd9b65c,x,.35,z); cyl(.14,.05,0x647763,x,.03,z); }
  const assets = new Map(), hits = [], labels = new Map();
  function vessel(g,p,copper=false,level=false) {
    const r=p.r, h=p.h;
    box(r*2.65,.12,r*2.65,0xcbd8c8,0,.09,0,g);
    for (const x of [-r*.65,r*.65]) for (const z of [-r*.65,r*.65]) cyl(.065,.8,0x7c9489,x,.45,z,g,{metalness:.7});
    const color=copper?0xc18b57:0xc6d7d5;
    cone(r,r*.28,.65,color,0,.95,0,g,{metalness:.65,roughness:.35});
    const bodyHeight=h-1.7;
    const body=cyl(r,bodyHeight,color,0,1.25+bodyHeight/2,0,g,level?{transparent:true,opacity:.27,depthWrite:false,metalness:.2,roughness:.2}:{metalness:.6,roughness:.3});
    const top=cone(r*.67,r,.3,color,0,h-.3,0,g,{metalness:.65,roughness:.3});
    cyl(r*.67,.13,color,0,h-.09,0,g,{metalness:.65}); cyl(.19,.15,0x718f85,0,h+.04,0,g);
    for (const y of [1.32,h-.46]) cyl(r+.04,.065,0xe1e6d9,0,y,0,g,{metalness:.7});
    pipe([[r*.4,.7,0],[r*.4,.4,.8],[r*.4,.4,1.65]],0x91aaa1,.065,g);
    const valve=cyl(.15,.06,0x4d8d84,r*.4,.56,1.2,g); valve.rotation.z=Math.PI/2;
    box(.45,.32,.07,0x42635c,.15,1.5,r+.03,g); box(.28,.14,.02,0xc6dccd,.15,1.54,r+.08,g);
    let liquid;
    if (level) {
      liquid=cyl(r*.94,1,0xd2a34f,0,1.28,0,g,{transparent:true,opacity:.87,roughness:.25});
      liquid.visible=false;
      // The visible volume is a level indicator, not a measured vessel capacity.
      for (let i=0;i<=4;i++) box(.18,.025,.025,0x527b6a,r*.8,1.35+i*(bodyHeight-.15)/4,r*.65,g);
    }
    return {body,top,liquid,bodyHeight};
  }
  for (const group of ['machines','tanks']) for (const asset of profile[group]) {
    const p=LAYOUT[asset.id], g=new THREE.Group(); g.position.set(p.x,0,p.z); world.add(g);
    const ring = new THREE.Group(); g.add(ring);
    const rw=asset.id==='fill01'?3.4:asset.id==='clean01'?2.4:(p.r||1.3)+.5;
    for (const z of [-1.8,1.8]) box(rw*2,.035,.045,0x0070f2,0,.03,z,ring);
    for (const x of [-rw,rw]) box(.045,.035,3.6,0x0070f2,x,.03,0,ring);
    ring.visible=false;
    let parts={};
    if (p.r) {
      parts=vessel(g,p,['mash01','boil01'].includes(asset.id),group==='tanks');
      if (asset.id==='malt') {cone(.26,.65,.5,0xa2b5a9,0,p.h+.3,0,g,{metalness:.6});pipe([[0,p.h+.5,0],[0,p.h+.8,-.6],[-1,p.h+.8,-.6]],0xa6b7af,.08,g);}
      if (asset.id==='mash01') {
        cyl(.2,.38,0x4f8075,0,p.h+.24,0,g);
        const paddle = new THREE.Group(); paddle.position.y=p.h+.5; g.add(paddle);
        box(1.65,.11,.16,0x578976,0,0,0,paddle);box(.16,.11,1.65,0x94b79a,0,0,0,paddle);parts.paddle=paddle;
      }
      if (asset.id==='boil01') {
        const track=mesh(new THREE.TorusGeometry(p.r+.25,.045,8,72),0xc5d4c9,0,.08,0,g);track.rotation.x=-Math.PI/2;
        const progress=mesh(new THREE.TorusGeometry(p.r+.25,.07,8,72),0xc09532,0,.1,0,g);progress.rotation.x=-Math.PI/2;parts.progress=progress;
        pipe([[0,p.h,0],[0,p.h+.7,0],[.3,p.h+.9,-.25]],0xb8c7bb,.12,g);
        parts.steam=[];
        for (let j=0;j<6;j++) {
          const puff=mesh(new THREE.IcosahedronGeometry(.17,1),0xf9fcf5,0,p.h+1,0,g,{transparent:true,opacity:.45,depthWrite:false}); puff.castShadow=false; parts.steam.push(puff);
        }
      }
      if (asset.id==='ferm01') {
        pipe([[1,1.3,.8],[1.5,1.3,.8],[1.5,3.8,.8],[1,3.8,.8]],0x93b2a6,.05,g);
        parts.bubbles=[];
        for (let j=0;j<4;j++) parts.bubbles.push(mesh(new THREE.SphereGeometry(.12,10,10),0x509eac,1.5,1.6+j*.5,.8,g));
      }
    } else if (asset.id==='fill01') {
      box(6.5,.25,1.3,0xa1b7aa,0,.92,0,g); box(6.4,.06,.93,0x466e5d,0,1.08,0,g);
      for (const z of [-.68,.68]) box(6.5,.07,.05,0xe0e6d9,0,1.21,z,g);
      for (const x of [-2.7,0,2.7]) for (const z of [-.5,.5]) box(.09,.85,.09,0x8ba494,x,.43,z,g);
      for (const x of [-1.3,1.3]) box(.15,1.7,.8,0x85a599,x,1.8,-.32,g);
      box(3,.25,1.1,0x6c998b,0,2.65,-.32,g);
      parts.streams=[];
      for (let x=-.9;x<=1;x+=.6) {
        cyl(.055,.5,0xb9c8b7,x,2.25,0,g);
        parts.streams.push(cyl(.045,.35,0xd5a648,x,1.82,0,g,{emissive:0xb18e3d,emissiveIntensity:.2}));
      }
      parts.belt=[];
      for(let i=0;i<12;i++){
        const mark=box(.16,.012,.65,i%3===0?0xe2c376:0x9ab9a0,-3+i*.5,1.12,0,g);parts.belt.push(mark);
      }
      parts.wheels=[];
      for(const x of [-2.75,2.75]){
        const wheel=new THREE.Group();wheel.position.set(x,.86,.79);g.add(wheel);
        const hub=cyl(.24,.13,0x426f5c,0,0,0,wheel);hub.rotation.x=Math.PI/2;
        box(.35,.06,.15,0xe3bf62,0,0,0,wheel);box(.06,.35,.15,0xe3bf62,0,0,0,wheel);parts.wheels.push(wheel);
      }
      box(.75,.55,.1,0x3f635c,1.95,2,.6,g);box(.59,.36,.04,0xbedbcc,1.95,2.04,.67,g);
      parts.bottles=[];
      for (let j=0;j<12;j++) {
        const bottle=new THREE.Group(); bottle.position.set(-2.75+j*.5,1.13,0);g.add(bottle);
        cyl(.11,.36,0x8b622a,0,.18,0,bottle,{roughness:.25,metalness:.2});cone(.045,.11,.1,0x8b622a,0,.4,0,bottle);cyl(.045,.16,0x8b622a,0,.5,0,bottle);
        cyl(.113,.13,j%3===0?0xe2c376:0xece3bd,0,.19,0,bottle);cyl(.048,.035,0xe2c574,0,.59,0,bottle);parts.bottles.push(bottle);
      }
    } else {
      box(3.8,.15,2,0xa8bdad,0,.18,0,g);
      for (const x of [-1.1,0,1.1]) {
        cyl(.4,1.65,0xc4d4ce,x,1.15,0,g,{metalness:.55});cone(.3,.4,.2,0xdce5d8,x,2.07,0,g,{metalness:.55});
        cyl(.1,.15,0x789e96,x,2.25,0,g);
      }
      pipe([[-1.5,.5,.65],[1.5,.5,.65],[1.5,.75,1.2]],0x6f9fae,.07,g);
      box(.75,.6,.13,0x628c80,-.1,1.2,1,g);box(.52,.32,.04,0xc4ddc8,-.1,1.24,1.09,g);
      const pump=new THREE.Group();pump.position.set(1.3,.53,.91);g.add(pump);
      const casing=cyl(.24,.15,0x5593a7,0,0,0,pump);casing.rotation.x=Math.PI/2;
      box(.34,.07,.17,0xb8dae1,0,0,0,pump);box(.07,.34,.17,0xb8dae1,0,0,0,pump);parts.pump=pump;
    }
    const lamp = cyl(.085,.24,STATUS.waiting,asset.id==='fill01'?1.25:.75,p.h+.3,-.8,g,{emissive:STATUS.waiting,emissiveIntensity:.15});
    const hit=box(asset.id==='fill01'?6.5:3.2,p.h+1,3.4,0xffffff,0,(p.h+1)/2,0,g,{transparent:true,opacity:0,depthWrite:false});
    hit.userData.asset=asset.id; hits.push(hit);
    const label = document.createElement('button');label.className='asset-label';label.dataset.asset=asset.id;
    const dot=document.createElement('i'), name=document.createElement('span'), value=document.createElement('b');
    name.textContent=asset.name;value.textContent='—';label.append(dot,name,value);label.setAttribute('aria-label',`Inspect ${asset.name}`);
    label.addEventListener('click',()=>onSelect(asset.id));labelHost.append(label);
    labels.set(asset.id,{label,dot,value});assets.set(asset.id,{g,ring,lamp,parts,p});
  }
  const flows=[];
  function route(from,to,points,color) {
    const curve=pipe(points,color,.07), dots=[];
    for (let i=0;i<5;i++) {const dot=mesh(new THREE.SphereGeometry(.105,8,8),color,0,0,0,world,{emissive:color,emissiveIntensity:.3});dots.push(dot);}
    flows.push({from,to,curve,dots});
  }
  route('malt','mash01',[[-10,1.1,-4],[-8,1.1,-4],[-8,1.1,-.5],[-5.1,1.1,-.5]],0xbda26b);
  route('mash01','boil01',[[-5.1,.65,.2],[-3,.65,.2],[-2.5,.65,-.5],[0,.65,-.5]],0xbda26b);
  route('boil01','ferm01',[[0,.6,-1.3],[0,.6,-4.6],[5,.6,-4.6],[5,.6,-3.2]],0xbda26b);
  route('ferm01','bright',[[5,.7,-2.4],[7.5,.7,-2.4],[10,.7,-2.4]],0xc4a858);
  route('bright','fill01',[[10,.65,-3.2],[12.2,.65,-3.2],[12.2,.65,4.2],[8,.65,4.2]],0xc4a858);
  pipe([[-4.6,.4,5.2],[-7,.4,5.2],[-7,.4,-6],[8,.4,-6]],0x77a3b4,.045);
  pipe([[-5.1,.4,-.5],[-7.5,.4,-.5],[-7.5,.4,2.8],[-10,.4,2.8]],0x9b9e71,.045);
  const raycaster=new THREE.Raycaster(), pointer=new THREE.Vector2();let down=null, showLabels=true, isTop=false;
  const onDown=e=>{down={x:e.clientX,y:e.clientY};};
  const onUp=e=>{
    if (!down || Math.hypot(e.clientX-down.x,e.clientY-down.y)>5) return;
    const r=renderer.domElement.getBoundingClientRect();pointer.set((e.clientX-r.left)/r.width*2-1,-(e.clientY-r.top)/r.height*2+1);
    raycaster.setFromCamera(pointer,camera);const hit=raycaster.intersectObjects(hits,false)[0];if(hit)onSelect(hit.object.userData.asset);
  };
  renderer.domElement.addEventListener('pointerdown',onDown);renderer.domElement.addEventListener('pointerup',onUp);
  function resize() {
    const w=host.clientWidth,h=host.clientHeight;if(!w||!h)return;
    const aspect=w/h, size=aspect<1?24:14.4;
    camera.left=-size*aspect;camera.right=size*aspect;camera.top=size;camera.bottom=-size;camera.updateProjectionMatrix();renderer.setSize(w,h,false);
  }
  const observer=new ResizeObserver(resize);observer.observe(host);resize();
  function home() {isTop=false;camera.position.set(27,24,32);camera.zoom=1;controls.target.set(0,.4,0);camera.updateProjectionMatrix();controls.update();}
  home();
  const position=new THREE.Vector3();
  function render(store,selected,motion,animate=true) {
    const now=Date.now();controls.update();
    for (const [id,visual] of assets) {
      const asset=store.assets.get(id), status=store.status(asset,now), active=status==='running'&&animate, color=STATUS[status];
      visual.ring.visible=selected===id;visual.lamp.material=mat(color,{emissive:color,emissiveIntensity:.2});
      const label=labels.get(id);label.label.classList.toggle('selected',selected===id);label.dot.className=status;
      label.label.setAttribute('aria-pressed',String(selected===id));
      const metric=primaryMetric(asset),primary=asset.values.get(metric.key);
      const unit=metric.key==='bottleCnt'?'bottles/min':metric.unit;
      const reading=primary?`${Number(primary.value.toFixed(1))} ${unit}`:'—';
      if(label.value.textContent!==reading)label.value.textContent=reading;
      label.value.classList.toggle('muted',primary&&!store.current(primary,now));
      position.set(visual.p.x,visual.p.h+1.15,visual.p.z).project(camera);
      label.label.style.left=`${(position.x*.5+.5)*host.clientWidth}px`;label.label.style.top=`${(-position.y*.5+.5)*host.clientHeight}px`;
      label.label.hidden=!showLabels||Math.abs(position.x)>.94||Math.abs(position.y)>.95;
      // A known level remains visible during an outage; its label is marked stale.
      const level=asset.values.get('level');
      if(visual.parts.liquid) {
        visual.parts.liquid.visible=Boolean(level)&&level.value>0;
        const height=Math.max(.005,visual.parts.bodyHeight*Math.max(0,Math.min(100,level?.value||0))/100);
        visual.parts.liquid.scale.y=height;visual.parts.liquid.position.y=1.28+height/2;
      }
      const val=key=>{const s=asset.values.get(key);return store.current(s,now)?s.value:0;};
      const frame=motion.get(id);
      if(visual.parts.paddle)visual.parts.paddle.rotation.y=frame.stirrer;
      if(visual.parts.progress){
        const s=asset.values.get('boilProg'),fraction=Math.max(0,Math.min(100,s?.value||0))/100,ring=visual.parts.progress;
        ring.visible=Boolean(s)&&fraction>0;
        if(ring.userData.fraction!==fraction){
          geometries.delete(ring.geometry);ring.geometry.dispose();
          ring.geometry=new THREE.TorusGeometry(visual.p.r+.25,.07,8,72,Math.max(.001,fraction*Math.PI*2));geometries.add(ring.geometry);ring.userData.fraction=fraction;
        }
      }
      for(const [i,puff] of (visual.parts.steam||[]).entries()) {
        const t=(frame.steam+i/6)%1;puff.visible=active&&val('boilTemp')>80;
        puff.position.set(.3+Math.sin(i+t*3)*.15,visual.p.h+.9+t*1.3,-.25);puff.scale.setScalar(.5+t*1.7);
      }
      for(const [i,bubble] of (visual.parts.bubbles||[]).entries()) {bubble.visible=active&&val('co2Rate')>0;bubble.position.y=1.4+((frame.gas+i/4)%1)*2.3;}
      for(const [i,bottle] of (visual.parts.bottles||[]).entries())bottle.position.x=-3+((frame.bottles+i)%12)*.5;
      for(const [i,mark] of (visual.parts.belt||[]).entries())mark.position.x=-3+((frame.bottles+i)%12)*.5;
      for(const wheel of visual.parts.wheels||[])wheel.rotation.z=-frame.bottles*Math.PI;
      for(const stream of visual.parts.streams||[]){stream.visible=active&&val('flowRate')>0;stream.scale.x=stream.scale.z=.3+Math.min(30,Math.max(0,val('flowRate')))/30;}
      if(visual.parts.pump)visual.parts.pump.rotation.z=frame.pump;
    }
    for(const flow of flows) {
      const a=store.assets.get(flow.from), b=store.assets.get(flow.to);
      const available=a.group==='machines'?store.status(a,now)==='running':store.current(a.values.get('level'),now)&&a.values.get('level').value>0;
      const moving=animate&&available&&(b.group==='machines'?store.status(b,now)==='running':store.current(b.values.get('level'),now));
      for(const [i,dot] of flow.dots.entries()){dot.visible=moving;if(moving)dot.position.copy(flow.curve.getPointAt((motion.elapsed*.12+i/5)%1));}
    }
    renderer.render(scene,camera);
  }
  function focus(id){const p=LAYOUT[id];isTop=false;controls.target.set(p.x,p.h*.45,p.z);camera.position.set(p.x+14,p.h*.45+12,p.z+16);camera.zoom=Math.min(5,(camera.right-camera.left)/11,(camera.top-camera.bottom)/9);camera.updateProjectionMatrix();controls.update();}
  return {render,home,focus,labels(){showLabels=!showLabels;return showLabels;},top(){isTop=!isTop;camera.zoom=1;camera.updateProjectionMatrix();camera.position.set(isTop?.01:27,isTop?42:24,isTop?.01:32);controls.target.set(0,.4,0);controls.update();return isTop;},dispose(){observer.disconnect();controls.dispose();renderer.domElement.removeEventListener('pointerdown',onDown);renderer.domElement.removeEventListener('pointerup',onUp);for(const g of geometries)g.dispose();for(const m of materials.values())m.dispose();for(const t of textures)t.dispose();renderer.dispose();renderer.domElement.remove();labelHost.replaceChildren();}};
}
