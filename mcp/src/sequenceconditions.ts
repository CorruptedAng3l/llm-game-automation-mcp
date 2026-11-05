import {runVinput,type CliResult} from './vinput.js';
import {sequenceJob,sequenceWaitError,type SequenceWaitContext,type Job} from './sequencewait.js';
type Obj=Record<string,unknown>;
type Region={x:number;y:number;w:number;h:number};
export type SequenceCondition={kind:'frame_change'|'region_change'|'text';region?:Region;text?:string;exact?:boolean;timeout_ms?:number;poll_ms?:number};
export type SequenceProgress={region?:Region;max_unchanged_ms:number};
export type SequenceConditions={start_on?:SequenceCondition;abort_on?:SequenceCondition;progress_check?:SequenceProgress};
type Sample={scope:string;hash?:string;text?:string;qpc:string;frequency:string};
export type PreparedConditions={scope?:string;start?:Sample;abort?:Sample;progress?:Sample;observations:number;options:SequenceConditions};
const obj=(v:unknown):Obj|undefined=>v&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const uint=(v:unknown,nonzero=false):v is string=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=18446744073709551615n&&(!nonzero||BigInt(v)>0n);
const delay=(ms:number)=>new Promise<void>(r=>setTimeout(r,ms));
const failed=(error:string):CliResult=>({ok:false,json:{ok:false,input_injected:false,error},argv:[],stderr:'',timedOut:false,exitCode:null});
export function canonicalSequenceScope(value:unknown):string|undefined{
 const s=obj(value);if(!s||typeof s.hwnd!=='string'||!/^0x[0-9a-f]+$/i.test(s.hwnd)||BigInt(s.hwnd)===0n||typeof s.thread!=='number'||!Number.isInteger(s.thread)||s.thread<1||s.thread>4294967295||!uint(s.adoption_generation,true)||!uint(s.geometry_epoch)||!uint(s.lifetime_cookie,true)||typeof s.client_w!=='number'||typeof s.client_h!=='number'||!Number.isInteger(s.client_w)||!Number.isInteger(s.client_h)||s.client_w<1||s.client_h<1||s.client_w>32768||s.client_h>32768||typeof s.geometry_witness!=='string')return;
 return JSON.stringify({hwnd:s.hwnd,thread:s.thread,adoption_generation:s.adoption_generation,geometry_epoch:s.geometry_epoch,lifetime_cookie:s.lifetime_cookie,client_w:s.client_w,client_h:s.client_h,geometry_witness:s.geometry_witness});
}
function conditionError(c:SequenceCondition):string|undefined{
 if(c.kind==='text'&&(!c.text||!c.text.normalize('NFKC').trim()))return 'text condition requires nonempty normalized text';
 if(c.kind!=='text'&&(c.text!==undefined||c.exact!==undefined))return 'text/exact apply only to text conditions';
 if(c.kind==='region_change'&&!c.region)return 'region_change requires a client region';
}
async function sample(ctx:SequenceWaitContext,c:SequenceCondition,budget=1000):Promise<Sample>{
 if(ctx.currentEpoch()!==ctx.epoch)throw new Error('attachment changed');
 let r:CliResult;
 if(c.kind==='text')r=await runVinput([...ctx.prefix,'ocr','--lines-only','--observation-only=true',...(c.region?['--region',`${c.region.x},${c.region.y},${c.region.w},${c.region.h}`]:[])],budget);
 else r=await runVinput([...ctx.prefix,'json',JSON.stringify({cmd:'frame-hash',observation_only:true,timeout_ms:Math.min(500,budget),...(c.region?{region:`${c.region.x},${c.region.y},${c.region.w},${c.region.h}`}:{})})],budget);
 if(ctx.currentEpoch()!==ctx.epoch)throw new Error('attachment changed during observation');
 const s=obj(r.json),scope=canonicalSequenceScope(s?.capture_scope);
 if(!r.ok||!s||!scope||s.capture_space_updated!==false||s.timing_clock!=='QueryPerformanceCounter'||!uint(s.capture_qpc_ticks,true)||!uint(s.qpc_frequency,true))throw new Error('scoped QPC capture observation unavailable');
 if(c.kind==='text'&&typeof s.text!=='string')throw new Error('OCR text observation unavailable');
 if(c.kind!=='text'&&(typeof s.hash!=='string'||!s.hash))throw new Error('frame hash unavailable');
 return {scope,qpc:s.capture_qpc_ticks,frequency:s.qpc_frequency,...(c.kind==='text'?{text:s.text as string}:{hash:s.hash as string})};
}
function match(c:SequenceCondition,now:Sample,before:Sample):boolean{
 if(now.scope!==before.scope||now.frequency!==before.frequency||BigInt(now.qpc)<BigInt(before.qpc))throw new Error('capture scope or QPC clock changed');
 if(c.kind==='text'){const norm=(s:string)=>s.normalize('NFKC').toLowerCase().replace(/\s+/g,' ').trim();const actual=norm(now.text??''),want=norm(c.text??'');return c.exact?actual===want:actual.includes(want);}
 return now.hash!==before.hash;
}
export async function prepareSequenceConditions(ctx:SequenceWaitContext,options:SequenceConditions):Promise<PreparedConditions|CliResult>{
 for(const c of [options.start_on,options.abort_on])if(c){const error=conditionError(c);if(error)return failed(error);}
 const status=await runVinput([...ctx.prefix,'status'],1000),s=obj(status.json);
 if(!status.ok||s?.pid!==ctx.pid||s.sequence_guard_version!==1||s.sequence_timing_version!==1||s.input_scope_version!==1||ctx.currentEpoch()!==ctx.epoch)return failed('conditional sequences require matching sequence_guard_version/timing_version/input_scope_version1; no sequence was sent');
 const p:PreparedConditions={observations:0,options};
 try{
  if(options.start_on){const c=options.start_on,before=await sample(ctx,c);p.observations++;const end=performance.now()+(c.timeout_ms??10000);let seen=before;
   while(c.kind!=='text'||!match(c,seen,before)){
    const left=end-performance.now();if(left<=0)return failed('start_on event was not observed before its bounded deadline; no sequence was sent');
    await delay(Math.min(c.poll_ms??100,left));seen=await sample(ctx,c,Math.max(1,Math.min(1000,Math.ceil(end-performance.now()))));p.observations++;
    if(performance.now()>end)return failed('start_on observation exceeded its deadline; no sequence was sent');
    if(match(c,seen,before))break;
   }p.start=seen;p.scope=seen.scope;
  }
  if(options.abort_on){p.abort=await sample(ctx,options.abort_on);p.observations++;if(p.scope&&p.scope!==p.abort.scope)throw new Error('capture scope changed before scheduling');p.scope=p.abort.scope;if(options.abort_on.kind==='text'&&match(options.abort_on,p.abort,p.abort))return failed('abort_on text is already visible; no sequence was sent');}
  if(options.progress_check){p.progress=await sample(ctx,{kind:'frame_change',region:options.progress_check.region});p.observations++;if(p.scope&&p.scope!==p.progress.scope)throw new Error('capture scope changed before scheduling');p.scope=p.progress.scope;}
  return p;
 }catch(e){return failed(`sequence event observation failed: ${String(e)}; no sequence was sent`);}
}
export async function guardSequence(initial:CliResult,ctx:SequenceWaitContext,p:PreparedConditions,timeoutMs?:number):Promise<CliResult>{
 const admitted=sequenceJob(initial.json,ctx,true),guardMs=4000,start=performance.now();let observations=p.observations,serial=0,last:Job|undefined=admitted,lastProgress=start,progress=p.progress;
 const confirmed=!!admitted&&admitted.sequence_guard_version===1&&admitted.observer_guarded===true&&admitted.observer_guard_ms===guardMs&&admitted.capture_scope_checked===true&&admitted.guard_observation_seq===0;
 const end=start+(timeoutMs??Math.min(120000,Number(admitted?.requested_ms??0)+5000));
 const finish=(outcome:string,error:string):CliResult=>{const r=sequenceWaitError(initial,outcome,error,performance.now()-start,timeoutMs,last);return {...r,json:{...obj(r.json),sequence_guard:{requested:true,outcome,observations,native_guard_confirmed:confirmed,heartbeat_ms:confirmed?guardMs:null,last_ack_serial:serial,unacknowledged_renewals_max:confirmed?1:null,game_progress_measured:false,start_event:p.start??null,scope:'native guard expires4000ms after the latest processed renewal; one unacknowledged request can extend it once before expiry; worker execution latency is not a hard real-time bound; no replay or uncertain cancellation retry'}}};};
 if(!confirmed)return finish('guard_ack_unavailable','guarded sequence acknowledgement is invalid; no cancellation or input was retried; native expiry was not confirmed');
 async function heartbeat(abort=false,reason=''):Promise<boolean>{
  if(ctx.currentEpoch()!==ctx.epoch)throw new Error('attachment changed; exact old job was not controlled');
  const next=serial+1,r=await runVinput([...ctx.prefix,'json',JSON.stringify({cmd:'sequence-observe',id:admitted!.job_id,observation_seq:next,abort,...(abort?{reason}:{})})],1000),outer=obj(r.json);
  if(ctx.currentEpoch()!==ctx.epoch)throw new Error('attachment changed while observing; no follow-up control was sent');
  const observed=sequenceJob(outer?.job,ctx,false,admitted!.job_id);
  if(!r.ok||outer?.guard_ack!==true||outer.observation_seq!==next||!observed||outer.guard_renewed!==!observed.job_complete||observed.sequence_guard_version!==1||observed.observer_guarded!==true||observed.observer_guard_ms!==guardMs||observed.capture_scope_checked!==true||!Number.isInteger(observed.guard_observation_seq)||Number(observed.guard_observation_seq)<0||(!observed.job_complete&&observed.guard_observation_seq!==next))throw new Error('exact-job observation acknowledgement unavailable; native expiry remains the cleanup bound');
  serial=next;last=observed;return observed.job_complete;
 }
 try{
  for(;;){
   if(await heartbeat())return finish(last!.job_state,`sequence ${last!.job_state}: ${String(last!.job_reason??'')}`);
   let reason='';
   if(performance.now()>=end)reason='observer_error';
   else try{
    if(p.options.abort_on&&p.abort){const seen=await sample(ctx,p.options.abort_on);observations++;if(match(p.options.abort_on,seen,p.abort))reason=p.options.abort_on.kind;}
    if(!reason&&p.options.progress_check&&progress){if(await heartbeat())return finish(last!.job_state,`sequence ${last!.job_state}`);const seen=await sample(ctx,{kind:'frame_change',region:p.options.progress_check.region});observations++;if(seen.scope!==progress.scope||seen.frequency!==progress.frequency||BigInt(seen.qpc)<BigInt(progress.qpc))throw new Error('capture scope changed');if(seen.hash!==progress.hash){progress=seen;lastProgress=performance.now();}if(performance.now()-lastProgress>=p.options.progress_check.max_unchanged_ms)reason='progress_stalled';}
   }catch{reason='observer_error';}
   if(reason){await heartbeat(true,reason);if(!last!.job_complete)throw new Error('abort acknowledgement did not establish a terminal job');const outcome=last!.job_state==='completed'?'completed':last!.job_reason===`sequence_abort_${reason}`?`aborted_${reason}`:last!.job_state;return finish(outcome,`sequence terminal: ${String(last!.job_reason??reason)}`);}
   await delay(Math.max(0,Math.min(p.options.abort_on?.poll_ms??100,500,end-performance.now())));
  }
 }catch(e){return finish('observer_unavailable',`sequence observer failed: ${String(e)}; no input/control was retried; native guard expires4000ms after its latest processed renewal, including at most one unacknowledged renewal`);}
}
