// Stateful fake CLI for camera/hold MCP integration. Never loads or controls a game.
import {appendFileSync,existsSync,readFileSync,writeFileSync} from 'node:fs';
const argv=process.argv.slice(2),rest=[];let pid=0,owner='';
for(let i=0;i<argv.length;i++){if(argv[i]==='--json')continue;if(argv[i]==='--pid'){pid=Number(argv[++i]);continue;}if(argv[i]==='--owner'){owner=argv[++i];continue;}rest.push(argv[i]);}
const cmd=rest[0],scen=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8')),stateFile=process.env.VINPUT_FAKE_SCENARIO+'.state';
const state=existsSync(stateFile)?JSON.parse(readFileSync(stateFile,'utf8')):{hashReads:0,duration:5000,target:[300,250]};
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600};
const id=`${pid}:10:1`;
const job=(accepted=false,done=false)=>({ok:accepted,accepted,job_id:id,owner,requested_ms:state.duration,job_state:done?'cancelled':'running',job_complete:done,hold_ready:!done,capture_scope_checked:true});
let result;
if(cmd==='help')result={ok:true,image_scope_version:scen.camera?.oldCli?undefined:1};
else if(cmd==='status')result={ok:true,pid,capture_scope_version:1,key_sequence_version:1,input_scope_version:1,relative_scope_version:scen.camera?.oldDll?undefined:1,client_w:800,client_h:600,cursor_guard:{owner_active:scen.camera?.manualOwner===true}};
else if(cmd==='jobs')result={ok:true,jobs:[]};
else if(cmd==='key'){state.duration=Number(rest[2]);result=job(true);}
else if(cmd==='job-status')result={ok:true,job:job()};
else if(cmd==='job-cancel'){state.done=true;result={ok:true,job:job(false,true)};}
else if(cmd==='find-image'){
  const count=scen.camera?.ambiguous?2:1;
  result={ok:true,match_count:count,matches:Array.from({length:count},()=>({center:state.target,score:.99})),capture_scope:scope,capture_space_preserved:rest.includes('--preserve-map')};
}
else if(cmd==='json'){
  const body=JSON.parse(rest[1]);
  if(body.cmd==='key'){
    if(body.expected_capture_scope!==JSON.stringify(scope))result={ok:false,input_injected:false,error:'capture scope mismatch'};
    else {state.duration=body.ms;state.holdAdmitted=true;result=job(true);}
  }
  if(body.cmd==='move'){
    if(body.expected_capture_scope!==JSON.stringify(scope))result={ok:false,input_injected:false,error:'capture scope mismatch'};
    else {if(!scen.camera?.noProgress)state.target=[state.target[0]-.5*body.dx,state.target[1]-.5*body.dy];state.moves=(state.moves??0)+1;result={ok:true,capture_scope_checked:true,raw_dx:body.dx,raw_dy:body.dy,unbounded:true};}
  }
  if(body.cmd==='frame-hash'){
    state.hashReads++;const hash=scen.camera?.look?Math.round(state.target[0]*1000+state.target[1]).toString(16).padStart(16,'0'):scen.camera?.stable||!state.holdAdmitted?'1111111111111111':'2222222222222222';
    result={ok:true,hash,capture_scope:scope,w:800,h:600,frame:state.hashReads,timing_clock:'QueryPerformanceCounter',qpc_frequency:'10000000',capture_qpc_ticks:String(state.hashReads*10000),frame_qpc_ticks:String(state.hashReads*10000-100),...(body.motion_sample?{motion_sample_version:1,motion_sample_w:16,motion_sample_h:1,motion_sample_rgb:'000000'.repeat(16)}:{})};
  }
}
if(result){if(process.env.VINPUT_FAKE_LOG)appendFileSync(process.env.VINPUT_FAKE_LOG,JSON.stringify({argv,t:Date.now()})+'\n');writeFileSync(stateFile,JSON.stringify(state));process.stdout.write(JSON.stringify(result)+'\n');process.exit(result.ok===true?0:1);}
await import('./fake_vinput.mjs');
