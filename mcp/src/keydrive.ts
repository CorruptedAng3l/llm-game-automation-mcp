import {z} from 'zod';
import type {CameraContext} from './camerahelpers.js';
import type {CliResult} from './vinput.js';
type Obj=Record<string,unknown>;
type Reply={content:({type:'text';text:string}|{type:'image';data:string;mimeType:string})[];isError?:boolean};
type Capture=Parameters<CameraContext['captureError']>[0];
type Register=<S extends z.ZodRawShape>(name:string,title:string,description:string,shape:S,run:(a:z.objectOutputType<S,z.ZodTypeAny>)=>Promise<Reply>,readOnly?:boolean)=>void;
const phase=z.object({ms:z.number().int().min(10).max(60000),throttle:z.boolean().optional(),steer:z.number().finite().min(-1).max(1).optional(),brake:z.boolean().optional()}).strict();
const bindings=z.object({throttle:z.string().min(1).max(128).optional(),left:z.string().min(1).max(32).optional(),right:z.string().min(1).max(32).optional(),brake:z.string().min(1).max(32).optional()}).strict();
export type KeyDriveOptions=Capture&{phases:z.infer<typeof phase>[];bindings?:z.infer<typeof bindings>;period_ms?:number;async?:boolean;replace?:boolean;max_lateness_ms?:number};
const object=(v:unknown):Obj|undefined=>v!==null&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const out=(json:Obj):Reply=>({content:[{type:'text',text:JSON.stringify(json)}],isError:json.ok!==true});
export function sequencePwmKeyError(chord:string):string|undefined{
 // Only screen the known unsupported lower VK range. The native parser stays
 // authoritative for the complete alias table and chord syntax.
 for(const part of chord.toLowerCase().split('+')){
  const key=part.startsWith('vk_')?part.slice(3):part;
  const low=['lbutton','rbutton','cancel','mbutton','xbutton1','xbutton2'].includes(key)||/^0x[0-9a-f]+$/.test(key)&&Number.parseInt(key.slice(2),16)<8||/^vk[0-9]+$/.test(key)&&Number(key.slice(2))<8;
  if(low)return 'PWM supports keyboard VK8..255 only; mouse and lower reserved values are unsupported; no input was sent';
 }
 return undefined;
}
function exactReceipt(value:unknown,ctx:CameraContext,pid:number,duration:number,period:number,count:number,async:boolean):boolean{
 const j=object(value),id=typeof j?.job_id==='string'?/^([1-9][0-9]{0,9}):([1-9][0-9]{0,19}):([1-9][0-9]{0,19})$/.exec(j.job_id):null;
 return !!j&&!!id&&BigInt(id[1])===BigInt(pid)&&BigInt(id[2])<=18446744073709551615n&&BigInt(id[3])<=18446744073709551615n&&
  j.ok===true&&j.owner===ctx.owner&&j.job_kind==='key_drive'&&j.requested_ms===duration&&j.key_drive_version===1&&j.key_drive_period_ms===period&&j.key_drive_phases===count&&j.capture_scope_checked===true&&
  (async?j.accepted===true&&j.job_complete===false&&['queued','running'].includes(String(j.job_state)):j.accepted===false&&j.job_complete===true&&j.job_state==='completed');
}
export function keyDrivePlan(a:Pick<KeyDriveOptions,'phases'|'period_ms'>):{script:string;duration:number;period:number;phases:Obj[]}|string{
 const period=a.period_ms??100;
 if(!Number.isInteger(period)||period<20||period>1000||!Array.isArray(a.phases)||a.phases.length<1||a.phases.length>128)return 'key_drive needs1..128 phases and period_ms20..1000; no input was sent';
 let duration=0,transitions=0;const planned:Obj[]=[],lines:string[]=[];
 for(const p of a.phases){
  if(!p||!Number.isInteger(p.ms)||p.ms<10||p.ms>60000||typeof(p.throttle??false)!=='boolean'||typeof(p.brake??false)!=='boolean'||!Number.isFinite(p.steer??0)||Math.abs(p.steer??0)>1)return 'invalid finite key_drive phase; no input was sent';
  const duty=Math.abs(p.steer??0),on=Math.round(duty*period);
  if(duty>0&&on===0||duty<1&&on===period||on>0&&on<10||on<period&&period-on<10)return 'PWM on/off intervals must quantize to at least10ms, or use steer0/-1/1; no input was sent';
  duration+=p.ms;if(duration>60000)return 'key_drive total duration exceeds60000ms; no input was sent';
  // Native applies the exact throttle chord length. This early bound cannot
  // accept an over-limit native plan; the authoritative native parser checks it.
  transitions+=(p.throttle?2:0)+(p.brake?2:0)+(on===0?0:on===period?2:2*Math.ceil(p.ms/period));
  if(transitions>4096)return 'key_drive exceeds4096 potential transitions; no input was sent';
  lines.push(`drive ${p.ms} ${p.throttle?1:0} ${p.steer??0} ${p.brake?1:0}`);
  planned.push({ms:p.ms,throttle:p.throttle??false,brake:p.brake??false,steer:p.steer??0,steer_on_ms:on,effective_steer_duty:Math.sign(p.steer??0)*on/period});
 }
 return {script:lines.join('\n')+'\n',duration,period,phases:planned};
}
export async function keyDrive(ctx:CameraContext,a:KeyDriveOptions):Promise<Reply>{
 const parsed=keyDrivePlan(a);if(typeof parsed==='string')return out({ok:false,input_injected:false,error:parsed});
 const invalid=ctx.captureError(a);if(invalid)return out({ok:false,input_injected:false,error:invalid});
 const epoch=ctx.epoch(a.pid);let attempted=false,receipt:CliResult|undefined;
 try{
  const capability=await ctx.run([...ctx.at(a.pid),'json',JSON.stringify({cmd:'ping',input_job_support:true})],2000),c=object(capability.json);
  if(ctx.epoch(a.pid)!==epoch)throw Error('attachment changed before key_drive; no input was sent');
  if(!capability.ok||c?.ok!==true||c.pid!==a.pid||c.key_drive_version!==1)throw Error('matching target lacks key_drive_version1; no input was sent');
  const b={throttle:'w',left:'a',right:'d',brake:'s',...a.bindings};
  const command={cmd:'key-drive',script:parsed.script,period_ms:parsed.period,throttle_key:b.throttle,left_key:b.left,right_key:b.right,brake_key:b.brake,async:a.async??false,replace:a.replace??false,...(a.max_lateness_ms!==undefined?{max_lateness_ms:a.max_lateness_ms}:{})};
  attempted=true;receipt=await ctx.run([...ctx.at(a.pid),'json',JSON.stringify(command)],a.async?8000:parsed.duration+15000);
  const j=object(receipt.json)??{},refused=!receipt.ok&&j.input_injected===false;
  if(receipt.ok&&!exactReceipt(j,ctx,a.pid,parsed.duration,parsed.period,a.phases.length,a.async===true))return out({...j,ok:false,input_may_have_been_applied:true,do_not_repeat:true,error:'key_drive receipt lacks the exact job/owner/scope/timing contract; no input or cleanup was replayed'});
  const payload:Obj={...j,ok:receipt.ok,key_drive:{requested_ms:parsed.duration,period_ms:parsed.period,bindings:b,phases:parsed.phases,vehicle_motion_measured:false,application_response:null,scope:'requested digital key states only; duty is key-on fraction, not steering angle, speed or route progress'},
    ...(receipt.ok?{}:{do_not_repeat:true,input_may_have_been_applied:!refused}),...(a.async&&receipt.ok?{next_action:{tool:'job_status',arguments:{pid:a.pid,job_id:j.job_id},note:'Follow this exact finite job; no input is repeated'}}:{})};
  const reply=out(payload);
  if(ctx.epoch(a.pid)!==epoch)return out({...j,ok:false,do_not_repeat:true,input_may_have_been_applied:!refused,error:'attachment changed during key_drive; native receipt retained and no action was replayed'});
  try{return await ctx.finish(a.pid,reply,a,epoch);}catch(e){return out({...payload,capture_after_ok:false,capture_after_error:e instanceof Error?e.message:String(e),capture_after_note:'Native input receipt retained; no input or capture was replayed',do_not_repeat:true});}
 }catch(e){return out({ok:false,error:String(e),input_injected:attempted?null:false,input_may_have_been_applied:attempted,do_not_repeat:attempted,input_receipt:receipt?.json??null});}
}
export function registerKeyDrive(register:Register,ctx:CameraContext,captureArgs:z.ZodRawShape){
 register('key_drive','Drive using bounded keyboard duty cycles',
  'Run one finite native keyboard timeline: hold throttle independently while left/right steering uses signed PWM duty and optional brake. Negative steer pulses left; positive pulses right;0 is no steering. Default bindings W/A/D/S; throttle may be a1..4-key chord (for example w+shift), other roles one key each, all canonical resources distinct. Contiguous throttle phases remain held. Late windows are skipped rather than replayed; no replacement by default. Duty is digital key-on fraction, never measured analog steering, speed, heading or route progress. Game controls and pulse consumption must be checked in captures. All owned keys release at completion/cancellation; another manual input interrupts the job.',
  {...captureArgs,pid:z.number().int().positive().max(4294967295),phases:z.array(phase).min(1).max(128),bindings:bindings.optional(),period_ms:z.number().int().min(20).max(1000).optional(),async:z.boolean().optional(),replace:z.boolean().optional(),max_lateness_ms:z.number().int().min(1).max(5000).optional()},a=>keyDrive(ctx,a as KeyDriveOptions));
}
