// Key-drive protocol endpoint only: modeled receipts, no native input.
import {appendFileSync,readFileSync} from 'node:fs';
const argv=process.argv.slice(2),log=process.env.VINPUT_FAKE_LOG,scenario=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8'));
const rest=[];let pid=0,owner='';for(let i=0;i<argv.length;i++){if(argv[i]==='--pid'){pid=Number(argv[++i]);continue;}if(argv[i]==='--owner'){owner=argv[++i];continue;}if(argv[i]==='--json')continue;rest.push(argv[i]);}
let q;try{if(rest[0]==='json')q=JSON.parse(rest[1]);}catch{}
if(q?.cmd==='key-drive'||q?.cmd==='ping'&&q.input_job_support||rest[0]==='seq'){
 let value,seqText;
 if(q?.cmd==='ping')value={ok:true,pid,key_drive_version:1,sequence_pwm_version:1,...scenario.capability_patch};
 else if(q?.cmd==='key-drive'){
  const lines=q.script.trim().split('\n');const duration=lines.reduce((n,l)=>n+Number(l.split(' ')[1]),0);
  value={ok:true,accepted:q.async,job_id:`${pid}:10:1`,job_kind:'key_drive',job_state:q.async?'queued':'completed',job_complete:!q.async,owner,requested_ms:duration,key_drive_version:1,key_drive_phases:lines.length,key_drive_period_ms:q.period_ms,capture_scope_checked:true,transitions:q.async?0:8,vehicle_motion_measured:false,application_response:null,...scenario.native_patch};
 }else{seqText=readFileSync(rest[1],'utf8');value={ok:true,accepted:true,job_id:`${pid}:10:2`,job_kind:'sequence',job_state:'queued',job_complete:false,owner,requested_ms:800,transitions:0};}
 appendFileSync(log,JSON.stringify({argv,t:Date.now(),seqText})+'\n');process.stdout.write(JSON.stringify(value)+'\n');process.exit(value.ok===false?1:0);
}else await import('./fake_vinput.mjs');