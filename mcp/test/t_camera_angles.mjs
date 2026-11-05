import {mkdtempSync,readFileSync,writeFileSync,readdirSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
import {cameraCreation,saveAngleProfile,loadAngleProfile} from '../build/camerastorage.js';
const {check,state}=makeChecker(),pid=process.pid,dir=mkdtempSync(join(tmpdir(),'vi-angles-'));
const prior=process.env.VINPUT_CAMERA_PROFILE_DIR;process.env.VINPUT_CAMERA_PROFILE_DIR=dir;
const env={VINPUT_EXE:fileURLToPath(new URL('./fake_camera_angles.mjs',import.meta.url)),VINPUT_CAMERA_PROFILE_DIR:dir};
const native=t=>t.callsOf('json').map(v=>JSON.parse(cmdOf(v)[1]));
const moved=t=>native(t).filter(v=>v.cmd==='move'),keys=t=>native(t).filter(v=>v.cmd==='key');
let t;
try{
 check(cameraCreation({presentation_window:{version:1,process_creation_filetime:'18446744073709551615'}})==='18446744073709551615'&&!cameraCreation({presentation_window:{version:1,process_creation_filetime:'18446744073709551616'}}),'exact process creation remains a bounded decimal string without precision loss');
 t=await startFake({},env);
 const tools=(await t.client.listTools()).tools;check(/\nmove 120 0 --raw\nsleep 150\ncapture shot.png/.test(tools.find(v=>v.name==='run_script').description),'actual tool description exposes line-oriented script grammar and a readable multiline example');
 let r=await t.call('turn',{pid,degrees:60});check(r.isError&&!moved(t).length&&/calibrat/.test(r.text),'missing profile refuses before motion');
 t.clearCalls();r=await t.call('look_calibrate',{pid,mode:'counts',yaw_counts:300,observed_yaw_deg:45,pitch_counts_per_degree:-5,game:'compass-settings-A'});
 check(!r.isError&&r.json.calibration.counts_per_degree===300/45&&r.json.calibration.source==='caller_observed_angle'&&r.json.camera_angle_measured===false&&!moved(t).length&&!keys(t).length,'counts calibration derives signed caller reference and persists without any input');
 const stored=r.json.calibration.path;check(readdirSync(dir).length===1&&JSON.parse(readFileSync(stored,'utf8')).pid===pid,'profile stores exact process/window identity in one file');
 await t.close();t=await startFake({},env);
 r=await t.call('turn',{pid,degrees:90,game:'compass-settings-A',capture_after:0,max_width:160});
 check(!r.isError&&r.json.submitted_dx===600&&r.json.actual_yaw_deg===null&&r.image&&moved(t).length===1,'new MCP server reuses exact-target profile and sends one turn plus image');
 t.clearCalls();r=await t.call('look',{pid,yaw:-45,pitch:10,game:'compass-settings-A'});
 check(!r.isError&&r.json.submitted_dx===-300&&r.json.submitted_dy===-50&&moved(t).length===1,'yaw and pitch use independent signed gains in one scoped move');
 check(moved(t).every(q=>q.raw===true&&q.steps===1&&JSON.parse(q.expected_capture_scope).lifetime_cookie==='3'),'every turn keeps native relative scope enforcement');
 t.clearCalls();r=await t.call('look',{pid,yaw:0,pitch:0});check(!r.isError&&!moved(t).length&&r.json.input_injected===false,'zero requested angles send no movement');
 for(const angles of [{created:'134300000000000001'},{geometry:'1'},{noCreation:true},{owner:true}]){
  t.setScenario({angles});t.clearCalls();r=await t.call('turn',{pid,degrees:60});check(r.isError&&!moved(t).length,'new process/window, missing identity or human ownership refuses old calibration '+JSON.stringify(angles));
 }
 t.setScenario({});t.clearCalls();r=await t.call('turn',{pid,degrees:60,game:'other-game'});check(r.isError&&!moved(t).length,'wrong game/settings label cannot borrow a profile');
 t.clearCalls();r=await t.call('look_calibrate',{pid,mode:'counts',counts_per_degree:12.5});check(!r.isError,'direct signed gain is accepted without a landmark');
 t.clearCalls();r=await t.call('look',{pid,yaw:20,pitch:10});check(r.isError&&!moved(t).length,'missing pitch calibration refuses both axes before input');
 for(const args of [{counts_per_degree:0},{yaw_counts:0,observed_yaw_deg:30},{yaw_counts:30,observed_yaw_deg:0},{yaw_counts:30,observed_yaw_deg:30,counts_per_degree:1},{counts_per_degree:1,template:'x.png'}]){
  t.clearCalls();r=await t.call('look_calibrate',{pid,mode:'counts',...args});check(r.isError&&!moved(t).length&&!keys(t).length,'invalid or ambiguous angular reference refuses before input '+JSON.stringify(args));
 }
 t.setScenario({angles:{badAck:true}});t.clearCalls();r=await t.call('turn',{pid,degrees:60});check(r.isError&&moved(t).length===1&&r.json.motion_may_have_been_applied===true,'uncertain move acknowledgement preserves receipt and never repeats');
 t.clearCalls();r=await t.call('unstick',{pid});check(r.isError&&keys(t).length===1&&moved(t).length===1&&r.json.motion_may_have_been_applied===true&&r.json.input_injection_unknown===true,'uncertain recovery turn keeps generic input uncertainty true and sends no jump/retry');
 t.setScenario({});t.clearCalls();r=await t.call('unstick',{pid,degrees:-60,capture_after:0});
 check(!r.isError&&keys(t).map(v=>v.key).join()==='s,space,w'&&moved(t).length===1&&moved(t)[0].dx===-750&&r.json.steps.length===4&&r.image&&r.json.displacement==='UNKNOWN','one recovery call sends bounded backup/turn/jump/retry and one image without claiming recovery');
 t.clearCalls();r=await t.call('unstick',{pid,jump:false});check(!r.isError&&keys(t).map(v=>v.key).join()==='s,w','explicit no-jump recovery omits only that step');
 t.setScenario({angles:{failKey:true}});t.clearCalls();r=await t.call('unstick',{pid});check(r.isError&&keys(t).length===1&&!moved(t).length&&r.json.steps[0].input_injection_unknown,'uncertain backup stops all later steps and retains the pending-input receipt');
 t.setScenario({});t.clearCalls();r=await t.call('unstick',{pid,degrees:5});check(r.isError&&!keys(t).length&&!moved(t).length,'out-of-bounds recovery refuses before input');
 writeFileSync(stored,'invalid JSON');t.clearCalls();r=await t.call('turn',{pid,degrees:60});check(r.isError&&!moved(t).length,'corrupt stored profile refuses without input or fallback gain');
 writeFileSync(stored,' '.repeat(8193));t.clearCalls();r=await t.call('turn',{pid,degrees:60});check(r.isError&&!moved(t).length&&/exceeds its bound/.test(r.text),'oversized profile is rejected at the bounded file read before input');
}finally{if(t)await t.close();if(prior===undefined)delete process.env.VINPUT_CAMERA_PROFILE_DIR;else process.env.VINPUT_CAMERA_PROFILE_DIR=prior;rmSync(dir,{recursive:true,force:true});}
console.log(`camera angles: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
