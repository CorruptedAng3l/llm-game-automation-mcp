import {canonicalSequenceScope} from './sequenceconditions.js';
import type {CliResult} from './vinput.js';
type Region={x:number;y:number;w:number;h:number};
export type ClickVerify={region:Region;timeout_ms?:number;poll_ms?:number};
type Run=(argv:string[],budget:number)=>Promise<CliResult>;
export async function clickScopeCapability(pid:number,run:Run,prefix:string[]):Promise<CliResult>{
 const r=await run([...prefix,'json',JSON.stringify({cmd:'ping',pointer_scope_support:true})],1000),j=r.json as Record<string,unknown>;
 return r.ok&&j?.pid===pid&&j?.pointer_scope_version===1?r:{...r,ok:false,json:{ok:false,input_injected:false,capability_receipt:r.json,...(j?.anticheat_refused===true?{anticheat_refused:true}:{}),error:'matching pointer-scope capability is unconfirmed; no click or temporary modifier was sent'}};
}
export async function verifiedClick(pid:number,options:ClickVerify,run:Run,prefix:string[],sameAttachment:()=>boolean,action:(scope:string)=>Promise<CliResult>,mappedScope?:string,checkedCapability?:CliResult):Promise<CliResult>{
 const empty={argv:[],stderr:'',timedOut:false,exitCode:1};let delivered:CliResult|undefined,baseline:Record<string,unknown>|undefined,failureReceipt:unknown;let samples=0;
 const read=async(deadline?:number)=>{
  if(!sameAttachment())throw new Error('attachment changed');
  const remaining=deadline===undefined?2000:Math.floor(deadline-performance.now());if(remaining<=0)throw new Error('observation deadline elapsed');
  const r=await run([...prefix,'json',JSON.stringify({cmd:'frame-hash',region:`${options.region.x},${options.region.y},${options.region.w},${options.region.h}`,observation_only:true,timeout_ms:Math.min(500,remaining)})],Math.min(2000,remaining));
  if(!sameAttachment())throw new Error('attachment changed');
  if(deadline!==undefined&&performance.now()>deadline){failureReceipt=r.json;throw new Error('observation reply arrived after deadline');}
  const j=r.json as Record<string,unknown>,scope=canonicalSequenceScope(j?.capture_scope);
  if(!r.ok||!scope||j.capture_space_updated!==false||typeof j.hash!=='string'||!/^[0-9a-f]{16}$/i.test(j.hash)){failureReceipt=r.json;throw new Error('scoped frame observation unavailable');}
  samples++;return {j,scope,hash:j.hash};
 };
 try{
  const capability=checkedCapability??await clickScopeCapability(pid,run,prefix);const cap=capability.json as Record<string,unknown>;
  if(!sameAttachment()||!capability.ok||cap?.pid!==pid||cap?.pointer_scope_version!==1){failureReceipt=capability.json;throw new Error('matching pointer-scope capability is unconfirmed');}
  const before=await read();baseline=before.j;if(mappedScope&&mappedScope!==before.scope)throw new Error('capture mapping and current window scope differ');
  if(!sameAttachment())throw new Error('attachment changed before click admission');
  delivered=await action(before.scope);
  const injected=(delivered.json as Record<string,unknown>)?.input_injected;if(!delivered.ok&&injected===false)return delivered;
  const end=performance.now()+(options.timeout_ms??1000);let last=before;
  while(Math.floor(end-performance.now())>0){
   last=await read(end);if(last.scope!==before.scope)throw new Error('window lifetime or geometry changed');
   if(last.hash!==before.hash)return {...delivered,json:{...(delivered.json as object),click_observation:{outcome:'visual_change_observed',changed:true,samples,baseline:before.j,final:last.j,application_response:null,caused_by_click:null,note:'pixel change can include animation; semantic click response is unmeasured'}}};
   await new Promise(resolve=>setTimeout(resolve,Math.min(options.poll_ms??100,Math.max(0,Math.ceil(end-performance.now())))));
  }
  return {...delivered,ok:false,json:{...(delivered.json as object),ok:false,input_receipt:delivered.json,click_observation:{outcome:'no_visual_change',changed:false,samples,baseline:before.j,final:last.j,application_response:null},error:'click delivery receipt retained; selected region did not change within the observation bound; animation state and game acceptance are unmeasured; do not replay automatically'}};
 }catch(error){return {...(delivered??empty),ok:false,json:{ok:false,...(delivered?{input_receipt:delivered.json}:{input_injected:false}),...(failureReceipt?{failure_receipt:failureReceipt,...((failureReceipt as Record<string,unknown>).anticheat_refused===true?{anticheat_refused:true}:{})}:{}),click_observation:{outcome:'unavailable',changed:null,samples,baseline:baseline??null,application_response:null},error:`click observation unavailable: ${String(error)}; ${delivered?'original input was not replayed':'no click was sent'}`}};}
}
