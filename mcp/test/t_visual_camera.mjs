const { observeSettle, lookCalibrate, lookAt, lookTurn } = await import(process.env.CAMERA_HELPER_MODULE ?? '../build/camerahelpers.js');
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),pid=4242,owner='unit-camera';
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600};
const matrix={xx:-.5,xy:0,yx:0,yy:-.5},calibration={matrix,capture_scope:scope};
function fixture(options={}) {
 const calls=[];let epoch=0,moves=0,reads=0,finishOptions,finishEpoch;let point=[300,250];
 const ctx={owner,at:p=>['--pid',String(p),'--owner',owner],epoch:()=>epoch,captureError:()=>undefined,
 finish:async(p,out,a,e)=>{finishOptions=a;finishEpoch=e;return out},run:async(argv,budget)=>{
  const a=argv[0]==='help'?argv:argv.slice(4),cmd=a[0];calls.push({argv,budget});let json;
  if(cmd==='help')json={ok:true,image_scope_version:options.oldCli?undefined:1};
  else if(cmd==='status')json={ok:true,pid,capture_scope_version:1,relative_scope_version:options.oldDll?undefined:1,owner_active:options.ownerActive===true,cursor_guard:{owner_active:options.nestedOwner===true||options.ownerAfterMove&&moves>0}};
  else if(cmd==='find-image'){
   const count=options.ambiguous?2:options.missingAfter&&moves?0:1;
   json={ok:count>0,match_count:count,matches:count?[{center:options.badCenter?[800,20]:point,score:.99}]:[],capture_scope:options.targetScope?{...scope,lifetime_cookie:'9'}:scope,capture_space_preserved:!options.noPreserve};
  }else if(cmd==='json'){
   const body=JSON.parse(a[1]);
   if(body.cmd==='frame-hash'){
    if(options.captureThrowsAfterMove&&moves)throw new Error('capture failed after acknowledged motion');
    reads++;const changing=options.animate||options.smooth&&reads<5;
    const value=changing?reads:Math.round(point[0]*1000+point[1]);
    json={ok:true,hash:value.toString(16).padStart(16,'0'),capture_scope:options.scopeAfterMove&&moves?{...scope,geometry_epoch:'1'}:scope,
     timing_clock:'QueryPerformanceCounter',qpc_frequency:'10000000',capture_qpc_ticks:String(reads*10000),frame_qpc_ticks:String(reads*10000-100),...(body.motion_sample?{motion_sample_version:1,motion_sample_w:16,motion_sample_h:1,motion_sample_rgb:(options.largeNoise&&reads%2?'FFFFFF':reads%2?'010101':'000000').repeat(16)}:{})};
   }else if(body.cmd==='move'){
    moves++;
    if(options.motionThrows)throw new Error('transport fault');
    if(!options.noProgress&&!options.singular){point=[point[0]-.5*body.dx,point[1]-.5*body.dy];}
    if(options.epochAfterMove)epoch++;
    json=options.refuse?{ok:false,input_injected:false,error:'scope mismatch'}:{ok:true,capture_scope_checked:!options.badMotionAck,raw_dx:body.dx,raw_dy:body.dy,unbounded:true};
   }else throw new Error(`unexpected json ${body.cmd}`);
  }else throw new Error(`unexpected ${cmd}`);
  return {ok:json.ok===true,json,exitCode:0,stderr:'',timedOut:false,argv};
 }};
 return {ctx,calls,get moves(){return moves},get finishOptions(){return finishOptions},get finishEpoch(){return finishEpoch}};
}
const read=r=>JSON.parse(r.content[0].text),args={pid,template:'landmark.png',poll_ms:1,stable_samples:2,timeout_ms:500,settle_timeout_ms:100};
let f=fixture({smooth:true}),r=read(await observeSettle(f.ctx,args));
check(r.ok&&r.settled&&r.observation.changes>0&&!f.moves,'finite changing frames can become hash stable without any injected input');
check(r.observation.queue_drain_measured===false&&r.observation.raw_queue_remaining_counts===null&&r.observation.camera_angle_measured===false,'pixel stability never becomes a raw queue drain or angle claim');
check(r.observation.sample.timing_clock==='QueryPerformanceCounter'&&r.observation.sample.capture_qpc_ticks,'native observation clock evidence is retained');
f=fixture({animate:true});r=read(await observeSettle(f.ctx,{...args,timeout_ms:15}));
check(!r.ok&&!r.settled&&!f.moves,'continuously changing region expires without invented settle evidence');
f=fixture();r=read(await lookCalibrate(f.ctx,args));
check(r.ok&&f.moves===2&&JSON.stringify(r.calibration.matrix)===JSON.stringify(matrix),'calibration measures signed two-axis landmark displacement from two probes');
check(r.degrees_per_count===null&&r.queue_drain_measured===false&&r.observations.length===3,'calibration keeps unknown camera angles and raw consumption explicit');
check(f.calls.filter(c=>c.argv[4]==='find-image').length===3&&f.calls.filter(c=>c.argv[4]==='find-image').every(c=>c.argv.includes('--preserve-map')),'every calibration probe reidentifies the target without changing the saved map');
check(f.calls.filter(c=>c.argv[4]==='json').map(c=>JSON.parse(c.argv[5])).filter(c=>c.cmd==='move').every(c=>c.steps===1&&c.raw===true&&c.expected_capture_scope===JSON.stringify(scope)),'all calibration movement is bounded and native scope-fenced');
f=fixture();r=read(await lookAt(f.ctx,{...args,calibration,max_counts_per_step:150}));
check(r.ok&&r.centered&&r.error_px<=6&&f.moves===2,'look_at uses reobserved template positions to converge in bounded steps');
check(r.steps.every(s=>Math.abs(s.dx)<=150&&Math.abs(s.dy)<=150)&&r.steps.every(s=>s.observation.stable),'every correction respects count and settle bounds');
for(const [option,label] of [['oldDll','old DLL'],['oldCli','old CLI'],['ambiguous','ambiguous template'],['targetScope','other image scope'],['noPreserve','unconfirmed map preservation'],['badCenter','out-of-client target'],['ownerActive','legacy user ownership'],['nestedOwner','current native user ownership']]){
 f=fixture({[option]:true});r=read(await lookCalibrate(f.ctx,args));check(!r.ok&&!f.moves,`${label} refuses before camera input`);
}
f=fixture({singular:true});r=read(await lookCalibrate(f.ctx,args));
check(!r.ok&&r.calibration===null&&f.moves===2&&r.error.includes('singular'),'zero visual response never becomes a fabricated calibration');
f=fixture();r=read(await lookAt(f.ctx,{...args,calibration:{matrix,capture_scope:{...scope,geometry_epoch:'1'}}}));
check(!r.ok&&!f.moves,'stale calibration scope refuses before any movement');
f=fixture();r=read(await lookAt(f.ctx,{...args,calibration:{matrix:{xx:1e308,yy:1e308,xy:0,yx:0},capture_scope:scope}}));
check(!r.ok&&!f.moves,'overflowing calibration determinant refuses before movement');
f=fixture({noProgress:true});r=read(await lookAt(f.ctx,{...args,calibration}));
check(!r.ok&&f.moves===1&&r.error.includes('progress'),'no visual progress stops after one correction rather than replaying blindly');
f=fixture({missingAfter:true});r=read(await lookAt(f.ctx,{...args,calibration}));
check(!r.ok&&f.moves===1,'lost landmark stops further movement');
f=fixture({scopeAfterMove:true});r=read(await lookAt(f.ctx,{...args,calibration}));
check(!r.ok&&f.moves===1&&r.error.includes('geometry')&&r.steps.length===1&&r.steps[0].move.capture_scope_checked===true,'post-move scope change stops and retains the acknowledged move receipt');
f=fixture({captureThrowsAfterMove:true});r=read(await lookAt(f.ctx,{...args,calibration}));
check(!r.ok&&f.moves===1&&r.steps.length===1&&r.steps[0].move.capture_scope_checked===true&&r.error.includes('capture failed'),'capture failure after motion cannot erase its acknowledged native receipt');
f=fixture({ownerAfterMove:true});r=read(await lookAt(f.ctx,{...args,calibration}));
check(!r.ok&&f.moves===1&&r.steps.length===1&&r.error.includes('user currently'),'new native user ownership blocks the next correction without losing previous motion evidence');
f=fixture({nestedOwner:true});r=read(await observeSettle(f.ctx,args));
check(r.ok&&!f.moves,'read-only stability observation remains available during native user ownership');
for(const option of ['badMotionAck','motionThrows','epochAfterMove']){f=fixture({[option]:true});r=read(await lookAt(f.ctx,{...args,calibration}));check(!r.ok&&f.moves===1&&r.motion_may_have_been_applied===true,`${option} keeps uncertain applied motion visible and never retries`);}
f=fixture({refuse:true});r=read(await lookAt(f.ctx,{...args,calibration}));
check(!r.ok&&f.moves===1&&r.motion_may_have_been_applied===false&&r.unconfirmed_motion.input_injected===false,'typed native no-input refusal remains distinguishable from uncertain motion');
f=fixture({animate:true});r=read(await lookAt(f.ctx,{...args,calibration,settle_timeout_ms:15}));
check(!r.ok&&!f.moves,'unsettled initial region refuses before camera correction');
f=fixture({animate:true});r=read(await observeSettle(f.ctx,{...args,max_change_pct:5,pixel_tolerance:8}));
check(r.ok&&r.observation.stability_metric==='area_averaged_rgb_grid'&&r.observation.max_change_pct===5,'small ambient RGB noise can settle under an explicit motion tolerance despite different exact hashes');
f=fixture({animate:true,largeNoise:true});r=read(await observeSettle(f.ctx,{...args,timeout_ms:20,max_change_pct:5}));
check(!r.ok&&!r.settled,'large changing regions cannot borrow a small motion tolerance');
f=fixture({animate:true});r=read(await lookTurn(f.ctx,{pid,yaw_deg:90,counts_per_degree:12.5}));
check(r.ok&&f.moves===1&&r.submitted_dx===1125&&r.actual_yaw_deg===null&&r.calibration_source==='caller_supplied','plain yaw submits supplied counts once without demanding a stable landmark or claiming angle measurement');
f=fixture();r=read(await lookTurn(f.ctx,{pid,yaw_deg:90,counts_per_degree:0}));check(!r.ok&&!f.moves,'plain yaw rejects zero gain before motion');
f=fixture();r=read(await lookAt(f.ctx,{...args,calibration,max_steps:1,max_counts_per_step:10}));
check(!r.ok&&f.moves===1&&r.steps.length===1,'correction stops at its explicit step bound');
f=fixture({badMotionAck:true});r=read(await lookAt(f.ctx,{...args,calibration,capture_after:0,max_width:160,format:'jpeg',quality:75}));
check(f.finishOptions.capture_after===0&&f.finishOptions.max_width===160&&f.finishOptions.quality===75&&f.finishEpoch===0,'post-action image options and original attachment epoch survive a failed motion observation');
console.log(`VISUAL_CAMERA_RESULT ${JSON.stringify({completed:true,passed:state.passes,failed:state.failures,scope:'dependency-injected visual protocol model; simulated pixel response, no camera-angle or actual raw-input-consumption measurement'})}`);
process.exit(state.failures?1:0);
