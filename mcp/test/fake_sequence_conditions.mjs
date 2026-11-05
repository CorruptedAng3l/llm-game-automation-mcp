// Scoped capture / exact-job protocol endpoint only. No native input or GUI API.
import {appendFileSync,readFileSync,writeFileSync} from 'node:fs';
const argv=process.argv.slice(2),log=process.env.VINPUT_FAKE_LOG,scenario=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8'));
const rest=[];let pid=0;for(let i=0;i<argv.length;i++){if(argv[i]==='--pid'){pid=Number(argv[++i]);continue;}if(argv[i]==='--owner'){i++;continue;}if(argv[i]==='--json')continue;rest.push(argv[i]);}
const command=rest[0];let request;try{if(command==='json')request=JSON.parse(rest[1]);}catch{}
if(request?.cmd==='frame-hash'||request?.cmd==='sequence-observe'||command==='ocr'||command==='find-image'||command==='seq'){
 const before=readFileSync(log,'utf8').split('\n').filter(Boolean).map(JSON.parse);let out;
 if(command==='seq'){
  const flag=f=>{const i=rest.indexOf(f);return i<0?undefined:rest[i+1];};
  out={...scenario.replies.seq,...(flag('--scope-json')?{capture_scope_checked:true}:{}),...(flag('--guard-ms')?{sequence_guard_version:1,observer_guarded:true,observer_guard_ms:Number(flag('--guard-ms')),guard_observation_seq:0}:{}),
   ...(flag('--start-qpc')?{anchor_source:'observed_capture',anchor_qpc_ticks:flag('--start-qpc'),qpc_frequency:flag('--start-frequency')}:{}),...(scenario.initial_patch??{})};
 }else if(request?.cmd==='sequence-observe'){
  const n=before.filter(x=>{try{return JSON.parse(x.argv[x.argv.indexOf('json')+1]).cmd==='sequence-observe';}catch{return false;}}).length;
  const state=request.abort?(scenario.abort_completed?'completed':'cancelled'):(scenario.guard_states?.[Math.min(n,scenario.guard_states.length-1)]??'running');
  const job={...scenario.replies.seq,accepted:false,ok:state==='completed',job_state:state,job_complete:['completed','cancelled','failed'].includes(state),capture_scope_checked:true,sequence_guard_version:1,observer_guarded:true,observer_guard_ms:4000,guard_observation_seq:request.observation_seq,sequence_step:1,finished_qpc_ticks:request.abort?'1030':'0',job_reason:request.abort?`sequence_abort_${request.reason}`:'',...(scenario.job_patch??{})};
  out=scenario.lost_guard_ack_at===n+1?{ok:false,error:'modeled acknowledgement loss'}:{ok:true,guard_ack:true,guard_renewed:!job.job_complete,observation_seq:request.observation_seq,job,...(scenario.guard_reply_patch??{})};
 }else{
  const n=before.filter(x=>x.argv.includes('ocr')||x.argv.includes('find-image')||x.argv.some(a=>a.includes('"cmd":"frame-hash"'))).length;
  out=scenario.observations?.[Math.min(n,scenario.observations.length-1)]??{ok:false,error:'no modeled capture'};
 }
 const captureIndex=rest.indexOf('--observation-capture'),capturePath=captureIndex>=0?rest[captureIndex+1]:request?.path;
 if(scenario.retain_frames&&capturePath&&(command==='ocr'||command==='find-image'&&out.match_count>0||request?.cmd==='frame-hash'&&out.hash!==request.retain_changed_from)){
  writeFileSync(capturePath,Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==','base64'));out={...out,observation_frame_path:capturePath};
 }
 let seqText;if(command==='seq')seqText=readFileSync(rest[1],'utf8');appendFileSync(log,JSON.stringify({argv,t:Date.now(),seqText})+'\n');
 if(out._delay_ms)await new Promise(r=>setTimeout(r,out._delay_ms));const {_delay_ms,_exit,...value}=out;process.stdout.write(JSON.stringify(value)+'\n');process.exit(_exit??(value.ok===false?1:0));
}else await import('./fake_vinput.mjs');
