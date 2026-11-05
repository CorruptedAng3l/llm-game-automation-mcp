import {motionSample,motionDifference} from '../build/motion.js';
import {holdUntilChange} from '../build/camerahelpers.js';
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),pid=4242,owner='motion-test',id=`${pid}:20:1`;
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600};
const grid=rgb=>({motion_sample_version:1,motion_sample_w:2,motion_sample_h:1,motion_sample_rgb:rgb});
let d=motionDifference(motionSample(grid('000000000000')),motionSample(grid('FFFFFF000000')));
check(d.changed_pct===50&&d.mean_abs_rgb_diff_pct===50,'known RGB grid has exact changed area and mean difference');
check(!motionSample(grid('not hex'))&&!motionDifference(undefined,motionSample(grid('000000000000'))),'missing or malformed samples are unavailable, not zero motion');
function fixture(options={}){
 let held=false,hashReads=0,keys=0,postHashes=0;const calls=[];
 const evidence={version:1,terminal:true,frozen:true,started:true,kind:'keys',keys:[{vk:87,message_dispatches:'1',down_returns:'0'}]};
 const job=(done=false)=>({ok:done,accepted:false,job_id:id,owner,requested_ms:100,capture_scope_checked:true,job_state:done?'cancelled':options.queued?'queued':'running',job_complete:done,hold_ready:!done&&(!options.queued||options.badQueued),input_read_observation:evidence});
 const ctx={owner,at:()=>[],epoch:()=>0,captureError:()=>undefined,finish:async(_p,r)=>r,run:async(argv,budget)=>{
  calls.push({argv,budget});let json;
  if(argv[0]==='status')json={ok:true,pid,capture_scope_version:1,key_sequence_version:1,input_scope_version:1};
  else if(argv[0]==='jobs')json={ok:true,jobs:[]};
  else if(argv[0]==='job-status')json={ok:true,job:job()};
  else if(argv[0]==='job-cancel')json={ok:true,job:job(true)};
  else if(argv[0]==='json'){
   const r=JSON.parse(argv[1]);
   if(r.cmd==='key'){held=true;keys++;json={...job(),ok:true,accepted:true,job_state:'queued',hold_ready:false};}
   else{hashReads++;if(held)postHashes++;const noise=options.animate&&hashReads%2;json={ok:true,hash:(noise?'2':held?'3':'1').repeat(16),capture_scope:scope,...(!options.noSample?grid(held?'FFFFFFFFFFFF':noise?'010101000000':'000000000000'):{})};}
  }
  return {ok:json.ok,json,argv,stderr:'',timedOut:false,exitCode:0};
 }};
 return {ctx,calls,get keys(){return keys},get postHashes(){return postHashes}};
}
const args={pid,key:'w',timeout_ms:100,poll_ms:5,baseline_ms:20},read=r=>JSON.parse(r.content[0].text);
let f=fixture({animate:true}),r=read(await holdUntilChange(f.ctx,args));
check(!r.ok&&!r.changed&&!f.keys&&r.baseline_changed_samples>0,'ambient exact-hash animation refuses before any hold is admitted');
f=fixture({animate:true});r=read(await holdUntilChange(f.ctx,{...args,min_change_pct:20,pixel_tolerance:8}));
check(r.ok&&r.hold_started&&r.changed&&f.keys===1&&r.motion_score.changed_pct===100,'tolerant quiet baseline permits one started hold and reports its larger pixel change');
check(r.input_read_observation?.keys[0].message_dispatches==='1'&&r.final_job.job_state==='cancelled','helper retains final per-key reader evidence after exact cancellation');
check(f.calls.filter(c=>c.argv[0]==='json'&&JSON.parse(c.argv[1]).cmd==='frame-hash').every(c=>c.budget>=500),'short remaining observation budgets do not spawn a CLI with a 32ms timeout');
for(const badQueued of [false,true]){f=fixture({queued:true,badQueued});r=read(await holdUntilChange(f.ctx,args));check(!r.ok&&!r.changed&&!r.hold_started&&f.postHashes===0,'queued hold never reports changed even with inconsistent hold_ready='+badQueued);}
f=fixture({noSample:true});r=read(await holdUntilChange(f.ctx,{...args,min_change_pct:5}));check(!r.ok&&!f.keys,'requested tolerant observation refuses an older DLL before input');
console.log(`motion helpers: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
