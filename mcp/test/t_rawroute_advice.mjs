import {startFake,fullStatus,makeChecker} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const observed={rawmode:1,raw_mouse:true,raw_buffer_routing:{version:1,mouse_thread:42,mouse_recent_buffer_reader:true},raw_registration:{query_ok:true,mouse_target:'0x123',mouse_target_owned:true,fallback_target_owned:true},raw_delivery:{buffer_supported:true,last_backend:'post',recent_buffer_read:false}};
const t=await startFake({replies:{status:fullStatus(observed)}});
try{
 let r=await t.call('status',{pid});check(r.json?.raw_route_advice?.destination_thread===42&&r.json.raw_route_advice.configured_mode==='post'&&/raw:auto.*raw:buffer/.test(r.json.raw_route_advice.message),'owned exact-destination buffer reader exposes explicit-post conflict');
 check(r.json?.raw_route_advice?.mode_changed===false&&r.json.raw_route_advice.input_replayed===false&&r.json.raw_route_advice.application_consumption_measured===false&&t.calls().length===1&&t.callsOf('config').length===0&&t.callsOf('json').length===0,'status advice adds no target query, config change, replay or consumption claim');
 t.clearCalls();r=await t.call('status',{pid,compact:true});check(r.json?.raw_route_advice?.destination_thread===42&&r.json.raw_route_advice.application_consumption_measured===false,'optional global compact presentation keeps exact raw advice scope');
 check(t.calls().length===1&&t.callsOf('config').length===0,'compact advice stays one read-only status call');
 for(const patch of [{rawmode:0},{rawmode:2},{rawmode:'1'},{raw_mouse:false},{raw_delivery:{buffer_supported:false,recent_buffer_read:true}},{raw_buffer_routing:{version:1,mouse_thread:42,mouse_recent_buffer_reader:false}},{raw_buffer_routing:{version:2,mouse_thread:42,mouse_recent_buffer_reader:true}},{raw_buffer_routing:{version:1,mouse_thread:'42',mouse_recent_buffer_reader:true}},{raw_buffer_routing:{version:1,mouse_thread:0,mouse_recent_buffer_reader:true}},{raw_buffer_routing:{version:1,mouse_thread:4294967296,mouse_recent_buffer_reader:true}},{raw_registration:{query_ok:false,mouse_target_owned:true}},{raw_registration:{query_ok:true,mouse_target:'0x123',mouse_target_owned:false,fallback_target_owned:true}}]){
  t.setScenario({replies:{status:fullStatus({...observed,...patch})}});r=await t.call('status',{pid});check(!r.json?.raw_route_advice,'unknown/foreign/global-only/unsupported or deliberately different mode gives no route recommendation');
 }
 t.setScenario({replies:{status:fullStatus({...observed,raw_registration:{query_ok:true,mouse_target:'0x0',mouse_target_owned:false,fallback_target_owned:true}})}});r=await t.call('status',{pid});check(r.json?.raw_route_advice?.destination_thread===42,'owned adopted-window fallback can qualify the actual destination');
}finally{await t.close();}
console.log(`raw route advice: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
