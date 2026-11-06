import {fileURLToPath} from 'node:url';
import {CaptureMappings,nativeMapping} from '../build/capturemapping.js';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const transform={scale:0.5,scale_y:0.25,offset_x:100,offset_y:200};
const saved={ok:true,pid,capture_map_version:1,capture_map_id:'18446744073709551615',capture_scope:scope,capture_mapping:{...transform,image_w:100,image_h:50}};
const result=j=>({ok:j.ok,json:j,argv:[],stderr:'',timedOut:false,exitCode:j.ok?0:1});
let reads=0;const getter=async()=>{reads++;return result(saved);};
let cache=new CaptureMappings(),m=await cache.resolve(pid,{coord_space:'capture'},[[10,20]],0,getter,[]);
check(m.pts[0][0]===120&&m.pts[0][1]===280&&m.mapping_used.capture_map_id===saved.capture_map_id&&m.mapping_used.source==='saved_capture','used receipt names the exact uint64 source token and converted coordinates');
check(m.mapping_used.scale===0.5&&m.mapping_used.scale_y===0.25&&m.mapping_used.offset_x===100&&m.mapping_used.offset_y===200&&m.mapping_used.image_w===100&&m.mapping_used.image_h===50&&JSON.stringify(m.mapping_used.capture_scope)===JSON.stringify(scope),'receipt preserves both axis scales, offsets, image dimensions and source window lifetime');
const first=m.mapping_used;cache.remember(pid,{...nativeMapping({...saved,capture_map_id:'2',capture_mapping:{...saved.capture_mapping,scale:0.25}},0,pid)});m=await cache.resolve(pid,{coord_space:'capture'},[[10,20]],0,getter,[]);
check(m.mapping_used.capture_map_id==='2'&&m.mapping_used.scale===0.25&&first.capture_map_id===saved.capture_map_id&&first.scale===0.5&&reads===1,'later captures change future mappings without mutating the earlier receipt or adding reads');
let explicitReads=0;const observer=async()=>{explicitReads++;return result({ok:true,pid,capture_space_updated:false,capture_scope:scope,capture_map_id:'99'});};
m=await cache.resolve(pid,{coord_space:'capture',mapping:{scale:0.5,offset_x:100,offset_y:200}},[[10,20]],0,observer,[]);
check(m.mapping_used.source==='explicit'&&m.mapping_used.capture_map_id===null&&m.mapping_used.scale_y===0.5&&m.mapping_used.image_w===null&&m.mapping_used.image_h===null&&explicitReads===1,'explicit mapping normalizes its actual default Y scale and does not borrow an unrelated native map token');
check(JSON.stringify(m.mapping_used.capture_scope)===JSON.stringify(scope)&&m.scope===JSON.stringify(scope),'explicit receipt uses exactly the already-observed native scope');
m=await cache.resolve(pid,{coord_space:'capture',mapping:transform},[[10,20]],0,observer,[],scope);check(m.mapping_used.capture_map_id===null&&explicitReads===1,'an existing helper scope supplies the receipt without another observation');
cache=new CaptureMappings();const local={mapping:{...transform},scope:JSON.stringify(scope),imageW:200,imageH:100,epoch:0};cache.remember(pid,local);m=await cache.resolve(pid,{coord_space:'capture'},[[10,20]],0,()=>{throw new Error('unexpected extra I/O');},[]);const unpersisted=m.mapping_used;local.mapping.scale=2;
check(unpersisted.capture_map_id===null&&unpersisted.source==='saved_capture'&&unpersisted.scale===0.5&&unpersisted.image_w===200,'unpersisted local mappings report a null token and snapshot their exact conversion');
cache.remember(pid,{mapping:{...transform},epoch:0});m=await cache.resolve(pid,{coord_space:'capture'},[[10,20]],0,getter,[]);check(m.mapping_used.capture_scope===null&&m.mapping_used.capture_map_id===null&&reads===1,'legacy unscoped local mapping reports unknown scope/token without inventing a read or guard');
m=await cache.resolve(pid,{coord_space:'capture'},[[10,20]],0,getter,[],{...scope,geometry_epoch:'1'});check(m.error&&!m.mapping_used&&reads===1,'existing changed-window scope refusal remains intact');
m=await cache.resolve(pid,{},[[10,20]],0,getter,[]);check(!m.mapped&&!m.mapping_used&&reads===1,'client-coordinate actions do not invent a capture mapping receipt');

