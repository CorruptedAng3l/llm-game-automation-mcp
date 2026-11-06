import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
import {makeChecker} from './lib.mjs';
const moduleUrl=process.env.VINPUT_TEST_MODULE_DIR?pathToFileURL(resolve(process.env.VINPUT_TEST_MODULE_DIR,'keydrive.js')).href:new URL('../build/keydrive.js',import.meta.url).href;
const {keyDrive,keyDrivePlan,sequencePwmKeyError}=await import(moduleUrl),{check,state}=makeChecker(),pid=4242,owner='keydrive-unit',id='4242:10:1';
for(const key of ['lbutton','VK_RBUTTON','mbutton','xbutton1','vk00006','VK_0x02','w+vk1'])check(!!sequencePwmKeyError(key),'SDK PWM rejects lower VK aliases before scheduling');
check(!sequencePwmKeyError('w+shift')&&!sequencePwmKeyError('0x41')&&!sequencePwmKeyError('vk87'),'SDK PWM permits ordinary keyboard names and numeric keyboard values');
const phases=[{ms:600,throttle:true,steer:-0.3},{ms:400,throttle:true,steer:0.5},{ms:200,brake:true}];
let parsed=keyDrivePlan({phases,period_ms:60});
check(typeof parsed==='object'&&parsed.duration===1200&&parsed.script==='drive 600 1 -0.3 0\ndrive 400 1 0.5 0\ndrive 200 0 0 1\n'&&parsed.phases[0].steer_on_ms===18,'public phases serialize exactly one finite native drive timeline with signed fractional duty');
for(const a of [{phases:[]},{phases:[{ms:60001}]},{phases:[{ms:60000},{ms:10}]},{phases:[{ms:100,steer:.01}],period_ms:60},{phases:[{ms:60000,steer:.5}],period_ms:20},{phases:Array.from({length:129},()=>({ms:10}))}])check(typeof keyDrivePlan(a)==='string','invalid duration/PWM/phase/transition bounds refuse before native calls');
check(typeof keyDrivePlan({phases:[{ms:10,steer:0},{ms:10,steer:1},{ms:10,steer:-1}]})==='object','explicit zero/full signed steering fractions remain valid finite states');
function fixture(options={}){
 const calls=[];let epoch=0,finished;
 const ctx={owner,at:p=>['--owner',owner,'--pid',String(p)],epoch:()=>epoch,captureError:()=>options.captureError,finish:async(p,r,a,e)=>{finished={p,r,a,e};if(options.finishThrows)throw Error('modeled post-action observation failure');return r;},run:async(argv,budget)=>{
  const q=JSON.parse(argv[5]);calls.push({q,budget,argv});let j;
  if(q.cmd==='ping'){if(options.capEpoch)epoch++;j={ok:true,pid:options.wrongPid?1:pid,key_drive_version:options.old?0:1};}
  else if(q.cmd==='key-drive'){
   if(options.throws)throw Error('modeled transport uncertainty');
   if(options.inputEpoch)epoch++;
   const lines=q.script.trim().split('\n'),duration=lines.reduce((total,line)=>total+Number(line.split(' ')[1]),0);
   j=options.refused?{ok:false,input_injected:false,error:'manual virtual input already holds this resource'}:{ok:true,pid,owner:options.badOwner?'other':owner,job_id:options.badId?'4242:11:99':id,job_kind:'key_drive',requested_ms:duration+(options.badDuration?1:0),key_drive_version:1,key_drive_period_ms:q.period_ms,key_drive_phases:lines.length,capture_scope_checked:!options.badScope,accepted:!!q.async,job_state:q.async?'queued':'completed',job_complete:!q.async,input_injected:q.async?null:true};
   if(options.badId)j.job_id='999:11:99';
  }else throw Error('unexpected native command '+q.cmd);
  return {ok:j.ok===true,json:j,argv,stderr:'',timedOut:false,exitCode:j.ok?0:1};
 }};
 return {ctx,calls,get finished(){return finished;}};
}
const read=r=>JSON.parse(r.content[0].text),drive=f=>f.calls.filter(c=>c.q.cmd==='key-drive');
let f=fixture(),r=read(await keyDrive(f.ctx,{pid,phases,period_ms:60,bindings:{throttle:'w+shift'}}));
check(r.ok&&r.job_id===id&&r.key_drive.requested_ms===1200&&r.key_drive.vehicle_motion_measured===false&&r.key_drive.application_response===null,'completed native identity and digital intent stay separate from vehicle/game response');
check(f.calls.length===2&&f.calls[0].q.input_job_support===true&&drive(f).length===1&&drive(f)[0].q.throttle_key==='w+shift'&&drive(f)[0].q.async===false&&drive(f)[0].q.replace===false,'tiny capability ping then exactly one native command with no default takeover');
check(f.finished?.e===0&&drive(f)[0].budget===16200,'shared final image receives original attachment epoch after native synchronous completion');
f=fixture();r=read(await keyDrive(f.ctx,{pid,phases,period_ms:60,async:true,replace:true,max_lateness_ms:20}));
check(r.ok&&r.accepted&&r.next_action.tool==='job_status'&&r.next_action.arguments.job_id===id&&drive(f)[0].q.async&&drive(f)[0].q.replace&&drive(f)[0].q.max_lateness_ms===20,'explicit async/takeover/lateness forward once and return exact-job continuation');
for(const option of ['old','wrongPid','capEpoch','captureError']){
 f=fixture({[option]:option==='captureError'?'bad capture options':true});r=read(await keyDrive(f.ctx,{pid,phases}));check(!r.ok&&r.input_injected===false&&!drive(f).length,option+' refuses before any driving input');
}
for(const option of ['badId','badOwner','badDuration','badScope']){
 f=fixture({[option]:true});r=read(await keyDrive(f.ctx,{pid,phases}));check(!r.ok&&r.input_may_have_been_applied&&r.do_not_repeat&&drive(f).length===1&&!f.finished,option+' ACK preserves uncertainty without another input or arbitrary cleanup');
}
f=fixture({refused:true});r=read(await keyDrive(f.ctx,{pid,phases}));check(!r.ok&&!r.input_may_have_been_applied&&r.input_injected===false&&drive(f).length===1,'typed native manual/busy no-input refusal is not reported as an active unknown hold');
for(const option of ['throws','inputEpoch']){
 f=fixture({[option]:true});r=read(await keyDrive(f.ctx,{pid,phases}));check(!r.ok&&r.do_not_repeat&&r.input_may_have_been_applied&&drive(f).length===1,option+' retains one admission and never replays or globally releases input');
}
f=fixture({finishThrows:true});r=read(await keyDrive(f.ctx,{pid,phases}));
check(r.ok&&r.job_complete===true&&r.job_state==='completed'&&r.job_id===id&&r.owner===owner&&r.input_injected===true&&r.capture_after_ok===false&&/post-action observation/.test(r.capture_after_error)&&r.input_may_have_been_applied===undefined&&drive(f).length===1,'post-image failure is advisory while exact completed native input remains known and successful');
f=fixture({finishThrows:true});r=read(await keyDrive(f.ctx,{pid,phases,async:true}));
check(r.ok&&r.accepted===true&&r.job_complete===false&&r.job_id===id&&r.next_action?.arguments.job_id===id&&r.capture_after_ok===false&&drive(f).length===1,'post-image failure retains known accepted asynchronous job and exact continuation without replay');
const compactUrl=process.env.VINPUT_TEST_MODULE_DIR?pathToFileURL(resolve(process.env.VINPUT_TEST_MODULE_DIR,'compactreply.js')).href:new URL('../build/compactreply.js',import.meta.url).href;
const {compactReply}=await import(compactUrl);f=fixture();const full=await keyDrive(f.ctx,{pid,phases});const fullJson=read(full);fullJson.key_drive_scope='requested digital duty, never analog steering angle or game acceptance';const compact=read(compactReply({...full,content:[{type:'text',text:JSON.stringify(fullJson)}]},true,'key_drive'));
check(compact.owner===owner&&compact.requested_ms===1200&&compact.capture_scope_checked===true&&compact.job_id===id,'compact successful drive retains exact owner/job/requested duration/scope admission facts');
const scopeText=compact.key_drive?.scope??compact.key_drive_scope;
check(typeof scopeText==='string'&&/digital/.test(scopeText)&&(/angle/.test(scopeText)||/steering/.test(scopeText)),'compact drive retains explicit digital-intent qualification instead of losing its scope');
console.log(`key drive model: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
