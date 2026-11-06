import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
const {padMoveUntilStuck}=await import(process.env.VINPUT_TEST_MODULE_DIR?pathToFileURL(resolve(process.env.VINPUT_TEST_MODULE_DIR,'padnavigation.js')).href:new URL('../build/padnavigation.js',import.meta.url).href);
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),pid=4242,owner='navigation-unit',id=`${pid}:10:1`;
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600,geometry_witness:'800x600'};
function fixture(options={}){
 const calls=[];let reads=0,statuses=0,epoch=0,admitted=false,lastQpc=0,finishEpoch,finishOptions;
 const duration=options.duration??700;
 const base={job_id:id,owner,job_kind:options.badKind?'keys':'pad',pad_motion_job_version:1,buttons:options.badButtons?1:0,axes_set_mask:options.badMask?3:63,hold:false,clamped:false,requested_axes:{lt:0,rt:0,lx:0,ly:options.badAxes?0:20000,rx:0,ry:0},requested_ms:duration+(options.overhead??0),capture_scope_checked:true};
 const job=(accepted=false,terminal=false)=>({ok:accepted||terminal,accepted,...base,job_state:terminal?'completed':options.queued?'queued':'running',job_reason:'',job_complete:terminal,hold_ready:!terminal&&!options.queued});
 const ctx={owner,at:p=>['--pid',String(p),'--owner',owner],epoch:()=>epoch,captureError:()=>options.captureError,
  finish:async(p,r,a,e)=>{finishEpoch=e;finishOptions=a;return r;},run:async(argv,budget)=>{
   const [cmd,arg]=argv.slice(4);calls.push({cmd,arg,budget,argv});let json;
   if(cmd==='status')json={ok:true,pid,capture_scope_version:1,pad_motion_job_version:options.old?0:1,input_scope_version:1,cursor_guard:{owner_active:options.manual===true}};
   else if(cmd==='jobs')json={ok:true,jobs:[]};
   else if(cmd==='json'){
    const q=JSON.parse(arg);
    if(q.cmd==='pad'){
     if(options.admissionThrows)throw Error('admission transport lost');
     admitted=true;json=options.refused?{ok:false,input_injected:false,error:'capture_scope_changed'}:{...job(true),job_id:options.badAck?'999:10:1':id};
    }else if(q.cmd==='frame-hash'){
     reads++;if(options.captureThrows&&admitted)throw Error('capture observation failed');
     if(options.epochChange&&admitted)epoch++;
     const now=Math.floor(performance.now()*10000)+1;
     const qpc=options.stalledClock&&reads>2?lastQpc:now;lastQpc=qpc;
     let value=options.moving?(reads%2?0:180):options.noise?reads%4:options.thenStill&&reads<4?reads*30:90;
     json={ok:true,hash:'1'.repeat(16),capture_scope:options.scopeChange&&admitted?{...scope,geometry_epoch:'1'}:scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',qpc_frequency:options.clockChange&&admitted?'1000':'10000000',capture_qpc_ticks:String(qpc),
      motion_sample_version:options.badMotion?0:1,motion_sample_w:2,motion_sample_h:1,motion_sample_rgb:value.toString(16).padStart(2,'0').repeat(6)};
    }else throw Error('unexpected JSON '+q.cmd);
   }else if(cmd==='job-status'){
    statuses++;if(options.statusDelay)await new Promise(r=>setTimeout(r,options.statusDelay));
    const endedDuringCapture=(options.cancelAfterCapture||options.completeAfterCapture)&&reads>=4;
    json={ok:true,job:{...job(false,options.completed===true||endedDuringCapture),...(options.statusDurationDrift!==undefined?{requested_ms:base.requested_ms+options.statusDurationDrift}:{}),...(options.cancelAfterCapture&&endedDuringCapture?{ok:false,job_state:'cancelled',job_reason:'manual_key_override'}:{}),...(options.badStatus?{owner:'other'}:{}),...(options.statusAxes?{requested_axes:{...base.requested_axes,rx:1}}:{})}};
   }else if(cmd==='job-cancel'){
    if(options.epochOnCancel)epoch++;
    json={ok:true,job:{...job(false,true),job_state:'cancelled',job_reason:'explicit_cancel',ok:false,job_id:options.badCancel?'4242:10:99':id,...(options.cancelDurationDrift!==undefined?{requested_ms:base.requested_ms+options.cancelDurationDrift}:{}),...(options.cancelAxes?{requested_axes:{...base.requested_axes,ly:0}}:{}),
     ...(options.cancelOnCleanup||options.cancelAfterCapture?{job_reason:'manual_key_override'}:{}),...(options.failOnCleanup?{job_state:'failed',job_reason:'input_capture_scope_changed'}:{})}};
   }else throw Error('unexpected command '+cmd);
   return {ok:json.ok===true,json,argv,exitCode:json.ok?0:1,stderr:'',timedOut:false};
  }};
 return {ctx,calls,duration,get finishEpoch(){return finishEpoch;},get finishOptions(){return finishOptions;}};
}
const args=f=>({pid,direction:'up',region:{x:10,y:20,w:100,h:80},timeout_ms:f.duration,grace_ms:0,still_ms:100,poll_ms:20});
const read=r=>JSON.parse(r.content[0].text),holds=f=>f.calls.filter(c=>c.cmd==='json'&&JSON.parse(c.arg).cmd==='pad'),captures=f=>f.calls.filter(c=>c.cmd==='json'&&JSON.parse(c.arg).cmd==='frame-hash'),cancels=f=>f.calls.filter(c=>c.cmd==='job-cancel');
let f=fixture(),r=read(await padMoveUntilStuck(f.ctx,args(f)));
check(r.ok&&r.stopped==='no_motion'&&r.release_confirmed&&r.hold_started,'stable selected pixels stop one running hold with confirmed release');
check(holds(f).length===1&&cancels(f).length===1&&cancels(f)[0].arg===id&&!f.calls.some(c=>['down','up','release'].includes(c.cmd)),'one finite admission and exact one generation cancellation; no replay or global release');
check(JSON.parse(holds(f)[0].arg).expected_capture_scope===JSON.stringify(scope)&&captures(f).every(c=>JSON.parse(c.arg).preserve_map&&JSON.parse(c.arg).observation_only),'admission binds canonical scope and observations preserve mapping');
check(r.motion_score.measured&&r.motion_score.changed_pct===0&&r.motion_score.quiet_window_ms>=100&&r.displacement===null&&!r.avatar_movement_measured&&r.application_response===null,'motion score is explicitly scoped pixel evidence with unknown displacement');
check(BigInt(r.after_capture_qpc_ticks)>BigInt(r.before_capture_qpc_ticks)&&r.final_job.job_id===id&&r.cleanup.confirmed,'capture clock and exact terminal native receipt survive');
f=fixture({thenStill:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(r.ok&&r.stopped==='no_motion'&&r.motion_score.max_step_changed_pct===100,'changing frames reset the quiet anchor before a later still window stops the hold');
f=fixture({noise:true});r=read(await padMoveUntilStuck(f.ctx,{...args(f),pixel_tolerance:8}));check(r.ok&&r.stopped==='no_motion'&&r.motion_score.changed_pct===0,'bounded RGB noise stays within explicit channel tolerance');
f=fixture({moving:true,duration:180});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(r.ok&&r.stopped==='timeout'&&r.timed_out&&r.motion_score.max_step_changed_pct===100&&cancels(f).length===1,'varying frames reach finite timeout without a no-motion verdict');
f=fixture({queued:true,duration:120});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(r.ok&&r.stopped==='timeout'&&!r.hold_started&&!r.motion_score.measured&&r.samples===0&&captures(f).length===1&&cancels(f).length===1,'queued/unstarted job never yields a motion verdict or post-admission pixel samples');
f=fixture({completed:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(r.ok&&r.stopped==='native_completed'&&r.release_confirmed&&!r.hold_started&&cancels(f).length===0&&captures(f).length===1,'natural native completion is terminal without unnecessary cancellation or pixel claim');
for(const option of ['cancelAfterCapture','cancelOnCleanup','failOnCleanup']){
 f=fixture({[option]:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.stopped===`native_${option==='failOnCleanup'?'failed':'cancelled'}`&&r.release_confirmed&&r.final_job.job_reason!=='explicit_cancel'&&cancels(f).length<=1,option+' cannot become a successful no-motion result after external terminal state');
}
f=fixture({completeAfterCapture:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(r.ok&&r.stopped==='native_completed'&&r.release_confirmed&&cancels(f).length===0,'post-capture running witness detects natural completion before a no-motion verdict');
for(const option of ['captureThrows','scopeChange','clockChange','stalledClock','badStatus']){
 f=fixture({[option]:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.stopped==='observation_failed'&&r.release_confirmed&&holds(f).length===1&&cancels(f).length===1,option+' stops observation and cancels only the admitted job');
}
f=fixture({epochChange:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&!r.release_confirmed&&r.hold_may_still_be_active&&cancels(f).length===0,'attachment change refuses cleanup into a new target attachment');
for(const option of ['badAck','admissionThrows']){
 f=fixture({[option]:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.hold_may_still_be_active&&cancels(f).length===0,option+' cannot authorize an arbitrary job cancellation');
}
f=fixture({refused:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.admission_explicitly_refused&&!r.hold_may_still_be_active&&cancels(f).length===0,'typed native no-input refusal proves there is no admitted hold to cancel');
for(const option of ['badCancel','epochOnCancel']){
 f=fixture({[option]:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&!r.release_confirmed&&r.hold_may_still_be_active&&cancels(f).length===1,option+' cannot confirm release or send a cleanup retry');
}
for(const option of ['old','manual','badMotion']){
 f=fixture({[option]:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&!r.admission_attempted&&!holds(f).length,option+' refuses before input');
}
f=fixture({duration:100,statusDelay:130});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(r.ok&&r.stopped==='timeout'&&!r.hold_started&&r.samples===0&&captures(f).length===1,'late running status cannot admit a post-deadline no-motion verdict');
f=fixture();r=read(await padMoveUntilStuck(f.ctx,{...args(f),capture_after:'immediate',max_width:160}));check(r.ok&&f.finishEpoch===0&&f.finishOptions.capture_after==='immediate'&&f.finishOptions.max_width===160,'shared action capture keeps the original attachment epoch and options');
for(const extra of [{region:undefined},{still_ms:0},{timeout_ms:16000},{pixel_tolerance:256},{max_change_pct:NaN},{direction:'sideways'},{speed:'fly'},{screen_rotation_deg:Infinity}]){
 f=fixture();r=read(await padMoveUntilStuck(f.ctx,{...args(f),...extra}));check(!r.ok&&r.input_injected===false&&f.calls.length===0,'invalid options refuse before any CLI command '+JSON.stringify(extra));
}
for(const option of ['badKind','badMask','badButtons','badAxes']){
 f=fixture({[option]:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.hold_may_still_be_active&&holds(f).length===1&&cancels(f).length===0,option+' ACK cannot authorize cleanup of an unknown pad resource');
}
f=fixture({overhead:1});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.hold_may_still_be_active&&cancels(f).length===0,'pad ACK duration must equal the exact requested finite duration');
f=fixture({statusDurationDrift:-1});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.release_confirmed&&cancels(f).length===1,'later duration drift fails observation and cleans only the original exact pad job');
f=fixture({statusAxes:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&r.release_confirmed&&cancels(f).length===1,'later immutable pad axes drift fails observation with one exact cleanup');
for(const option of ['cancelAxes','cancelDurationDrift']){
 f=fixture({[option]:option==='cancelDurationDrift'?-1:true});r=read(await padMoveUntilStuck(f.ctx,args(f)));check(!r.ok&&!r.release_confirmed&&r.hold_may_still_be_active&&cancels(f).length===1,option+' cannot confirm neutral release or trigger a retry');
}
check(JSON.parse(holds(f)[0].arg).axes_set_mask===undefined&&JSON.parse(holds(f)[0].arg).lx===0&&JSON.parse(holds(f)[0].arg).ly===20000&&JSON.parse(holds(f)[0].arg).rx===0&&JSON.parse(holds(f)[0].arg).buttons==='none','request explicitly publishes all six axes and no buttons through the native parser');
console.log(`pad navigation motion: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
