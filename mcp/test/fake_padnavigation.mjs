// Stateful navigation protocol fixture. It never loads a DLL or controls a game.
import {appendFileSync,existsSync,readFileSync,writeFileSync} from 'node:fs';
const argv=process.argv.slice(2),rest=[];let pid=0,owner='';
for(let i=0;i<argv.length;i++){if(argv[i]==='--json')continue;if(argv[i]==='--pid'){pid=Number(argv[++i]);continue;}if(argv[i]==='--owner'){owner=argv[++i];continue;}rest.push(argv[i]);}
const scenario=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8')),options=scenario.padnavigation??{},file=process.env.VINPUT_FAKE_SCENARIO+'.state';
const state=existsSync(file)?JSON.parse(readFileSync(file,'utf8')):{reads:0,duration:2000,admitted:false,start:0};
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600,geometry_witness:'800x600'};
const id=`${pid}:10:1`,job=(accepted=false,cancelled=false)=>{
 const done=cancelled||options.completed===true||state.admitted&&Date.now()-state.start>=state.duration;
 return {ok:accepted||done&&!cancelled,accepted,job_id:id,owner,job_kind:'pad',pad_motion_job_version:1,buttons:0,axes_set_mask:63,hold:false,clamped:false,requested_axes:state.axes??{lt:0,rt:0,lx:0,ly:20000,rx:0,ry:0},requested_ms:state.duration,capture_scope_checked:true,job_state:cancelled?'cancelled':done?'completed':options.queued?'queued':'running',job_reason:cancelled?'explicit_cancel':'',job_complete:done,hold_ready:!done&&!options.queued};
};
let result;
if(rest[0]==='status')result={ok:true,pid,capture_scope_version:1,pad_motion_job_version:options.old?0:1,input_scope_version:1,cursor_guard:{owner_active:options.manual===true}};
else if(rest[0]==='jobs')result={ok:true,jobs:options.busy?[{job_kind:'pad',job_complete:false}]:[]};
else if(rest[0]==='job-status')result={ok:true,job:job()};
else if(rest[0]==='job-cancel')result={ok:true,job:job(false,true)};
else if(rest[0]==='json'){
 const q=JSON.parse(rest[1]);
 if(q.cmd==='pad'){
  if(q.expected_capture_scope!==JSON.stringify(scope))result={ok:false,input_injected:false,error:'capture scope mismatch'};
  else{state.axes={lt:q.lt,rt:q.rt,lx:q.lx,ly:q.ly,rx:q.rx,ry:q.ry};state.duration=q.ms;state.admitted=true;state.start=Date.now();result=job(true);}
 }else if(q.cmd==='frame-hash'){
  state.reads++;
  if(options.captureFailure&&state.admitted)result={ok:false,error:'capture observation failed'};
  else{
   const color=options.moving?(state.reads%2?'000000':'eeeeee'):options.noise?(state.reads%4).toString(16).padStart(2,'0').repeat(3):'333333';
   result={ok:true,hash:color.padEnd(16,'0'),capture_scope:options.scopeChange&&state.admitted?{...scope,geometry_epoch:'1'}:scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',qpc_frequency:'10000000',capture_qpc_ticks:String(BigInt(Date.now())*10000n),
    motion_sample_version:1,motion_sample_w:16,motion_sample_h:1,motion_sample_rgb:color.repeat(16)};
  }
 }
}
if(result){if(process.env.VINPUT_FAKE_LOG)appendFileSync(process.env.VINPUT_FAKE_LOG,JSON.stringify({argv,t:Date.now()})+'\n');writeFileSync(file,JSON.stringify(state));process.stdout.write(JSON.stringify(result)+'\n');process.exit(result.ok===true?0:1);}
await import('./fake_vinput.mjs');
