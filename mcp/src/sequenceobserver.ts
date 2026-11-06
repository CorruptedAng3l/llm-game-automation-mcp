import {createHash} from 'node:crypto';
import type {CliResult} from './vinput.js';
import {sequenceJob,type Job,type SequenceWaitContext} from './sequencewait.js';

type Obj=Record<string,unknown>;
const object=(v:unknown):Obj|undefined=>v!==null&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const positiveUint=(v:unknown):v is string=>typeof v==='string'&&/^[1-9][0-9]{0,19}$/.test(v)&&BigInt(v)<=18446744073709551615n;
export const OBSERVER_BUDGET_MS=1000,RECONCILE_WORK_RESERVE_MS=2600;
export function boundedReceipt(value:unknown,maxBytes=4096):unknown{
 const text=JSON.stringify(value??null),bytes=Buffer.byteLength(text);
 if(bytes<=maxBytes)return value??null;
 let prefix=Buffer.from(text).subarray(0,maxBytes).toString('utf8');while(Buffer.byteLength(prefix)>maxBytes)prefix=prefix.slice(0,-1);
 return {truncated:true,bytes,sha256:createHash('sha256').update(text).digest('hex'),json_prefix:prefix};
}
export function transportLost(r:CliResult):boolean{
 if(r.ok)return false;
 const body=object(r.json);if(body&&(Object.hasOwn(body,'guard_ack')||Object.hasOwn(object(body.partial_reply)??{},'guard_ack')))return false;
 return r.timedOut||r.exitCode===null||/cannot connect|pipe i\/o|did not answer|pipe transport|acknowledgement loss|ack.*withheld/i.test(String(object(r.json)?.error??''));
}
export function heartbeatPredicates(r:CliResult,ctx:SequenceWaitContext,id:string,serial:number,guardMs:number){
 const o=object(r.json),job=sequenceJob(o?.job,ctx,false,id);
 const checks={transport_ok:r.ok,guard_ack:o?.guard_ack===true,ack_serial:o?.observation_seq===serial,exact_job:!!job,
  renewal_state:!!job&&o?.guard_renewed===!job.job_complete,guard_version:job?.sequence_guard_version===1,guarded:job?.observer_guarded===true,
  guard_interval:job?.observer_guard_ms===guardMs,scope_checked:job?.capture_scope_checked===true,
  typed_serial:Number.isInteger(job?.guard_observation_seq)&&Number(job?.guard_observation_seq)>=0,
  live_serial:!!job&&(job.job_complete||job.guard_observation_seq===serial)};
 return {checks,job,ok:Object.values(checks).every(Boolean)};
}
export function reconciliationPredicates(r:CliResult,ctx:SequenceWaitContext,admitted:Job,scope:string|undefined,serial:number,guardMs:number,elapsedMs:number,scopeKey:(v:unknown)=>string|undefined,reference:Job=admitted){
 const o=object(r.json),job=sequenceJob(o?.job,ctx,false,admitted.job_id);
 const remaining=job?.observer_guard_remaining_ms,previousRemaining=reference.observer_guard_remaining_ms,sample=job?.observer_guard_sample_ms,previousSample=reference.observer_guard_sample_ms;
 const orderedSample=positiveUint(sample)&&positiveUint(previousSample)&&BigInt(sample)>=BigInt(previousSample);
 const typedPrevious=typeof previousRemaining==='number'&&Number.isInteger(previousRemaining)&&previousRemaining>=0&&previousRemaining<=guardMs;
 const checks={transport_ok:r.ok,outer_ok:o?.ok===true,exact_job:!!job,capability:admitted.sequence_observer_reconcile_version===1&&job?.sequence_observer_reconcile_version===1,
  exact_scope:!!scope&&scopeKey(admitted.capture_scope)===scope&&scopeKey(job?.capture_scope)===scope,scope_checked:job?.capture_scope_checked===true,
  guard_version:job?.sequence_guard_version===1,guarded:job?.observer_guarded===true,guard_interval:job?.observer_guard_ms===guardMs,
  exact_serial:Number.isSafeInteger(job?.guard_observation_seq)&&job?.guard_observation_seq===serial,observer_qpc:positiveUint(job?.observer_qpc_ticks),
  sample_clock:job?.observer_guard_clock==='QueryInterruptTime'&&reference.observer_guard_clock==='QueryInterruptTime',sample_time:positiveUint(sample),monotonic_sample:orderedSample,
  deadline_not_regressed:!!job&&(job.job_complete||orderedSample&&typedPrevious&&typeof remaining==='number'&&Number.isInteger(remaining)&&remaining>=0&&BigInt(sample!)+BigInt(remaining)>=BigInt(previousSample!)+BigInt(previousRemaining)),
  typed_remaining:typeof remaining==='number'&&Number.isInteger(remaining)&&remaining>=0&&remaining<=guardMs,typed_round_trip:Number.isFinite(elapsedMs)&&elapsedMs>=0,
  lease_current:!!job&&(job.job_complete?remaining===0:typeof remaining==='number'&&remaining>Math.ceil(elapsedMs)+RECONCILE_WORK_RESERVE_MS)};
 return {checks,job,ok:Object.values(checks).every(Boolean)};
}
export class ObserverDiagnostics{
 private entries:Obj[]=[];private failures=0;
 add(entry:Obj):Obj{this.failures++;if(this.entries.length<4)this.entries.push(entry);return entry;}
 report():Obj|undefined{return this.failures?{version:1,failures:this.failures,retained:this.entries.length,dropped:this.failures-this.entries.length,
  max_entries:4,receipt_max_bytes:4096,stderr_max_chars:1024,entries:this.entries,
  note:'Host elapsed times include CLI startup/transport. A read-only query never renews a native lease; uncertain control requests are never replayed.'}:undefined;}
}
