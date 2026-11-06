import {z} from 'zod';
import {randomUUID} from 'node:crypto';
import {openSync,fstatSync,readSync,closeSync} from 'node:fs';
import {resolve} from 'node:path';
import {decodePng} from './png.js';
import {canonicalSequenceScope} from './sequenceconditions.js';
import {presentInputReads} from './inputreads.js';
import type {CameraContext} from './camerahelpers.js';
type Obj=Record<string,unknown>;
type Reply={content:({type:'text';text:string}|{type:'image';data:string;mimeType:string})[];isError?:boolean};
type Register=<S extends z.ZodRawShape>(name:string,title:string,description:string,shape:S,run:(a:z.objectOutputType<S,z.ZodTypeAny>)=>Promise<Reply>,readOnly?:boolean)=>void;
type Rect={x:number;y:number;w:number;h:number};
type Capture=Parameters<CameraContext['captureError']>[0];
export type RadarOptions=Capture&{radar_mode:'heading_up';radar_region:Rect;colour?:[number,number,number];template?:string;colour_tolerance?:number;template_score?:number;min_pixels?:number;max_pixels?:number;centre_x?:number;centre_y?:number;arrival_radius?:number;angle_tolerance?:number;forward_cone?:number;full_steer_deg?:number;steering_sign?:number;stable_samples?:number;progress_ms?:number;period_ms?:number;timeout_ms?:number;poll_ms?:number;throttle_while_turning?:boolean;degrees?:number;forward_key?:string;left_key?:string;right_key?:string;replace?:boolean;capture_method?:'auto'|'native'|'printwindow'};
const object=(v:unknown):Obj|undefined=>v!==null&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const reply=(json:Obj):Reply=>({content:[{type:'text',text:JSON.stringify(json)}],isError:json.ok!==true});
export function radarRequest(a:RadarOptions,turnOnly:boolean,id:string):Obj{
 if((a.colour!==undefined)===(a.template!==undefined))throw new Error('choose exactly one colour or PNG template for the marker');
 const r=a.radar_region;if(!r||r.w<8||r.h<8||r.w>512||r.h>512||r.x<0||r.y<0)throw new Error('radar_region needs client x,y and sides8..512');
 const request:Obj={cmd:turnOnly?'radar-turn':'radar-nav',request_id:id,radar_mode:a.radar_mode,radar_region:`${r.x},${r.y},${r.w},${r.h}`,timeout_ms:a.timeout_ms??6000};
 if(a.colour){if(a.colour.length!==3||a.colour.some(v=>!Number.isInteger(v)||v<0||v>255))throw new Error('colour needs three byte components');[request.colour_r,request.colour_g,request.colour_b]=a.colour;}
 else{
  if(r.w>256||r.h>256)throw new Error('template radar sides must be<=256');
  const path=resolve(a.template!),fd=openSync(path,'r');let bytes:Buffer;
  try{const info=fstatSync(fd);if(!info.isFile()||info.size<1||info.size>256*1024)throw new Error('radar template must be a PNG file of1..262144 bytes');bytes=Buffer.alloc(info.size);let at=0;while(at<bytes.length){const n=readSync(fd,bytes,at,bytes.length-at,at);if(!n)throw new Error('radar template changed while reading');at+=n;}if(fstatSync(fd).size!==info.size)throw new Error('radar template changed while reading');}finally{closeSync(fd);}
  const image=decodePng(bytes,{maxPixels:1024,maxDimension:32});let opaque=0;for(let n=3;n<image.data.length;n+=4)if(image.data[n]>=128)++opaque;if(opaque<2)throw new Error('radar template needs at least two opaque pixels');
  request.template_w=image.width;request.template_h=image.height;request.template_rgba_hex=image.data.toString('hex');
 }
 for(const key of ['colour_tolerance','template_score','min_pixels','max_pixels','centre_x','centre_y','arrival_radius','angle_tolerance','forward_cone','full_steer_deg','steering_sign','stable_samples','progress_ms','period_ms','poll_ms','throttle_while_turning','degrees','forward_key','left_key','right_key','replace','capture_method'] as const)if(a[key]!==undefined)request[key]=a[key];
 return request;
}
export async function runRadar(ctx:CameraContext,a:RadarOptions,turnOnly=false):Promise<Reply>{
 const invalid=ctx.captureError(a);if(invalid)return reply({ok:false,input_injected:false,error:invalid});
 const epoch=ctx.epoch(a.pid),id=randomUUID();let attempted=false,receipt:Obj|undefined;
 try{
  const request=radarRequest(a,turnOnly,id);
  const support=await ctx.run([...ctx.at(a.pid),'json',JSON.stringify({cmd:'ping',input_job_support:true})],3000);const feature=object(support.json);
  if(!support.ok||feature?.ok!==true||feature.pid!==a.pid||feature.radar_navigation_version!==1)return reply({ok:false,input_injected:false,error:'target lacks typed native radar navigation support',support_receipt:support.json});
  if(ctx.epoch(a.pid)!==epoch)return reply({ok:false,input_injected:false,error:'attachment changed before radar admission'});
  attempted=true;const native=presentInputReads(await ctx.run([...ctx.at(a.pid),'json',JSON.stringify(request)],(a.timeout_ms??6000)+10000));receipt=object(native.json);
  if(!native.ok||receipt?.ok!==true)return reply({...receipt,ok:false,native_receipt:native.json,input_may_have_been_applied:receipt?.input_injected!==false,do_not_repeat:true,...(native.stderr?{native_stderr:native.stderr}:{})});
  const radar=object(receipt.radar_navigation),job=typeof receipt.job_id==='string'?/^([1-9][0-9]{0,9}):([1-9][0-9]{0,19}):([1-9][0-9]{0,19})$/.exec(receipt.job_id):null;
  const scope=canonicalSequenceScope(receipt.capture_scope),finished=canonicalSequenceScope(receipt.finished_capture_scope);
  if(!job||BigInt(job[1])!==BigInt(a.pid)||BigInt(job[2])>18446744073709551615n||BigInt(job[3])>18446744073709551615n||receipt.owner!==ctx.owner||receipt.job_kind!=='radar'||receipt.requested_ms!==(a.timeout_ms??6000)||receipt.job_complete!==true||receipt.job_state!=='completed'||receipt.accepted!==false||receipt.capture_scope_checked!==true||!scope||scope!==finished||radar?.version!==1||radar.request_id!==id||typeof radar.goal_reached!=='boolean')return reply({ok:false,error:'radar completion acknowledgement is unconfirmed; input was not replayed',input_may_have_been_applied:true,do_not_repeat:true,native_receipt:native.json});
  const out=reply({...receipt,goal_reached:radar.goal_reached,stop_reason:radar.stop_reason,application_response:null,world_yaw_degrees:null});
  if(ctx.epoch(a.pid)!==epoch)return reply({...receipt,attachment_changed:true,capture_after_error:'attachment changed after the native radar job; completed input retained'});
  try{return await ctx.finish(a.pid,out,a,epoch);}catch(e){return reply({...receipt,goal_reached:radar.goal_reached,stop_reason:radar.stop_reason,capture_after_ok:false,capture_after_error:e instanceof Error?e.message:String(e),capture_after_note:'Completed native input retained; no input or capture was replayed'});}
 }catch(e){return reply({ok:false,error:e instanceof Error?e.message:String(e),input_injected:attempted?null:false,input_may_have_been_applied:attempted,do_not_repeat:attempted,...(receipt?{native_receipt:receipt}:{})});}
}
export function registerRadarNavigation(register:Register,ctx:CameraContext,captureArgs:z.ZodRawShape){
 const common={...captureArgs,pid:z.number().int().positive().max(4294967295),radar_mode:z.literal('heading_up').describe('Explicit caller declaration: the radar keeps the player heading at screen-up. North-up radar is unsupported.'),radar_region:z.object({x:z.number().int().min(0).max(32767),y:z.number().int().min(0).max(32767),w:z.number().int().min(8).max(512),h:z.number().int().min(8).max(512)}).strict(),colour:z.tuple([z.number().int().min(0).max(255),z.number().int().min(0).max(255),z.number().int().min(0).max(255)]).optional(),template:z.string().min(1).max(32768).optional(),colour_tolerance:z.number().int().min(0).max(255).optional(),template_score:z.number().min(.5).max(1).optional(),min_pixels:z.number().int().min(1).max(65536).optional(),max_pixels:z.number().int().min(1).max(65536).optional(),centre_x:z.number().finite().optional(),centre_y:z.number().finite().optional(),angle_tolerance:z.number().min(1).max(45).optional(),full_steer_deg:z.number().min(10).max(180).optional(),steering_sign:z.union([z.literal(1),z.literal(-1)]).optional(),stable_samples:z.number().int().min(1).max(10).optional(),progress_ms:z.number().int().min(250).max(10000).optional(),period_ms:z.number().int().min(20).max(1000).optional(),timeout_ms:z.number().int().min(250).max(15000).optional(),poll_ms:z.number().int().min(50).max(1000).optional(),left_key:z.string().min(1).max(32).optional(),right_key:z.string().min(1).max(32).optional(),replace:z.boolean().optional(),capture_method:z.enum(['auto','native','printwindow']).optional()};
 const movement={...common,arrival_radius:z.number().min(1).max(128).optional(),forward_cone:z.number().min(1).max(90).optional(),throttle_while_turning:z.boolean().optional(),forward_key:z.string().min(1).max(128).optional()};
 const description='One finite native job observes a unique colour component or <=32x32 PNG marker in your heading-up radar, pulses configured A/D steering proportionally and holds the forward chord when aligned (or explicitly while turning). It stops on repeated marker-near-centre witnesses, no marker progress, ambiguity/loss, scope/manual change, observation lease expiry or timeout. DefaultW/A/D; forward chord may include sprint. The radar and bindings/sign are caller-supplied assumptions. No world yaw, vehicle speed, physical arrival or mission response is measured. Captures stay off the input mutex; generated input is never replayed. At most15s and4096 virtual transitions.';
 register('steer_to_blip','Steer toward a radar marker',description,movement,a=>runRadar(ctx,a as RadarOptions));
 register('goto_marker','Approach a radar marker',description,movement,a=>runRadar(ctx,a as RadarOptions));
 register('turn_keys','Turn keys using a radar reference','Pulse configured turning keys while tracking a unique north/reference marker in a heading-up radar. degrees is a shortest signed turn within179degrees; stop on repeated observed reference-bearing alignment. No forward input. Keys must actually change radar bearing; the measured angle is radar geometry, never asserted world yaw.',{...common,degrees:z.number().finite().min(-179).max(179)},a=>runRadar(ctx,a as RadarOptions,true));
}
