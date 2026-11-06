// Actual MCP against finite fake CLI/capture endpoints. No game or OS input.
import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid,region={x:10,y:20,w:100,h:80};
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const framework={version:1,class:'UnityWndClass',hwnd:'0x1234',window_generation:'2'};
const scene=(camera={},over={})=>({camera:{look:true,...camera},png:{w:800,h:600,core_capture:true},replies:{mousedown:{ok:true},mouse:{ok:true},mouseup:{ok:true,button_released:true},move:{ok:true,relative_accounting_version:1,cursor_clamped:true,raw_dx:600,applied_dx:252,device_dx:600},capture:{capture_scope:scope},...over}});
const env={VINPUT_EXE:fileURLToPath(new URL('./fake_camera.mjs',import.meta.url))};
const t=await startFake(scene(),env),drag={pid,x1:20,y1:30,x2:40,y2:50,ms:1,hold_start_ms:0,hold_end_ms:0,capture_after:false};
const hashCalls=()=>t.callsOf('json').filter(a=>{try{return JSON.parse(cmdOf(a)[1]).cmd==='frame-hash'}catch{return false}});
const witness=(good,label,r)=>{if(!good)console.error('WORKFLOW_DIAGNOSTIC '+JSON.stringify({reply:r,timed_cli_calls:t.timedCalls()}));check(good,label,r.text);};
try{
 let r=await t.call('scroll',{pid,notches:1,settle:{region,poll_ms:50,stable_samples:2,timeout_ms:1000},capture_after:'immediate',max_width:160});
 witness(!r.isError&&r.json.settle_ok&&r.image&&t.callsOf('scroll').length===1&&hashCalls().length>=3&&t.callsOf('capture').length===1,'one wheel settles selected pixels then returns one final image',r);
 t.setScenario(scene({animate:true}));t.clearCalls();r=await t.call('scroll',{pid,notches:1,settle:{region,poll_ms:50,stable_samples:2,timeout_ms:100},capture_after:false});
 check(!r.isError&&r.json.ok&&!r.json.settle_ok&&r.json.settle_warning&&r.json.settle_observation.timed_out&&t.callsOf('scroll').length===1,'wheel pixel timeout is advisory and never repeats the action');
 t.setScenario(scene({wheelRefused:true}));t.clearCalls();r=await t.call('scroll',{pid,notches:1,settle:true,capture_after:'immediate'});
 check(r.isError&&hashCalls().length===0&&t.callsOf('capture').length===0&&t.callsOf('scroll').length===1,'refused wheel is neither replayed nor observed as successful');
 t.setScenario(scene());t.clearCalls();r=await t.call('move_relative',{pid,dx:600,dy:0,capture_after:'immediate',capture_stable:{region,poll_ms:50,stable_samples:2,timeout_ms:1000},max_width:160});
 check(!r.isError&&r.image&&r.json.capture_stability?.ok&&r.json.capture_stability?.final_capture_same_scope===true&&t.callsOf('move').length===1&&t.callsOf('capture').length===1,'completed relative action observes stability before one same-scope image');
 check(/virtual-cursor/.test(r.json.relative_motion_note??'')&&/raw:true/.test(r.json.relative_motion_warning??''),'relative reply distinguishes requested/device/cursor channels and bounded-look guidance');
 t.setScenario(scene({}, {capture:{capture_scope:{...scope,lifetime_cookie:'4'}}}));t.clearCalls();r=await t.call('move_relative',{pid,dx:1,dy:0,capture_after:'immediate',capture_stable:{region,poll_ms:50,stable_samples:2,timeout_ms:1000}});
 check(!r.isError&&r.image&&r.json.capture_stability?.ok===false&&r.json.capture_stability?.final_capture_same_scope===false&&r.json.capture_stability_warning&&t.callsOf('move').length===1,'different final-image lifetime cannot inherit earlier pixel stability');
 for(const [name,args] of [['move_relative',{dx:1,dy:0,capture_after:false}],['key',{key:'w',async:true,capture_after:'immediate'}],['key_sequence',{steps:[{action:'key',key:'w',ms:100}],capture_after:'immediate'}]]){
   t.clearCalls();r=await t.call(name,{pid,...args,capture_stable:true});
   check(r.isError&&t.calls().length===0,`${name} refuses stability without a final image/completed action before input`);
 }
 t.setScenario(scene({framework}));t.clearCalls();r=await t.call('drag',{pid,x1:20,y1:30,x2:40,y2:50,capture_after:false});
 check(!r.isError&&r.json.ms===800&&r.json.hold_start_ms===400&&r.json.hold_end_ms===500&&r.json.timing_profile_used.source==='fresh_owned_window_class'&&t.callsOf('mousedown').length===1&&t.callsOf('mouse').length===1&&t.callsOf('mouseup').length===1,'fresh Unity class selects the measured slower drag preset once');
 t.clearCalls();r=await t.call('drag',drag);
 check(!r.isError&&r.json.ms===1&&r.json.hold_start_ms===0&&r.json.hold_end_ms===0,'explicit drag timings override every automatic default');
 t.setScenario(scene());t.clearCalls();r=await t.call('drag',{...drag,measure_region:region});
 check(!r.isError&&r.json.input_motion?.measured&&r.json.input_motion?.changed_pct===0&&r.json.input_motion?.application_response===null&&r.json.input_motion?.measurement==='region_pixels_only','unchanged pixels across one completed drag do not mean unread or rejected input');
 check(t.callsOf('mousedown')[0]?.includes('--scope-json')&&t.callsOf('mouse')[0]?.includes('--scope-json')&&cmdOf(t.callsOf('mouseup')[0]??[]).length===2,'measured drag fences press/move and releases once without coordinates');
 t.setScenario(scene({}, {mousedown:{ok:false,error:'lost down acknowledgement'}}));t.clearCalls();r=await t.call('drag',drag);
 check(r.isError&&r.json.input_may_have_been_applied&&r.json.cleanup.button_released&&t.callsOf('mousedown').length===1&&t.callsOf('mouse').length===0&&cmdOf(t.callsOf('mouseup')[0]).length===2,'unknown down acknowledgement gets one selected-button cleanup and no move/repeated press');
 t.setScenario(scene({}, {mousedown:{ok:false,input_injected:false,error:'explicit refusal'}}));t.clearCalls();r=await t.call('drag',drag);
 check(r.isError&&r.json.input_injected===false&&t.callsOf('mouseup').length===0,'definite pre-input down refusal does not release unrelated button state');
 t.setScenario(scene({}, {mouse:{ok:false,error:'move fault'},mouseup:{ok:false,error:'release lost'}}));t.clearCalls();r=await t.call('drag',drag);
 check(r.isError&&/unconfirmed/.test(r.json.error)&&r.json.down.ok&&r.json.up.error==='release lost'&&t.callsOf('mouseup').length===1,'failed move and cleanup never claim that the button was released');
 t.setScenario(scene({}, {'placement-drag':{ok:true,job_state:'completed',capture_scope_checked:true}}));t.clearCalls();r=await t.call('drag',{...drag,placement:true,coord_space:'capture',mapping:{scale:.5,offset_x:0,offset_y:0}});
 check(!r.isError&&t.callsOf('placement-drag')[0]?.includes('--scope-json')&&t.callsOf('mousedown').length===0,'mapped placement forwards its original scope to the one native job');
 t.setScenario(scene({}, {capture:{capture_scope:scope,_delay_ms:500}}));t.clearCalls();
 const pending=t.call('move_relative',{pid,dx:1,dy:0,capture_after:'immediate'});const until=Date.now()+3000;
 while(!t.callsOf('capture').length&&Date.now()<until)await new Promise(resolve=>setTimeout(resolve,10));
 check(t.callsOf('capture').length===1,'attachment-race control reaches its one pending final capture');
 await t.call('detach',{pid});r=await pending;
 check(!r.isError&&!r.image&&/attachment changed/.test(r.json.capture_after_error)&&t.callsOf('move').length===1&&t.callsOf('capture').length===1,'attachment epoch change during capture retains the action but excludes the old-session image');
}finally{await t.close()}
const stale=await startFake(scene({scopeAfterFirstHash:true}),env);
try{const r=await stale.call('drag',{...drag,coord_space:'capture',mapping:{scale:.5,offset_x:0,offset_y:0},measure_region:region});check(r.isError&&r.json.input_injected===false&&stale.callsOf('mousedown').length===0,'fresh measurement scope cannot replace the earlier mapped-coordinate fence');}finally{await stale.close()}
console.log(`playtest workflows: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
