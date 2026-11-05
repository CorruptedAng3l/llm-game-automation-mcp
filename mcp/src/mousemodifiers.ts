import type { CliResult } from './vinput.js';
type Content = {type:'text';text:string}|{type:'image';data:string;mimeType:string};
type ToolReply = {content:Content[];isError?:boolean};
type Run = (args:string[],timeout:number)=>Promise<CliResult>;
type Obj=Record<string,unknown>;
const aliases:Record<string,number[]>={ctrl:[17,162,163],shift:[16,160,161],alt:[18,164,165]};
const errorReply=(error:string):ToolReply=>({content:[{type:'text',text:JSON.stringify({ok:false,error})}],isError:true});
const locks=new Map<number,Promise<void>>();
const object=(x:unknown):Obj|undefined=>x!==null&&typeof x==='object'&&!Array.isArray(x)?x as Obj:undefined;
function ownedId(value:unknown,pid:number):value is string{
  if(typeof value!=='string'||value.length>52)return false;
  const parts=/^([1-9][0-9]{0,9}):([1-9][0-9]{0,19}):([1-9][0-9]{0,19})$/.exec(value);
  return !!parts&&parts[1]===String(pid)&&BigInt(parts[1])<=4294967295n&&BigInt(parts[2])<=18446744073709551615n&&BigInt(parts[3])<=18446744073709551615n;
}
function attach(value:ToolReply,receipt:Obj,problem?:string):ToolReply{
  let attached=false;
  const content=value.content.map(c=>{
    if(attached||c.type!=='text')return c;
    try{const j=object(JSON.parse(c.text));if(!j)return c;attached=true;return {type:'text' as const,text:JSON.stringify({...j,modifiers:receipt,...(problem?{ok:false,modifier_scope_error:problem}:{})})};}catch{return c;}
  });
  if(!attached)content.push({type:'text',text:JSON.stringify({modifiers:receipt,...(problem?{ok:false,modifier_scope_error:problem}:{})})});
  return {...value,content,...(problem?{isError:true}:{})};
}
/** One bounded native key job owns newly requested modifiers. Exact job cancel
 * uses native resource generations, so cleanup cannot release a newer key hold.
 * Native expiry also releases the job if the MCP process disappears. */
export async function withMouseModifiers(pid:number,modifiers:readonly string[]|undefined,action:()=>Promise<ToolReply>,run:Run,prefix:(pid:number)=>string[],holdMs=60000):Promise<ToolReply>{
  if(!modifiers?.length)return action();
  if(modifiers.length>3||new Set(modifiers).size!==modifiers.length||modifiers.some(k=>!aliases[k])||!Number.isSafeInteger(holdMs)||holdMs<100||holdMs>118000)return errorReply('modifiers must be unique ctrl, shift or alt names with modifier_hold_ms 100..118000; no input was sent');
  const previous=locks.get(pid)??Promise.resolve();let unlock!:()=>void;const mine=new Promise<void>(r=>{unlock=r;});locks.set(pid,mine);
  await previous;
  try{return await scoped(pid,modifiers,action,run,prefix,holdMs);}
  finally{unlock();if(locks.get(pid)===mine)locks.delete(pid);}
}
async function scoped(pid:number,modifiers:readonly string[],action:()=>Promise<ToolReply>,run:Run,prefix:(pid:number)=>string[],holdMs:number):Promise<ToolReply>{
  const pre=prefix(pid),ownerIndex=pre.indexOf('--owner'),owner=ownerIndex>=0?pre[ownerIndex+1]:undefined;
  if(!owner)return errorReply('temporary mouse modifiers require an explicit native owner; no input was sent');
  const state=await run([...pre,'status'],15000),down=object(state.json)?.virtual_down;
  if(!state.ok||!Array.isArray(down)||!down.every(k=>Number.isInteger(k)&&k>=1&&k<=255))return errorReply('cannot establish pre-held virtual modifier state; updated status.virtual_down is required; no modifier or mouse input was sent');
  const already=modifiers.filter(k=>aliases[k].some(v=>down.includes(v))),fresh=modifiers.filter(k=>!already.includes(k));
  const receipt:Obj={requested:[...modifiers],already_held:already,temporary:fresh,native_hold_ms:holdMs,scope:'bounded native key job; exact-ID generation-aware cancellation; pre-held aliases preserved; modified mouse calls serialized per PID in this MCP process'};
  if(!fresh.length)return attach(await action(),receipt);
  let jobId:string|undefined,value:ToolReply|undefined,problem:string|undefined;
  const duration=holdMs+30*(fresh.length-1);
  const owned=(j:Obj|undefined):boolean=>!!j&&ownedId(j.job_id,pid)&&j.owner===owner&&j.requested_ms===duration;
  const active=(j:Obj|undefined):boolean=>owned(j)&&j!.job_id===jobId&&j!.job_state==='running'&&j!.job_complete===false&&j!.hold_ready===true;
  try{
    const admitted=await run([...pre,'key',fresh.join('+'),String(holdMs),'--async'],15000),initial=object(admitted.json);receipt.admission=admitted.json;
    if(owned(initial)&&initial!.accepted===true)jobId=initial!.job_id as string;
    if(!admitted.ok||!jobId){problem='bounded modifier job admission was refused or unconfirmed; mouse action was not sent; no uncertain input was retried';value=errorReply(problem);}
    else{
      const deadline=performance.now()+2000;let latest=initial;
      while(!active(latest)&&performance.now()<deadline){
        const r=await run([...pre,'job-status',jobId],Math.max(1,Math.ceil(deadline-performance.now())));latest=r.ok?object(object(r.json)?.job):undefined;
        if(!latest||latest.job_id!==jobId||latest.job_complete===true)break;
        if(!active(latest))await new Promise(r=>setTimeout(r,10));
      }
      receipt.before_action=latest;
      if(!active(latest)){problem='exact modifier job did not reach an active hold within 2000ms; mouse action was not sent';value=errorReply(problem);}
      else{
        receipt.action_attempted=true;value=await action();
        const r=await run([...pre,'job-status',jobId],15000),after=r.ok?object(object(r.json)?.job):undefined;receipt.after_action=after??r.json;
        if(!active(after))problem='modifier job expired, was superseded, or could not be observed after the action; modifier coverage through the whole action is unconfirmed';
      }
    }
  }catch(e){problem=`modified mouse action failed: ${(e as Error).message}`;value=errorReply(problem);}
  finally{
    if(jobId){
      try{const cancelled=await run([...pre,'job-cancel',jobId],15000),j=object(object(cancelled.json)?.job);receipt.cancellation=cancelled.json;
        if(!cancelled.ok||!owned(j)||j!.job_id!==jobId||j!.job_complete!==true){receipt.release_confirmed=false;problem??='exact modifier job cancellation was unconfirmed; native deadline remains the backstop; no key_up or global release was sent';}
        else receipt.release_confirmed=true;
      }catch(e){receipt.release_confirmed=false;receipt.cancel_error=(e as Error).message;problem??='exact modifier job cancellation failed; native deadline remains the backstop; no key_up or global release was sent';}
    }else receipt.release_confirmed=null;
  }
  return attach(value??errorReply(problem??'modified action returned no receipt'),receipt,problem);
}
