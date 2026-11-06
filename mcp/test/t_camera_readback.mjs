import {fileURLToPath} from 'node:url';
import {mkdtempSync,readdirSync,readFileSync,realpathSync,rmSync} from 'node:fs';
import {dirname,isAbsolute,join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const intendedTempParent=realpathSync(resolve(tmpdir())),profiles=mkdtempSync(join(intendedTempParent,'camera-readback-profile-')),createdProfileReal=realpathSync(profiles);
const t=await startFake({}, {VINPUT_EXE:fileURLToPath(new URL('./fake_camera_readback.mjs',import.meta.url)),VINPUT_CAMERA_PROFILE_DIR:profiles,VINPUT_MCP_COMPACT:'false'});
let serial=0;
const observe={timeout_ms:1500,quiet_ms:100,stable_samples:2,poll_ms:50};
const base={pid,yaw_deg:20,counts_per_degree:5,capture_after:0,observe_motion:observe,timeout_ms:10000};
const scene=options=>({readback:{case_id:String(++serial),...options}});
const reset=options=>{t.setScenario(scene(options));t.clearCalls();};
const requests=()=>t.callsOf('json').map(argv=>JSON.parse(cmdOf(argv)[1]));
const moves=()=>requests().filter(q=>q.cmd==='move');
const frames=()=>t.timedCalls().filter(c=>c.event==='modeled_frame'),post=()=>frames().filter(c=>c.post_move);
const noReplay=()=>moves().length===1&&moves()[0].raw===true&&moves()[0].steps===1&&typeof moves()[0].expected_capture_scope==='string'&&!requests().some(q=>['release','job-cancel','key','sequence'].includes(q.cmd));
function retained(r){return !r.isError&&r.json?.ok===true&&r.json.input_receipt?.capture_scope_checked===true&&r.json.input_receipt.raw_dx===100&&r.json.camera_angle_measured===false;}
function noPng(r){return !r.r.content.some(c=>c.type==='image')&&!t.callsOf('capture').length&&!requests().some(q=>q.path)&&!readdirSync(t.dir).some(f=>/\.(png|jpe?g)$/i.test(f));}
function witness(ok,name,r){if(!ok)console.error('CAMERA_READBACK_DIAGNOSTIC '+JSON.stringify({reply:r.json,frames:frames(),requests:requests()}));check(ok,name);}
try{
 const list=(await t.client.listTools()).tools;
 check(['look_turn','turn','look'].every(n=>list.find(x=>x.name===n)?.inputSchema.properties.observe_motion),'actual MCP exposes advisory motion readback on all angular tools');
 reset({onset_ms:350,smoothing_ms:150});let r=await t.call('look_turn',base),o=r.json?.camera_observation;
 witness(retained(r)&&o?.outcome==='settled'&&o.onset_observed&&o.post_input_pixels_settled&&o.quiet_observed_ms>=100&&o.quiet_fresh_samples>=2,'delayed onset followed by smoothing and native quiet preserves one known move',r);
 const pre=post().filter(f=>f.phase==='A'),last=post().at(-1),onset=post().find(f=>f.phase!=='A');
 witness(pre.length>=2&&onset&&last&&last.modeled_elapsed_ms>=500&&o.onset_capture_qpc_ticks===onset.capture_qpc_ticks,'multiple unchanged pre-onset frames cannot settle before the modeled later response',r);
 check(frames().every((f,n,a)=>!n||BigInt(f.capture_qpc_ticks)>BigInt(a[n-1].capture_qpc_ticks))&&post().every(f=>f.modeled_elapsed_ms>=0)&&noReplay(),'fake native clock follows actual monotonic elapsed time and the adapter sends exactly one scoped raw move');
 check(noPng(r)&&o.actual_angle===null&&!o.input_consumption_measured&&!o.queue_drain_measured,'capture_after0 writes no PNG while advisory never invents angle, consumption or drain');

 reset({onset_ms:0,smoothing_ms:100});r=await t.call('look_turn',{...base,observe_motion:{...observe,max_change_pct:1,pixel_tolerance:8}});o=r.json?.camera_observation;
 witness(retained(r)&&o?.metric==='rgb_grid'&&o.settled&&o.onset_observed&&noReplay(),'RGB readback requests a comparable motion grid in its pre-input baseline and post-input frames',r);

 reset({static:true});r=await t.call('look_turn',{...base,observe_motion:{...observe,timeout_ms:900}});o=r.json?.camera_observation;
 witness(retained(r)&&o?.outcome==='no_change'&&!o.onset_observed&&!o.settled&&o.pixel_change_observed===false&&o.do_not_repeat&&noReplay(),'static/dark unchanged view is advisory no-change, not failed or retried known input',r);

 for(const [fault,reason] of [['repeat_clock','capture_clock_not_advanced'],['regress_clock','capture_clock_not_advanced'],['repeat_frame','native_source_frame_not_advanced'],['frequency','capture_frequency_changed'],['scope','observation_unavailable'],['capture','observation_unavailable'],['transport','observation_unavailable']]){
  reset({fault,onset_ms:0,smoothing_ms:0});r=await t.call('look_turn',{...base,observe_motion:{...observe,timeout_ms:900}});o=r.json?.camera_observation;
  witness(retained(r)&&o?.outcome==='unavailable'&&o.stop_reason===reason&&!o.settled&&o.actual_angle===null&&o.do_not_repeat&&noReplay(),`${fault} after known ACK stays qualified unavailable without input replay/inverse`,r);
 }

 reset({printwindow:true,onset_ms:0,smoothing_ms:0});r=await t.call('look_turn',base);o=r.json?.camera_observation;
 witness(retained(r)&&o?.settled&&o.frame_freshness==='unmeasured_printwindow'&&o.native_source_frame_progress_observed===null&&noReplay(),'PrintWindow readback stays pixel-only with unknown native/render freshness',r);

 reset({static:true});r=await t.call('look_turn',{...base,observe_motion:false});
 check(retained(r)&&!r.json.camera_observation&&post().length===0&&noReplay()&&noPng(r),'explicit observe_motion false opts out without changing one raw move or capture_after0');
 reset({});r=await t.call('look_turn',{...base,yaw_deg:0});
 check(!r.isError&&r.json.ok&&r.json.input_injected===false&&!moves().length&&!r.json.camera_observation&&noPng(r),'zero-degree request emits no motion and never waits for a nonexistent onset');

 reset({static:true});r=await t.call('look_turn',{...base,compact:true,observe_motion:{...observe,timeout_ms:900}});o=r.json?.camera_observation;
 witness(retained(r)&&r.json.compact===true&&o?.outcome==='no_change'&&o.actual_angle===null&&o.do_not_repeat&&noReplay()&&noPng(r),'compact reply retains no-change/unknown-angle advisory and original scoped move receipt',r);

 reset({});r=await t.call('look_calibrate',{pid,mode:'counts',counts_per_degree:5,pitch_counts_per_degree:-3,game:'readback-profile',capture_after:0});
 check(!r.isError&&r.json.ok&&r.json.input_injected===false&&!moves().length&&readdirSync(profiles).length===1&&JSON.parse(readFileSync(r.json.calibration.path,'utf8')).yaw===5,'counts profile saves exact modeled process birth/window without any probe');
 reset({onset_ms:0,smoothing_ms:100});r=await t.call('turn',{pid,degrees:20,game:'readback-profile',capture_after:0,observe_motion:observe,timeout_ms:10000});o=r.json?.camera_observation;
 witness(retained(r)&&o?.settled&&r.json.actual_yaw_deg===null&&noReplay()&&noPng(r),'saved-profile turn uses one100-count raw move then qualified readback',r);
 reset({onset_ms:0,smoothing_ms:100});r=await t.call('look',{pid,yaw:20,pitch:10,game:'readback-profile',capture_after:0,observe_motion:observe,timeout_ms:10000});o=r.json?.camera_observation;
 witness(retained(r)&&o?.settled&&moves()[0].dy===-30&&r.json.actual_pitch_deg===null&&noReplay()&&noPng(r),'saved yaw/pitch profile sends one combined move and never presents pixel quiet as angle measurement',r);

 reset({bad_ack:true});r=await t.call('look_turn',base);
 check(r.isError&&!r.json.ok&&r.json.motion_may_have_been_applied===true&&moves().length===1&&post().length===0&&!r.json.camera_observation,'legacy malformed motion ACK remains uncertain; advisory readback cannot bless or retry it');
}finally{
 await t.close();
 const resolvedProfile=resolve(profiles),realProfile=realpathSync(resolvedProfile),realParent=realpathSync(dirname(resolvedProfile));
 const same=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
 if(!isAbsolute(resolvedProfile)||!same(resolvedProfile,realProfile)||!same(realProfile,createdProfileReal)||!same(realParent,intendedTempParent)||!same(dirname(realProfile),intendedTempParent))throw Error('refused profile cleanup: exact created directory no longer resolves directly inside its captured intended temp parent');
 rmSync(realProfile,{recursive:true,force:true});
}
console.log(`CAMERA_READBACK_RESULT ${JSON.stringify({completed:true,passed:state.passes,failed:state.failures,scope:'actual MCP protocol against modeled CLI receipts/clocks; no native game/input/rendered-camera proof'})}`);
console.log(`camera readback: ${state.passes} PASS / ${state.failures} FAIL`);
if(state.failures)process.exitCode=1;
