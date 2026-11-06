import {makeChecker,startFake,fullStatus,cmdOf} from './lib.mjs';
import {decodePng} from '../build/png.js';
const {check,state}=makeChecker(),pid=process.pid;
const scene={png:{w:1280,h:720,core_capture:true},replies:{status:fullStatus(),mousedown:{ok:true,receipt:'down'},mouse:{ok:true,receipt:'move'},mouseup:{ok:true,receipt:'up'},scroll:{ok:true,receipt:'wheel'},'placement-drag':{ok:true,receipt:'placement'}}};
const t=await startFake(scene),image={capture_after:'immediate',region:{x:100,y:40,w:320,h:180},max_width:160};
const drag={pid,x1:20,y1:30,x2:40,y2:50,ms:1,steps:1,hold_start_ms:0,hold_end_ms:0};
const flag=(a,k)=>{const i=a.indexOf(k);return i<0?undefined:a[i+1];};
try{
 for(const [name,args] of [['drag',drag],['drag',{...drag,placement:true}],['scroll',{pid,notches:2}]]){
  t.clearCalls();const r=await t.call(name,{...args,...image}),cap=t.callsOf('capture')[0]??[];
  const im=r.image?decodePng(Buffer.from(r.image.data,'base64')):undefined;
  check(!r.isError&&im?.width===160&&im.height===90&&flag(cap,'--region')==='100,40,320,180'&&flag(cap,'--max-width')==='160',`${name}${args.placement?' placement':''} uses shared crop/size capture options`);
  check(r.json.capture_after.image.offset_x===100&&r.json.capture_after.image.scale===.5&&t.callsOf('capture').length===1,`${name} retains one post-action image and its client mapping`);
 }
 t.setScenario({...scene,replies:{...scene.replies,capture:{ok:false,error:'modeled capture failure'}}});t.clearCalls();
 let r=await t.call('drag',{...drag,...image});check(!r.isError&&r.json.ok&&r.json.capture_after_error&&r.json.down.receipt==='down'&&r.json.up.receipt==='up'&&t.callsOf('mousedown').length===1&&t.callsOf('mouse').length===1&&t.callsOf('mouseup').length===1,'drag capture failure preserves successful input, release receipt and single action sequence');
 t.clearCalls();r=await t.call('scroll',{pid,notches:2,...image});check(!r.isError&&r.json.receipt==='wheel'&&r.json.capture_after_error&&t.callsOf('scroll').length===1,'scroll capture failure remains advisory after its one wheel action');
 for(const [name,args] of [['drag',drag],['scroll',{pid,notches:2}]]){t.clearCalls();r=await t.call(name,{...args,region:image.region});check(r.isError&&!t.calls().length,`${name} invalid image-only options refuse before input`);}
}finally{await t.close();}
console.log(`drag scroll capture: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
