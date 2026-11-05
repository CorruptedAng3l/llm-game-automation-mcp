import {runVinput,defaultCapturePath,type CliResult} from './vinput.js';
import {existsSync,rmSync} from 'node:fs';
import {resolve} from 'node:path';
import {conditionTextMatch} from './conditiontext.js';
import {sequenceJob,sequenceWaitError,type SequenceWaitContext,type Job} from './sequencewait.js';
type Obj=Record<string,unknown>;
type Region={x:number;y:number;w:number;h:number};
export type SequenceCondition={kind:'frame_change'|'region_change'|'text'|'image';region?:Region;text?:string;exact?:boolean;fuzzy?:number;template?:string;threshold?:number;grace_ms?:number;timeout_ms?:number;poll_ms?:number;after_frames?:number;present_timeout_ms?:number};
export type SequenceProgress={region?:Region;max_unchanged_ms:number};
export type SequenceConditions={start_on?:SequenceCondition;abort_on?:SequenceCondition;progress_check?:SequenceProgress};
type Sample={scope:string;hash?:string;text?:string;found?:boolean;qpc:string;frequency:string;frame_qpc_ticks:unknown;frame_index:unknown;receipt:Obj;frame_path?:string};
export type PreparedConditions={scope?:string;start?:Sample;presentTarget?:string;abort?:Sample;progress?:Sample;observations:number;options:SequenceConditions};
const obj=(v:unknown):Obj|undefined=>v&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const uint=(v:unknown,nonzero=false):v is string=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=18446744073709551615n&&(!nonzero||BigInt(v)>0n);
const delay=(ms:number)=>new Promise<void>(r=>setTimeout(r,ms));
const failed=(error:string):CliResult=>({ok:false,json:{ok:false,input_injected:false,error},argv:[],stderr:'',timedOut:false,exitCode:null});
export function canonicalSequenceScope(value:unknown):string|undefined{
 const s=obj(value);if(!s||typeof s.hwnd!=='string'||!/^0x[0-9a-f]+$/i.test(s.hwnd)||BigInt(s.hwnd)===0n||typeof s.thread!=='number'||!Number.isInteger(s.thread)||s.thread<1||s.thread>4294967295||!uint(s.adoption_generation,true)||!uint(s.geometry_epoch)||!uint(s.lifetime_cookie,true)||typeof s.client_w!=='number'||typeof s.client_h!=='number'||!Number.isInteger(s.client_w)||!Number.isInteger(s.client_h)||s.client_w<1||s.client_h<1||s.client_w>32768||s.client_h>32768||typeof s.geometry_witness!=='string')return;
 return JSON.stringify({hwnd:s.hwnd,thread:s.thread,adoption_generation:s.adoption_generation,geometry_epoch:s.geometry_epoch,lifetime_cookie:s.lifetime_cookie,client_w:s.client_w,client_h:s.client_h,geometry_witness:s.geometry_witness});
}
function conditionError(c:SequenceCondition):string|undefined{
 if(c.after_frames!==undefined&&(!Number.isInteger(c.after_frames)||c.after_frames<1||c.after_frames>3600))return 'after_frames must be integer1..3600';
 if(c.present_timeout_ms!==undefined&&(!Number.isInteger(c.present_timeout_ms)||c.present_timeout_ms<100||c.present_timeout_ms>60000))return 'present_timeout_ms must be integer100..60000';
 if(c.fuzzy!==undefined&&(!Number.isFinite(c.fuzzy)||c.fuzzy<=0||c.fuzzy>1))return 'fuzzy must be in (0,1]';
 if(c.grace_ms!==undefined&&(!Number.isInteger(c.grace_ms)||c.grace_ms<0||c.grace_ms>30000))return 'grace_ms must be integer 0..30000';
 if(c.kind==='text'&&(!c.text||!c.text.normalize('NFKC').trim()))return 'text condition requires nonempty normalized text';
 if(c.kind!=='text'&&(c.text!==undefined||c.exact!==undefined||c.fuzzy!==undefined))return 'text/exact/fuzzy apply only to text conditions';
 if(c.kind==='image'&&!c.template?.trim())return 'image condition requires a PNG template';
 if(c.kind!=='image'&&(c.template!==undefined||c.threshold!==undefined))return 'template/threshold apply only to image conditions';
 if(c.kind==='region_change'&&!c.region)return 'region_change requires a client region';
}
function discard(s:Sample){if(s.frame_path)try{rmSync(s.frame_path,{force:true});}catch{/* Only the generated observation file is eligible. */}}
async function sample(ctx:SequenceWaitContext,c:SequenceCondition,budget=1000,retain=false,before?:Sample):Promise<Sample>{
 if(ctx.currentEpoch()!==ctx.epoch)throw new Error('attachment changed');
 let r:CliResult;const path=retain?defaultCapturePath(ctx.pid,'png','_abort_observation'):undefined;
 try{
 if(c.kind==='text')r=await runVinput([...ctx.prefix,'ocr','--lines-only','--observation-only=true',...(path?['--observation-capture',path]:[]),...(c.region?['--region',`${c.region.x},${c.region.y},${c.region.w},${c.region.h}`]:[])],budget);
 else if(c.kind==='image')r=await runVinput([...ctx.prefix,'find-image',resolve(c.template!),'--threshold',String(c.threshold??.95),'--no-scale','--preserve-map','--observation-only=true',...(path?['--observation-capture',path]:[]),...(c.region?['--region',`${c.region.x},${c.region.y},${c.region.w},${c.region.h}`]:[])],budget);
 else r=await runVinput([...ctx.prefix,'json',JSON.stringify({cmd:'frame-hash',observation_only:true,timeout_ms:Math.min(500,budget),...(path&&before?.hash?{path,retain_changed_from:before.hash}:{}),...(c.region?{region:`${c.region.x},${c.region.y},${c.region.w},${c.region.h}`}:{})})],budget);
 if(ctx.currentEpoch()!==ctx.epoch)throw new Error('attachment changed during observation');
 const s=obj(r.json),scope=canonicalSequenceScope(s?.capture_scope);
 const imageAbsent=c.kind==='image'&&s?.ok===false&&s.match_count===0&&Array.isArray(s.matches)&&s.matches.length===0&&s.error==='template not found at the threshold';
 if((!r.ok&&!imageAbsent)||!s||!scope||(c.kind==='image'?s.capture_space_preserved!==true:s.capture_space_updated!==false)||s.timing_clock!=='QueryPerformanceCounter'||!uint(s.capture_qpc_ticks,true)||!uint(s.qpc_frequency,true))throw new Error('scoped QPC capture observation unavailable');
 if(c.kind==='text'&&typeof s.text!=='string')throw new Error('OCR text observation unavailable');
 if(c.kind==='image'&&(!Number.isInteger(s.match_count)||Number(s.match_count)<0||!Array.isArray(s.matches)||s.match_count===0&&s.matches.length!==0||Number(s.match_count)>0&&!s.matches.length||s.matches.some(m=>typeof obj(m)?.score!=='number'||!Number.isFinite(obj(m)!.score)||Number(obj(m)!.score)<(c.threshold??.95)||Number(obj(m)!.score)>1)))throw new Error('image observation unavailable');
 if(c.kind!=='text'&&c.kind!=='image'&&(typeof s.hash!=='string'||!/^[0-9a-f]{16}$/i.test(s.hash)))throw new Error('frame hash unavailable');
 return {scope,qpc:s.capture_qpc_ticks,frequency:s.qpc_frequency,frame_qpc_ticks:uint(s.frame_qpc_ticks,true)?s.frame_qpc_ticks:null,frame_index:s.captured_present??s.frame??null,receipt:s,...(path&&s.observation_frame_path===path&&existsSync(path)?{frame_path:path}:{}),...(c.kind==='text'?{text:s.text as string}:c.kind==='image'?{found:Number(s.match_count)>0}:{hash:s.hash as string})};
 }catch(e){if(path)try{rmSync(path,{force:true});}catch{/* Generated file only. */}throw e;}
}
function match(c:SequenceCondition,now:Sample,before:Sample):boolean{
 if(now.scope!==before.scope||now.frequency!==before.frequency||BigInt(now.qpc)<BigInt(before.qpc))throw new Error('capture scope or QPC clock changed');
 if(c.kind==='text')return conditionTextMatch(now.text??'',c.text??'',c.exact,c.fuzzy);
 if(c.kind==='image')return now.found===true;
 return now.hash!==before.hash;
}
export async function prepareSequenceConditions(ctx:SequenceWaitContext,options:SequenceConditions):Promise<PreparedConditions|CliResult>{
 for(const c of [options.start_on,options.abort_on])if(c){const error=conditionError(c);if(error)return failed(error);}
 if(options.start_on?.grace_ms)return failed('grace_ms applies only to abort_on; no sequence was sent');
 if(options.abort_on?.after_frames!==undefined||options.abort_on?.present_timeout_ms!==undefined)return failed('after_frames/present_timeout_ms apply only to start_on; no sequence was sent');
 if(options.start_on?.present_timeout_ms!==undefined&&options.start_on.after_frames===undefined)return failed('present_timeout_ms requires start_on.after_frames; no sequence was sent');
 const status=await runVinput([...ctx.prefix,'status'],1000),s=obj(status.json);
 if(!status.ok||s?.pid!==ctx.pid||s.sequence_guard_version!==1||s.sequence_timing_version!==1||s.input_scope_version!==1||ctx.currentEpoch()!==ctx.epoch)return failed('conditional sequences require matching sequence_guard_version/timing_version/input_scope_version1; no sequence was sent');
 if((options.start_on?.kind==='image'||options.abort_on?.kind==='image')&&s.sequence_abort_image_version!==1)return failed('image sequence observation requires matching native sequence_abort_image_version1; no sequence was sent');
 if(options.start_on?.after_frames!==undefined&&s.sequence_frame_anchor_version!==1)return failed('start_on.after_frames requires native sequence_frame_anchor_version1; no sequence was sent');
 const p:PreparedConditions={observations:0,options};
 try{
  if(options.start_on){const c=options.start_on,before=await sample(ctx,c);p.observations++;const end=performance.now()+(c.timeout_ms??10000);let seen=before;
   while((c.kind!=='text'&&c.kind!=='image')||!match(c,seen,before)){
    const left=end-performance.now();if(left<=0)return failed('start_on event was not observed before its bounded deadline; no sequence was sent');
    await delay(Math.min(c.poll_ms??100,left));seen=await sample(ctx,c,Math.max(1,Math.min(1000,Math.ceil(end-performance.now()))));p.observations++;
    if(performance.now()>end)return failed('start_on observation exceeded its deadline; no sequence was sent');
    if(match(c,seen,before))break;
   }p.start=seen;p.scope=seen.scope;
   if(c.after_frames!==undefined){const n=seen.frame_index;if(!uint(seen.frame_qpc_ticks,true)||typeof n!=='number'||!Number.isSafeInteger(n)||n<1||n+c.after_frames>2147483647)throw new Error('Present anchor requires a native captured frame index and QPC; PrintWindow is unavailable');p.presentTarget=String(n+c.after_frames);}
  }
  if(options.abort_on){p.abort=await sample(ctx,options.abort_on);p.observations++;if(p.scope&&p.scope!==p.abort.scope)throw new Error('capture scope changed before scheduling');p.scope=p.abort.scope;if(!options.abort_on.grace_ms&&(options.abort_on.kind==='text'||options.abort_on.kind==='image')&&match(options.abort_on,p.abort,p.abort))return failed('abort_on text/image is already visible; no sequence was sent');}
  if(options.progress_check){p.progress=await sample(ctx,{kind:'frame_change',region:options.progress_check.region});p.observations++;if(p.scope&&p.scope!==p.progress.scope)throw new Error('capture scope changed before scheduling');p.scope=p.progress.scope;}
  return p;
 }catch(e){return failed(`sequence event observation failed: ${String(e)}; no sequence was sent`);}
}
export async function guardSequence(initial:CliResult,ctx:SequenceWaitContext,p:PreparedConditions,timeoutMs?:number):Promise<CliResult>{
 const admitted=sequenceJob(initial.json,ctx,true),guardMs=4000,start=performance.now();let observations=p.observations,serial=0,last:Job|undefined=admitted,lastProgress=start,progress=p.progress;
 const grace=p.options.abort_on?.grace_ms??0;let runningAt:number|undefined,armed=grace===0,abortBefore=p.abort,matched:Sample|undefined,stepBefore:unknown=null,observerError:string|undefined;
 const confirmed=!!admitted&&admitted.sequence_guard_version===1&&admitted.observer_guarded===true&&admitted.observer_guard_ms===guardMs&&admitted.capture_scope_checked===true&&admitted.guard_observation_seq===0;
 const end=start+(timeoutMs??Math.min(120000,Number(admitted?.requested_ms??0)+Number(admitted?.present_anchor_timeout_ms??0)+5000));
 const finish=(outcome:string,error:string):CliResult=>{const r=sequenceWaitError(initial,outcome,error,performance.now()-start,timeoutMs,last);return {...r,json:{...obj(r.json),sequence_guard:{requested:true,outcome,observations,native_guard_confirmed:confirmed,heartbeat_ms:confirmed?guardMs:null,last_ack_serial:serial,unacknowledged_renewals_max:confirmed?1:null,game_progress_measured:false,start_event:p.start??null,grace_ms:grace,observer_armed:armed,matched_observation:matched??null,matched_frame_path:matched?.frame_path??null,matched_frame_available:!!matched?.frame_path,step_index_before_observation:stepBefore,step_index_at_abort:matched?last?.sequence_step??null:null,abort_qpc_ticks:matched&&uint(last?.finished_qpc_ticks,true)?last!.finished_qpc_ticks:null,abort_confirmed:matched?last?.job_complete===true&&String(last.job_reason??'').startsWith('sequence_abort_'):false,...(observerError?{observer_error:observerError}:{}),scope:'native guard expires4000ms after the latest processed renewal; one unacknowledged request can extend it once before expiry; matched pixels and native abort cleanup have separate QPC/step receipts; worker/observer latency is not a hard real-time bound; no replay or uncertain cancellation retry'}}};};
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
   const running=last!.job_state==='running'&&last!.sequence_waiting_for_present!==true;
   if(running&&runningAt===undefined){runningAt=performance.now();lastProgress=runningAt;}
   let reason='';
   if(performance.now()>=end)reason='observer_error';
   else try{
    if(p.options.abort_on&&abortBefore&&running&&(armed||runningAt!==undefined&&performance.now()-runningAt>=grace)){
      const c=p.options.abort_on;stepBefore=last!.sequence_step??null;const seen=await sample(ctx,c,1000,true,abortBefore);observations++;let compare=true;
      if(!armed){armed=true;abortBefore=seen;if(c.kind==='frame_change'||c.kind==='region_change')compare=false;}
      if(compare&&match(c,seen,abortBefore)){reason=c.kind;matched=seen;}else discard(seen);
    }
    if(!reason&&p.options.progress_check&&progress&&running){if(await heartbeat())return finish(last!.job_state,`sequence ${last!.job_state}`);const seen=await sample(ctx,{kind:'frame_change',region:p.options.progress_check.region});observations++;if(seen.scope!==progress.scope||seen.frequency!==progress.frequency||BigInt(seen.qpc)<BigInt(progress.qpc))throw new Error('capture scope changed');if(seen.hash!==progress.hash){progress=seen;lastProgress=performance.now();}if(performance.now()-lastProgress>=p.options.progress_check.max_unchanged_ms)reason='progress_stalled';}
   }catch(e){reason='observer_error';observerError=String(e);}
   if(reason){await heartbeat(true,reason);if(!last!.job_complete)throw new Error('abort acknowledgement did not establish a terminal job');const outcome=last!.job_state==='completed'?'completed':last!.job_reason===`sequence_abort_${reason}`?`aborted_${reason}`:last!.job_state;return finish(outcome,`sequence terminal: ${String(last!.job_reason??reason)}`);}
   await delay(Math.max(0,Math.min(p.options.abort_on?.poll_ms??(p.options.abort_on?.kind==='text'?100:20),500,end-performance.now())));
  }
 }catch(e){return finish('observer_unavailable',`sequence observer failed: ${String(e)}; no input/control was retried; native guard expires4000ms after its latest processed renewal, including at most one unacknowledged renewal`);}
}
