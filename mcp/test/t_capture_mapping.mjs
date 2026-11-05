import {CaptureMappings,nativeMapping} from '../build/capturemapping.js';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const saved={ok:true,pid,capture_map_version:1,capture_map_id:'18446744073709551615',capture_scope:scope,capture_mapping:{scale:0.5,scale_y:0.25,offset_x:100,offset_y:200,image_w:100,image_h:50}};
const result=j=>({ok:j.ok,json:j,argv:[],stderr:'',timedOut:false,exitCode:j.ok?0:1});
let calls=0;const getter=async()=>{calls++;return result(saved);};
let cache=new CaptureMappings(),m=await cache.resolve(pid,{coord_space:'capture'},[[10,20]],0,getter,[]);
check(m.pts?.[0][0]===120&&m.pts?.[0][1]===280&&!!m.scope&&calls===1,'fresh client retrieves native crop offset and separate axis scales');
m=await cache.resolve(pid,{coord_space:'capture'},[[20,10]],0,getter,[]);check(m.pts?.[0][0]===140&&calls===1,'same attachment reuses confirmed mapping');
m=await cache.resolve(pid,{coord_space:'capture'},[[1,1]],1,getter,[]);check(calls===2&&!!m.pts,'attachment generation never reuses an old local entry');
m=await cache.resolve(pid,{coord_space:'capture'},[[1,1]],1,getter,[],{...scope,geometry_epoch:'1'});check(!!m.error,'changed window geometry refuses mapping');
cache.clear(pid);m=await cache.resolve(pid,{coord_space:'capture'},[[1,1]],1,async()=>result({...saved,pid:pid+1}),[]);check(!!m.error,'wrong target identity cannot supply a persisted mapping');
for(const patch of [{capture_map_version:2},{capture_map_id:'18446744073709551616'},{capture_scope:{...scope,lifetime_cookie:'0'}},{capture_mapping:{...saved.capture_mapping,scale_y:0}}])check(!nativeMapping({...saved,...patch},0,pid),'malformed native mapping is unavailable');
const transform={scale:0.5,scale_y:0.25,offset_x:100,offset_y:200};
const observation={ok:true,pid,capture_space_updated:false,capture_scope:scope};
let observedRequests=[];const observer=async(argv)=>{observedRequests.push(JSON.parse(argv.at(-1)));return result(observation);};
m=await cache.resolve(pid,{coord_space:'capture',mapping:transform},[[10,20]],1,observer,[]);
check(m.pts?.[0][0]===120&&m.pts?.[0][1]===280&&m.scope===JSON.stringify(scope)&&observedRequests.length===1&&observedRequests[0].cmd==='frame-hash'&&observedRequests[0].observation_only===true,'explicit transform is preserved while one fresh observation supplies its native scope');
m=await cache.resolve(pid,{coord_space:'capture',mapping:transform},[[10,20]],1,observer,[],scope);
check(m.scope===JSON.stringify(scope)&&observedRequests.length===1,'helper baseline scope binds explicit mapping without an extra observation');
m=await cache.resolve(pid,{coord_space:'capture',mapping:transform},[[10,20]],1,observer,[],{});
check(!!m.error&&observedRequests.length===1,'malformed expected scope refuses before observation');
for(const patch of [{ok:false},{pid:pid+1},{capture_space_updated:true},{capture_scope:{...scope,lifetime_cookie:'0'}},{}]){
 const value=Object.keys(patch).length?{...observation,...patch}:{ok:true};
 m=await cache.resolve(pid,{coord_space:'capture',mapping:transform},[[10,20]],1,async()=>result(value),[]);
 check(!!m.error&&!m.pts,'unavailable, wrong-target or malformed fresh scope cannot admit explicit mapping');
}
for(const mapping of [{...transform,scale:Infinity},{...transform,scale_y:0},{...transform,offset_x:NaN},{...transform,scale:1e-100}]){
 m=await cache.resolve(pid,{coord_space:'capture',mapping},[[10,20]],1,observer,[]);
 check(!!m.error&&observedRequests.length===1,'invalid explicit transform refuses without native I/O');
}
const changed={...scope,geometry_epoch:'1'};
m=await cache.resolve(pid,{coord_space:'capture',mapping:transform},[[10,20]],1,async()=>result({...observation,capture_scope:changed}),[]);
check(m.scope===JSON.stringify(changed),'each explicit mapping call binds to its new observation rather than a cached scope');
const scenario={replies:{json:saved,status:{ok:true,client_w:800,client_h:600},click:{ok:true}}};
for(let i=0;i<2;i++){
 const t=await startFake(scenario);try{
  const r=await t.call('click',{pid,x:10,y:20,coord_space:'capture',edge_margin:0,hover_ms:0});const args=cmdOf(t.callsOf('click')[0]??[]);
  check(!r.isError&&args[2]==='120'&&args[3]==='280'&&args.includes('--scope-json')&&t.callsOf('capture').length===0&&t.callsOf('json').length===1,`independent MCP process ${i+1} retrieves target map before one fenced click`,r.text);
 }finally{await t.close();}
}
const t=await startFake({png:{w:100,h:50},replies:{capture:{...saved,w:100,h:50,method:'swapchain'},json:{...saved,capture_map_id:'2',capture_mapping:{...saved.capture_mapping,scale:1,scale_y:0.5,image_w:200,image_h:100}}}});
try{
 let r=await t.call('capture',{pid,zoom:2});const writes=t.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1]));
 check(!r.isError&&r.json.mapping.scale===1&&r.json.mapping.scale_y===0.5&&r.json.capture_mapping_persisted===true,'host enlargement composes native per-axis scales and confirms persistence',r.text);
 check(writes.length===1&&writes[0].cmd==='capture-map-resize'&&writes[0].image_w===200&&writes[0].image_h===100&&writes[0].capture_map_id===saved.capture_map_id,'postprocess publishes only dimensions with exact map identity and scope');
 t.clearCalls();r=await t.call('capture',{pid,include_image:false,zoom:2});check(r.isError&&/preview/.test(r.text)&&t.callsOf('capture').length===0&&t.callsOf('json').length===0,'file-only preview zoom refuses explicitly before capture or mapping mutation',r.text);
 t.clearCalls();r=await t.call('capture',{pid,include_image:false});check(!r.isError&&r.json.mapping.scale===0.5&&r.json.capture_mapping_persisted===true&&t.callsOf('json').length===0,'plain file-only capture still remembers the exact saved native image mapping',r.text);
 t.setScenario({png:{w:100,h:50},replies:{capture:{...saved,w:100,h:50,method:'swapchain'},json:{ok:false,error:'map changed'}}});t.clearCalls();r=await t.call('capture',{pid,zoom:2});check(!r.isError&&r.json.capture_mapping_persisted===false&&r.json.capture_mapping_warning&&t.callsOf('capture').length===1&&t.callsOf('json').length===1,'lost map CAS is explicit and never replays capture or input',r.text);
}finally{await t.close();}
const explicit=await startFake({replies:{json:observation,click:{ok:true}}});
try{
 let r=await explicit.call('click',{pid,x:10,y:20,coord_space:'capture',mapping:transform,edge_margin:0,hover_ms:0});
 let requests=explicit.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1]));const args=cmdOf(explicit.callsOf('click')[0]??[]);
 check(!r.isError&&args[2]==='120'&&args[3]==='280'&&args.includes('--scope-json')&&requests.length===1&&requests[0].cmd==='frame-hash'&&requests[0].observation_only===true,'actual MCP explicit mapping acquires one observation-only scope before one fenced click',r.text);
 explicit.setScenario({replies:{json:{ok:true}}});explicit.clearCalls();
 r=await explicit.call('click',{pid,x:10,y:20,coord_space:'capture',mapping:transform,edge_margin:0,hover_ms:0});
 check(r.isError&&explicit.callsOf('click').length===0&&explicit.callsOf('json').length===1,'actual MCP missing fresh scope sends no coordinate input',r.text);
}finally{await explicit.close();}
console.log(`capture mapping: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
