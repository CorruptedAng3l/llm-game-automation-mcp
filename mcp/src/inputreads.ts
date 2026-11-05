import type { CliResult } from './vinput.js';
type Obj=Record<string,unknown>;
const object=(v:unknown):Obj|undefined=>v&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const u64=(v:unknown):v is string=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=18446744073709551615n;
/** Native scheduler completion and reader returns are separate observations. */
export function presentInputReads(result:CliResult):CliResult{
 const native=object(result.json),o=object(native?.input_read_observation);
 if(!native)return result;
 const nested=object(native.job);
 if(result.ok&&native.ok===true&&nested){
  const qualified=presentInputReads({...result,json:nested});
  if(qualified.json!==nested)return {...result,ok:qualified.ok,json:{...native,ok:qualified.ok,job:qualified.json,...(!qualified.ok?{scheduler_ok:true,error:object(qualified.json)?.error}:{})}};
  return result;
 }
 if(!result.ok||native.ok!==true||native.job_state!=='completed'||native.job_complete!==true||native.input_injected!==true)return result;
 if(native.accepted!==false||typeof native.job_id!=='string'||!native.job_id)return result;
 const unavailable=():CliResult=>({...result,json:{...native,input_read_warning:'Native reader evidence is incomplete or inconsistent; scheduler completion does not confirm a reader or application response.'}});
 if(!o||o.version!==1)return unavailable();
 if(o.terminal!==true||o.started!==true||o.frozen!==true||o.application_response!==null||typeof o.applicable!=='boolean'||typeof o.measured!=='boolean')return unavailable();
 if(o.applicable===false&&o.outcome==='not_applicable')return result;
 if(o.measured!==true)return unavailable();
 let observed=false,partial=false;
 if(o.kind==='pad'){
  if(!u64(o.pad_returns)||!Array.isArray(o.keys)||o.keys.length!==0)return unavailable();
  observed=BigInt(o.pad_returns)>0n;
 }else if(o.kind==='keys'){
  if(!Array.isArray(o.keys)||!Number.isInteger(o.keys_requested)||o.keys_requested!==o.keys.length||o.keys.length<1||o.keys.length>256)return unavailable();
  let seen=0;const keys=new Set<number>();
  for(const value of o.keys){const k=object(value);if(!k||typeof k.vk!=='number'||!Number.isInteger(k.vk)||k.vk<1||k.vk>255||keys.has(k.vk)||k.measured!==true||!u64(k.poll_reads)||!u64(k.down_returns)||!u64(k.message_dispatches)||BigInt(k.down_returns)>BigInt(k.poll_reads))return unavailable();keys.add(k.vk);if(BigInt(k.down_returns)>0n)++seen;}
  if(o.keys_observed!==seen)return unavailable();observed=seen===o.keys.length;partial=seen>0&&!observed;
 }else return unavailable();
 const outcome=observed?'observed':partial?'partial':'not_observed';
 if(o.all_requested_observed!==observed||o.outcome!==outcome)return unavailable();
 if(observed)return {...result,json:{...native,reader_return_observed:true,application_response:null}};
 const messages=o.kind==='keys'&&Array.isArray(o.keys)&&o.keys.every(k=>BigInt(object(k)!.message_dispatches as string)>0n);
 return {...result,json:{...native,reader_return_observed:false,application_response:null,...(o.kind==='keys'?{window_messages_dispatched:messages}:{}),
  input_read_warning:(messages?'The requested key messages were dispatched. ':'The native input job completed. ')+(partial?'Some requested keys had no observed polling high-state return. ':'No requested polling high-state return was observed. ')+
   'The game may consume window messages or use an uncovered API. Reader uncertainty does not mean delivery failed or the game ignored the input; inspect input_read_observation and do not replay automatically.'}};
}