const native={...saved,capture_map_id:'7'},scenario={png:{w:100,h:50},replies:{json:native,status:{ok:true,client_w:800,client_h:600},click:{ok:true}}};
const t=await startFake(scenario,{VINPUT_MCP_COMPACT:'false'});
try{
 let r=await t.call('click',{pid,x:10,y:20,coord_space:'capture',edge_margin:0,hover_ms:0,hold_ms:1});let args=cmdOf(t.callsOf('click')[0]);
 check(!r.isError&&r.json.mapping_used.capture_map_id==='7'&&r.json.mapping_used.scale_y===0.25&&args[2]==='120'&&args[3]==='280'&&args.includes('--scope-json')&&t.callsOf('json').length===1&&t.callsOf('click').length===1,'actual click returns its used source map with the same one scoped native action and one map read');
 t.clearCalls();r=await t.call('move',{pid,x:10,y:20,coord_space:'capture',edge_margin:0,compact:true});args=cmdOf(t.callsOf('mouse')[0]);
 check(!r.isError&&r.json.compact&&r.json.mapping_used.capture_map_id==='7'&&r.json.mapping_used.capture_scope.lifetime_cookie==='99'&&args[1]==='120'&&args[2]==='280'&&args.includes('--scope-json')&&!t.callsOf('json').length,'move and compact presentation preserve the complete used receipt without more reads');
 t.clearCalls();r=await t.call('drag',{pid,x1:10,y1:20,x2:30,y2:40,coord_space:'capture',edge_margin:0,hold_start_ms:0,hold_end_ms:0,ms:1,steps:1,compact:true});
 check(!r.isError&&r.json.compact&&r.json.mapping_used.capture_map_id==='7'&&r.json.mapped.x1===120&&r.json.mapped.y2===360&&t.callsOf('mousedown').length===1&&t.callsOf('mouse').length===1&&t.callsOf('mouseup').length===1&&!t.callsOf('json').length,'compact drag echoes the shared endpoint transform without additional input or map queries');
 t.setScenario({png:{w:100,h:50},replies:{capture:{...native,capture_map_id:'8',w:100,h:50,method:'swapchain',capture_mapping:{...native.capture_mapping,scale:1,scale_y:0.5,offset_x:10,offset_y:20}},click:{ok:true}}});t.clearCalls();
 r=await t.call('click',{pid,x:10,y:20,coord_space:'capture',edge_margin:0,hover_ms:0,hold_ms:1,capture_after:'immediate'});
 check(!r.isError&&r.image&&r.json.mapping_used.capture_map_id==='7'&&r.json.mapping_used.scale===0.5&&r.json.mapped.x===120&&t.callsOf('click').length===1&&t.callsOf('capture').length===1,'capture_after cannot replace the action receipt with the newer capture transform');
 t.clearCalls();r=await t.call('click',{pid,x:10,y:20,coord_space:'capture',edge_margin:0,hover_ms:0,hold_ms:1});
 check(r.json.mapping_used.capture_map_id==='8'&&r.json.mapping_used.scale===1&&r.json.mapped.x===20&&r.json.mapped.y===60&&!t.callsOf('json').length,'the next deliberate action separately reports the newer remembered map');
 t.setScenario({replies:{json:{...native,capture_space_updated:false},click:{ok:true}}});t.clearCalls();r=await t.call('click',{pid,x:10,y:20,coord_space:'capture',mapping:transform,edge_margin:0,hover_ms:0,hold_ms:1,compact:true});
 check(!r.isError&&r.json.mapping_used.source==='explicit'&&r.json.mapping_used.capture_map_id===null&&r.json.mapping_used.scale_y===0.25&&t.callsOf('json').length===1&&JSON.parse(cmdOf(t.callsOf('json')[0])[1]).cmd==='frame-hash'&&t.callsOf('click').length===1,'explicit actual MCP action reports null source ID while preserving its one existing fresh-scope observation');
}finally{await t.close();}
const fallback=await startFake({png:{w:100,h:50},replies:{capture:{...native,w:100,h:50,method:'swapchain'},json:{ok:false,error:'modeled lost resize CAS'}}});
try{
 let r=await fallback.call('capture',{pid,zoom:2});check(!r.isError&&r.json.capture_mapping_persisted===false,'unpersisted host-resize positive control retains its existing warning');
 fallback.clearCalls();r=await fallback.call('click',{pid,x:10,y:20,coord_space:'capture',edge_margin:0,hover_ms:0,hold_ms:1});
 check(!r.isError&&r.json.mapping_used.capture_map_id===null&&r.json.mapping_used.scale===1&&r.json.mapping_used.scale_y===0.5&&r.json.mapping_used.capture_scope.geometry_epoch==='0'&&!fallback.callsOf('json').length,'lost resize publication reports the used local transform with null native token, no guessing or new read');
}finally{await fallback.close();}
const loops=await startFake({png:{w:100,h:50},replies:{capture:{...native,capture_map_id:'9',w:100,h:50,method:'swapchain'}}},{VINPUT_EXE:fileURLToPath(new URL('./fake_agent_loops.mjs',import.meta.url))});
const requests=()=>loops.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])),actions=()=>requests().filter(r=>['click','mouse','key'].includes(r.cmd));
try{
 await loops.call('capture',{pid,include_image:false});loops.clearCalls();let r=await loops.call('click_many',{pid,points:[[10,20],[30,40]],coord_space:'capture',hold_ms:1,hover_ms:0,timeout_ms:10000,compact:true});
 check(!r.isError&&r.json.mapping_used.capture_map_id==='9'&&r.json.mapping_used.scale_y===0.25&&r.json.points[0].client.x===120&&actions().length===2&&actions().every(a=>a.expected_capture_scope&&a.admit_before_qpc_ticks)&&requests().filter(a=>a.cmd==='frame-hash').length===1,'compact batch shares one exact source receipt while all original scope/deadline guards and action counts stay intact');
 loops.clearCalls();r=await loops.call('click_grid',{pid,x0:10,y0:20,dx:10,dy:0,cols:2,rows:1,coord_space:'capture',hold_ms:1,hover_ms:0,timeout_ms:10000});
 check(!r.isError&&r.json.mapping_used.capture_map_id==='9'&&r.json.points[1].client.x===140&&actions().length===2,'grid alias reports the same source map for every converted point');
 loops.clearCalls();r=await loops.call('hover_many',{pid,points:[[10,20]],coord_space:'capture',mapping:transform,hover_ms:0,timeout_ms:10000});
 check(!r.isError&&r.json.mapping_used.capture_map_id===null&&r.json.mapping_used.source==='explicit'&&r.json.mapping_used.capture_scope.hwnd===scope.hwnd&&actions().length===1&&requests().filter(a=>a.cmd==='frame-hash').length===2,'hover batch reuses its existing observed scope for an explicit receipt without an extra capture');
 loops.clearCalls();r=await loops.call('click_to_move',{pid,x:10,y:20,coord_space:'capture',region:{x:0,y:0,w:100,h:100},hold_ms:1500,timeout_ms:7000,poll_ms:50,stable_samples:2});
 check(!r.isError&&r.json.mapping_used.capture_map_id==='9'&&r.json.mapping_used.offset_x===100&&actions().filter(a=>a.cmd==='mouse').length===1&&actions().filter(a=>a.cmd==='key').length===1,'bounded click-to-move keeps its conversion receipt alongside the one original move/hold');
 loops.setScenario({capture_patches:{0:{capture_scope:{...scope,geometry_epoch:'1'}}}});loops.clearCalls();r=await loops.call('click_many',{pid,points:[[10,20]],coord_space:'capture',timeout_ms:10000});
 check(r.isError&&!actions().length&&r.json.error.includes('different window lifetime/geometry'),'changed native window geometry still refuses the batch before any coordinate input');
}finally{await loops.close();}
console.log(`mapping used: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
