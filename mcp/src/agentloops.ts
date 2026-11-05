import {z} from 'zod';
import {resolve} from 'node:path';
import type {CliResult} from './vinput.js';
import {canonicalSequenceScope} from './sequenceconditions.js';
import {motionSample,motionDifference} from './motion.js';
import {frameworkClickTiming} from './frameworkclick.js';
import type {MappingUsed} from './capturemapping.js';
import {conditionTextMatch,normalizedConditionText} from './conditiontext.js';
type Obj=Record<string,unknown>;
type Reply={content:({type:'text';text:string}|{type:'image';data:string;mimeType:string})[];isError?:boolean};
type Rect={x:number;y:number;w:number;h:number};type Point={x:number;y:number};
type Mapping={scale:number;offset_x:number;offset_y:number};
type Runtime={signal?:AbortSignal};
type Options={pid:number;coord_space?:'client'|'capture';mapping?:Mapping;capture_after?:boolean|number;capture_settle_ms?:number;region?:Rect;max_width?:number;scale?:number;upscale?:boolean;save_path?:string;out_dir?:string;format?:'png'|'jpeg';quality?:number};
export type LoopContext={owner:string;at:(pid:number)=>string[];run:(argv:string[],budget:number)=>Promise<CliResult>;epoch:(pid:number)=>number;
 map:(pid:number,options:Options,points:[number,number][],expectedScope:unknown)=>{pts:[number,number][];mapped:boolean;mapping_used?:MappingUsed}|{error:string}|Promise<{pts:[number,number][];mapped:boolean;mapping_used?:MappingUsed}|{error:string}>;
 captureError:(options:Options)=>string|undefined;finish:(pid:number,out:Reply,options:Options,epoch:number)=>Promise<Reply>;qualifyInput?:(r:CliResult)=>CliResult};
type Register=<S extends z.ZodRawShape>(name:string,title:string,description:string,shape:S,run:(a:z.objectOutputType<S,z.ZodTypeAny>,runtime?:Runtime)=>Promise<Reply>,readOnly?:boolean)=>void;
const obj=(v:unknown):Obj|undefined=>v&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const reply=(j:Obj):Reply=>({content:[{type:'text',text:JSON.stringify(j)}],isError:j.ok!==true});
const u64=(v:unknown):v is string=>typeof v==='string'&&/^[1-9][0-9]{0,19}$/.test(v)&&BigInt(v)<=18446744073709551615n;
const nap=(ms:number)=>new Promise<void>(r=>setTimeout(r,ms));
const sessions=new Set<number>();let nextSession=0;
const inputGenerations=new Map<number,number>();
export function noteOtherInput(pid:number){inputGenerations.set(pid,(inputGenerations.get(pid)??0)+1);}
class NativeFailure extends Error{constructor(message:string,readonly receipt:unknown,readonly stderr?:string){super(message);}}
const failureView=(e:unknown):Obj=>e instanceof NativeFailure?{failure_receipt:e.receipt,...(e.stderr?{failure_stderr:e.stderr}:{}),...(obj(e.receipt)?.anticheat_refused===true?{anticheat_refused:true}:{})}:{};
type Session={pid:number;epoch:number;generation:number;deadline:number;signal?:AbortSignal;scope?:string;nativeDeadline?:string;frequency?:string;ownerCount?:number;id:string;mapping_used?:MappingUsed};
function check(ctx:LoopContext,s:Session){if(s.signal?.aborted)throw new Error('request_cancelled');if(ctx.epoch(s.pid)!==s.epoch)throw new Error('attachment_changed');if((inputGenerations.get(s.pid)??0)!==s.generation)throw new Error('other_input_command_started');if(performance.now()>=s.deadline)throw new Error('session_deadline');}
function budget(ctx:LoopContext,s:Session,maximum=1000){check(ctx,s);return Math.max(1,Math.min(maximum,Math.floor(s.deadline-performance.now())));}
async function json(ctx:LoopContext,s:Session,request:Obj,maximum=1000,onSend?:()=>void){const available=budget(ctx,s,maximum);onSend?.();return ctx.run([...ctx.at(s.pid),'json',JSON.stringify(request)],available);}
type Sample={hash:string;scope:string;receipt:Obj};
async function sample(ctx:LoopContext,s:Session,region?:Rect,motion=false):Promise<Sample>{
 const r=await json(ctx,s,{cmd:'frame-hash',observation_only:true,timeout_ms:Math.min(500,budget(ctx,s)),...(motion?{motion_sample:true}:{}),...(region?{region:`${region.x},${region.y},${region.w},${region.h}`}:{})});check(ctx,s);
 const j=obj(r.json),scope=canonicalSequenceScope(j?.capture_scope);
 if(!r.ok||!j||!scope||j.capture_space_updated!==false||typeof j.hash!=='string'||!/^[0-9a-f]{16}$/i.test(j.hash)||j.timing_clock!=='QueryPerformanceCounter'||!u64(j.capture_qpc_ticks)||!u64(j.qpc_frequency))throw new NativeFailure('scoped_capture_unavailable',r.json);
 if(s.scope&&s.scope!==scope)throw new NativeFailure('capture_scope_changed',j);if(s.frequency&&s.frequency!==j.qpc_frequency)throw new NativeFailure('capture_clock_changed',j);
 if(!s.scope){s.scope=scope;s.frequency=j.qpc_frequency;const left=Math.max(0,Math.floor(s.deadline-performance.now()));s.nativeDeadline=(BigInt(j.capture_qpc_ticks)+BigInt(j.qpc_frequency)*BigInt(left)/1000n).toString();}
 return {hash:j.hash.toUpperCase(),scope,receipt:j};
}
async function status(ctx:LoopContext,s:Session,button:number,ownHold=false){
 const r=await ctx.run([...ctx.at(s.pid),'status'],budget(ctx,s));check(ctx,s);const j=obj(r.json);
 if(!r.ok||j?.pid!==s.pid||j.capture_scope_version!==1||j.pointer_scope_version!==1||j.input_scope_version!==1||j.input_admission_version!==1||!Array.isArray(j.virtual_down))throw new NativeFailure('bounded_scoped_input_support_unavailable',r.json);
 if(j.owner_active===true||obj(j.cursor_guard)?.owner_active===true)throw new Error('user_controls_target');
 if(!Number.isInteger(j.owner_input_count)||Number(j.owner_input_count)<0)throw new Error('user_activity_observation_unavailable');
 if(s.ownerCount!==undefined&&s.ownerCount!==j.owner_input_count)throw new Error('user_activity_changed');s.ownerCount=Number(j.owner_input_count);
 if(!ownHold&&j.virtual_down.includes(button))throw new Error('mouse_button_already_held');return j;
}
async function idleJobs(ctx:LoopContext,s:Session){const r=await ctx.run([...ctx.at(s.pid),'jobs'],budget(ctx,s));check(ctx,s);const j=obj(r.json);if(!r.ok||!Array.isArray(j?.jobs)||j.jobs.some(v=>obj(v)?.job_complete!==true))throw new NativeFailure('other_input_job_active_or_unknown',r.json);}
async function finish(ctx:LoopContext,a:Options,out:Reply,s:Session){try{return await ctx.finish(a.pid,out,a,s.epoch);}catch(e){const original=JSON.parse((out.content[0] as {text:string}).text);return reply({...original,input_ok:original.ok===true,capture_after_ok:false,capture_after_error:String(e),post_capture_error:String(e),capture_after_warning:'Input receipts are retained. The observation failed after input; do not repeat any point because of this capture error.'});}finally{sessions.delete(a.pid);}}
const nativeFields=(s:Session)=>({expected_capture_scope:s.scope,admit_before_qpc_ticks:s.nativeDeadline,admit_qpc_frequency:s.frequency});
const qualified=(ctx:LoopContext,r:CliResult)=>ctx.qualifyInput?.(r)??r;
async function points(ctx:LoopContext,s:Session,a:Options,input:Point[]):Promise<Point[]>{
 const mapped=await ctx.map(a.pid,a,input.map(p=>[p.x,p.y]),JSON.parse(s.scope!));check(ctx,s);if('error' in mapped)throw new Error(mapped.error);
 if(mapped.mapping_used)s.mapping_used=mapped.mapping_used;
 const scope=JSON.parse(s.scope!) as {client_w:number;client_h:number};
 if(mapped.pts.length!==input.length||mapped.pts.some(p=>p.length!==2||!Number.isFinite(p[0])||!Number.isFinite(p[1])||p[0]<0||p[1]<0||p[0]>=scope.client_w||p[1]>=scope.client_h))throw new Error('point_outside_current_client');
 const result=mapped.pts.map(([x,y])=>({x:Math.round(x),y:Math.round(y)}));if(result.some(p=>p.x>=scope.client_w||p.y>=scope.client_h))throw new Error('rounded_point_outside_current_client');return result;
}
const buttons={left:1,right:2,middle:4,x1:5,x2:6} as const;
type ProbeOptions={probe_region?:Rect};
export function pointProbeRegion(point:Point,scope:{client_w:number;client_h:number},explicit?:Rect):Rect{
 const {client_w:w,client_h:h}=scope;
 if(!Number.isInteger(w)||!Number.isInteger(h)||w<1||h<1||w>32768||h>32768||!Number.isFinite(point.x)||!Number.isFinite(point.y))throw new Error('point_probe_geometry_unavailable');
 if(explicit){if(![explicit.x,explicit.y,explicit.w,explicit.h].every(Number.isInteger)||explicit.x<0||explicit.y<0||explicit.w<1||explicit.h<1||explicit.x+explicit.w>w||explicit.y+explicit.h>h)throw new Error('probe_region_outside_current_client');return {...explicit};}
 const width=Math.min(400,w),height=Math.min(200,h);
 return {x:Math.max(0,Math.min(w-width,Math.round(point.x-width/2))),y:Math.max(0,Math.min(h-height,Math.round(point.y-height/2))),w:width,h:height};
}
async function readTooltip(ctx:LoopContext,s:Session,region:Rect):Promise<Obj>{
 const r=await ctx.run([...ctx.at(s.pid),'ocr','--lines-only','--observation-only=true','--region',`${region.x},${region.y},${region.w},${region.h}`],budget(ctx,s,2000));check(ctx,s);const text=obj(r.json);
 if(!r.ok||!text||text.ok!==true||typeof text.text!=='string'||text.text.length>16384||canonicalSequenceScope(text.capture_scope)!==s.scope||text.capture_space_updated!==false||text.timing_clock!=='QueryPerformanceCounter'||text.qpc_frequency!==s.frequency||!u64(text.capture_qpc_ticks))throw new NativeFailure('scoped_tooltip_ocr_unavailable',r.json,r.stderr);
 return text;
}
export type ClickManyOptions=Options&ProbeOptions&{points:Point[];button?:keyof typeof buttons;timeout_ms?:number;hold_ms?:number;hover_ms?:number;hover_timeout_ms?:number;interval_ms?:number;verify_change?:boolean;verify_region?:Rect;verify_settle_ms?:number;min_change_pct?:number;pixel_tolerance?:number;verify_policy?:'stop'|'report_only';unchanged_limit?:number;verify?:'tooltip';reject_texts?:string[];tooltip_hover_ms?:number};
export type ClickGridOptions=Omit<ClickManyOptions,'points'>&{x0:number;y0:number;dx:number;dy:number;cols:number;rows:number};
export function gridPoints(a:Pick<ClickGridOptions,'x0'|'y0'|'dx'|'dy'|'cols'|'rows'>):Point[]{
 if(!Number.isInteger(a.cols)||!Number.isInteger(a.rows)||a.cols<1||a.rows<1||a.cols*a.rows>256||![a.x0,a.y0,a.dx,a.dy].every(Number.isFinite))throw new Error('grid requires1..256 finite row-major points');
 return Array.from({length:a.cols*a.rows},(_,i)=>({x:a.x0+i%a.cols*a.dx,y:a.y0+Math.floor(i/a.cols)*a.dy}));
}
function clickAck(r:CliResult,p:Point,s:Session){const j=obj(r.json);return r.ok&&j?.ok===true&&j.pointer_scope_version===1&&j.capture_scope_checked===true&&j.input_admission_version===1&&j.admission_deadline_qpc_ticks===s.nativeDeadline&&j.button_down_count===1&&j.button_released===true&&j.last_press_x===p.x&&j.last_press_y===p.y;}
export async function clickMany(ctx:LoopContext,a:ClickManyOptions,runtime:Runtime={}):Promise<Reply>{
 const invalid=ctx.captureError(a);if(invalid)return reply({ok:false,error:invalid,input_injected:false});
 if(a.verify_change&&!a.verify_region)return reply({ok:false,error:'verify_change requires verify_region in client pixels',input_injected:false});
 if(!a.verify_change&&[a.verify_region,a.verify_settle_ms,a.min_change_pct,a.pixel_tolerance].some(v=>v!==undefined))return reply({ok:false,error:'verification options require verify_change:true',input_injected:false});
 if(a.verify_policy!==undefined&&!['stop','report_only'].includes(a.verify_policy)||a.unchanged_limit!==undefined&&(!Number.isInteger(a.unchanged_limit)||a.unchanged_limit<1||a.unchanged_limit>256))return reply({ok:false,error:'verify_policy must be stop/report_only and unchanged_limit must be1..256',input_injected:false});
 if(!a.verify_change&&(a.verify_policy!==undefined||a.unchanged_limit!==undefined))return reply({ok:false,error:'verify_policy/unchanged_limit require verify_change:true',input_injected:false});
 if(a.verify_policy==='report_only'&&a.unchanged_limit!==undefined)return reply({ok:false,error:'unchanged_limit applies only to verify_policy:stop',input_injected:false});
 if(a.verify!==undefined&&a.verify!=='tooltip')return reply({ok:false,error:'verify supports tooltip only',input_injected:false});
 if(!a.verify&&[a.reject_texts,a.tooltip_hover_ms,a.probe_region].some(v=>v!==undefined))return reply({ok:false,error:'tooltip options require verify:tooltip',input_injected:false});
 if(a.verify==='tooltip'&&(!Array.isArray(a.reject_texts)||a.reject_texts.length<1||a.reject_texts.length>16||a.reject_texts.some(t=>typeof t!=='string'||t.length>256||!normalizedConditionText(t))||a.tooltip_hover_ms!==undefined&&(!Number.isInteger(a.tooltip_hover_ms)||a.tooltip_hover_ms<0||a.tooltip_hover_ms>2000)))return reply({ok:false,error:'tooltip verification needs1..16 nonempty reject_texts and optional tooltip_hover_ms0..2000',input_injected:false});
 if(!a.points.length||a.points.length>256||a.points.some(p=>!Number.isFinite(p.x)||!Number.isFinite(p.y)))return reply({ok:false,error:'points require1..256 finite coordinates',input_injected:false});
 if(sessions.has(a.pid))return reply({ok:false,error:'another_bounded_helper_session_active',input_injected:false});sessions.add(a.pid);
 const s:Session={pid:a.pid,epoch:ctx.epoch(a.pid),generation:inputGenerations.get(a.pid)??0,deadline:performance.now()+(a.timeout_ms??30000),signal:runtime.signal,id:`loops-${a.pid}-${++nextSession}`},receipts:Obj[]=[];
 let error:string|undefined,baseline:Obj|undefined,failure:Obj={},nextIndex=0,uncertainIndex:number|undefined,unchangedStreak=0,active:Obj|undefined,inputAttempted=false,stopReason:string|undefined;
 const policy=a.verify_policy??'stop',limit=a.unchanged_limit??1;
 try{
  await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);baseline=(await sample(ctx,s)).receipt;const client=await points(ctx,s,a,a.points);
  for(const [index,p] of client.entries()){
   await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);
   const timing=frameworkClickTiming(a.hold_ms,a.hover_ms,true),hold=timing.hold,hover=timing.hover,hoverTimeout=a.hover_timeout_ms??1000,required=timing.budgetHold+timing.budgetHover+hoverTimeout+2500;
   if(s.deadline-performance.now()<required)throw new Error('insufficient_budget_for_next_bounded_click');
   active=undefined;
   if(a.verify==='tooltip'){
    const probe=pointProbeRegion(p,JSON.parse(s.scope!),a.probe_region),dwell=a.tooltip_hover_ms??400;
    if(s.deadline-performance.now()<required+dwell+5000)throw new Error('insufficient_budget_for_tooltip_and_bounded_click');
    active={index,requested:a.points[index],client:p,click_attempted:false,confirmed:false,delivery_confirmed:null,placement_confirmed:null,changed:null,probe_region:probe,reason:'tooltip_pending',hover_confirmed:false};receipts.push(active);
    const moved=await json(ctx,s,{cmd:'mouse',x:p.x,y:p.y,steps:1,frame_paced:false,...nativeFields(s)},1000,()=>{inputAttempted=true;});active.move_receipt=moved.json;const move=obj(moved.json);
    if(!moved.ok||move?.capture_scope_checked!==true||move.pointer_scope_version!==1||move.input_admission_version!==1||move.admission_deadline_qpc_ticks!==s.nativeDeadline||move.x!==p.x||move.y!==p.y)throw new Error('tooltip_hover_unconfirmed_no_click');active.hover_confirmed=true;
    if(dwell)await nap(Math.min(dwell,budget(ctx,s,dwell)));active.tooltip_ocr=await readTooltip(ctx,s,probe);
    await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);
    const text=String(obj(active.tooltip_ocr)!.text),reject=a.reject_texts!.find(t=>conditionTextMatch(text,t));
    if(!normalizedConditionText(text)||reject!==undefined){active.skipped=true;active.reason=reject!==undefined?'reject_text_observed':'tooltip_empty';if(reject!==undefined)active.matched_reject_text=reject;nextIndex=index+1;unchangedStreak=0;active=undefined;
     if(index<client.length-1&&(a.interval_ms??0)>0)await nap(Math.min(a.interval_ms!,budget(ctx,s)));continue;}
   }
   const before=a.verify_change?await sample(ctx,s,a.verify_region,true):undefined;
   if(before&&!motionSample(before.receipt))throw new Error('region_motion_sample_unavailable_before_click');
   if(s.deadline-performance.now()<required)throw new Error('insufficient_budget_for_next_bounded_click');
   const point:Obj=active??{index,requested:a.points[index],client:p,click_attempted:false,confirmed:false,delivery_confirmed:null,placement_confirmed:null,changed:null};if(!active)receipts.push(point);active=point;
   const native=await json(ctx,s,{cmd:'click',button:a.button??'left',x:p.x,y:p.y,count:1,ms:hold,settle_ms:hover,hover_timeout_ms:hoverTimeout,...timing.native,...nativeFields(s)},required,()=>{inputAttempted=true;point.click_attempted=true;nextIndex=index+1;uncertainIndex=index;});
   const result=qualified(ctx,native),confirmed=clickAck(result,p,s);Object.assign(point,{confirmed,delivery_confirmed:confirmed,confirmation_scope:'native_delivery_only',receipt:native.json,...(result.json!==native.json?{qualified_receipt:result.json}:{})});if(confirmed)uncertainIndex=undefined;point.reason='native_click_delivered';
   if(!confirmed)throw new Error('click_acknowledgement_unconfirmed_no_retry');check(ctx,s);
   if(before){if(a.verify_settle_ms)await nap(Math.min(a.verify_settle_ms,budget(ctx,s)));const after=await sample(ctx,s,a.verify_region,true),diff=motionDifference(motionSample(before.receipt),motionSample(after.receipt),a.pixel_tolerance??8);if(!diff)throw new Error('region_motion_sample_unavailable_after_delivered_click');const changed=diff.changed_pct>(a.min_change_pct??0);point.changed=changed;point.reason=changed?'region_pixels_changed':'region_pixels_unchanged';point.region_diff={...diff,changed,region:a.verify_region,before:before.receipt,after:after.receipt,metric:'coarse_grid_area_average_RGB',application_response:null};unchangedStreak=changed?0:unchangedStreak+1;if(!changed&&policy==='stop'&&unchangedStreak>=limit){error='point_region_unchanged_after_delivered_click';stopReason='unchanged_limit';active=undefined;break;}}
   active=undefined;
   if(index<client.length-1&&(a.interval_ms??0)>0)await nap(Math.min(a.interval_ms!,budget(ctx,s)));
  }
 }catch(e){error=e instanceof Error?e.message:String(e);failure=failureView(e);if(active)active.reason=error;}
 const skipped=receipts.filter(r=>r.skipped===true).length,reason=stopReason??error??(skipped?'all_points_processed':'all_points_acknowledged');
 const out=reply({ok:!error,pid:a.pid,capture_scope:s.scope?JSON.parse(s.scope):null,qpc_frequency:s.frequency??null,...(s.mapping_used?{mapping_used:s.mapping_used}:{}),input_session:{id:s.id,kind:'bounded_mcp_loop',native_batch:false,timeout_ms:a.timeout_ms??30000,admission_deadline_qpc_ticks:s.nativeDeadline??null,stop_reason:reason},stop_reason:reason,next_index:nextIndex,processed_points:nextIndex,skipped_points:skipped,...(uncertainIndex!==undefined?{uncertain_index:uncertainIndex}:{}),...(a.verify_change?{verify_policy:policy,unchanged_limit:policy==='stop'?limit:null,unchanged_streak:unchangedStreak}:{}),requested_points:a.points.length,completed_points:receipts.filter(r=>r.confirmed).length,completed_injected_points:receipts.filter(r=>r.confirmed).length,game_accepted_points:null,do_not_repeat_delivered_points:true,points:receipts,baseline_observation:baseline??null,input_injected:inputAttempted?null:false,...failure,...(error?{error}:{}),game_response_measured:false,scope:'completed_points/completed_injected_points count native delivered clicks only; skipped tooltip decisions and region pixels do not prove placement or causality; no failed or uncertain point is replayed',hint:'Use wait_text for any-of banners or observe_settle for scoped stable frames; capture_after is one image after the entire batch.'});
 return finish(ctx,a,out,s);
}
export async function clickGrid(ctx:LoopContext,a:ClickGridOptions,runtime:Runtime={}):Promise<Reply>{try{return await clickMany(ctx,{...a,points:gridPoints(a)},runtime);}catch(e){return reply({ok:false,error:String(e),input_injected:false});}}

export type HoverManyOptions=Options&{points:Point[];probe_region?:Rect;hover_ms?:number;ocr?:boolean;timeout_ms?:number};
export async function hoverMany(ctx:LoopContext,a:HoverManyOptions,runtime:Runtime={}):Promise<Reply>{
 const invalid=ctx.captureError(a);if(invalid)return reply({ok:false,error:invalid,input_injected:false});
 if(sessions.has(a.pid))return reply({ok:false,error:'another_bounded_helper_session_active',input_injected:false});sessions.add(a.pid);
 const s:Session={pid:a.pid,epoch:ctx.epoch(a.pid),generation:inputGenerations.get(a.pid)??0,deadline:performance.now()+(a.timeout_ms??30000),signal:runtime.signal,id:`hover-${a.pid}-${++nextSession}`},receipts:Obj[]=[];let error:string|undefined,failure:Obj={};
 try{
  await status(ctx,s,1);await idleJobs(ctx,s);await sample(ctx,s,a.probe_region);const client=await points(ctx,s,a,a.points);
  for(const [index,p] of client.entries()){
   await status(ctx,s,1);await idleJobs(ctx,s);if(s.deadline-performance.now()<(a.hover_ms??500)+1500)throw new Error('insufficient_budget_for_next_hover');
   const moved=await json(ctx,s,{cmd:'mouse',x:p.x,y:p.y,steps:1,frame_paced:false,...nativeFields(s)}),j=obj(moved.json);const point:Obj={index,requested:a.points[index],client:p,move_receipt:moved.json};receipts.push(point);
   if(!moved.ok||j?.capture_scope_checked!==true||j.pointer_scope_version!==1||j.input_admission_version!==1||j.admission_deadline_qpc_ticks!==s.nativeDeadline||j.x!==p.x||j.y!==p.y)throw new Error('hover_move_unconfirmed_no_retry');
   const probe=pointProbeRegion(p,JSON.parse(s.scope!),a.probe_region);point.probe_region=probe;point.hover_confirmed=true;
   if(a.hover_ms!==0)await nap(Math.min(a.hover_ms??500,budget(ctx,s)));await status(ctx,s,1);point.observation=(await sample(ctx,s,probe)).receipt;
   if(a.ocr)point.tooltip_ocr=await readTooltip(ctx,s,probe);
  }
 }catch(e){error=e instanceof Error?e.message:String(e);failure=failureView(e);}
 return finish(ctx,a,reply({ok:!error,pid:a.pid,capture_scope:s.scope?JSON.parse(s.scope):null,qpc_frequency:s.frequency??null,...(s.mapping_used?{mapping_used:s.mapping_used}:{}),input_session:{id:s.id,kind:'bounded_hover_probe',native_batch:false},requested_points:a.points.length,observed_points:receipts.filter(p=>p.observation&&(!a.ocr||p.tooltip_ocr)).length,points:receipts,game_response_measured:false,...failure,...(error?{error}:{}),scope:'one scoped ordinary mouse move per point, bounded tooltip dwell, hash and optional OCR with separate capture/QPC provenance; no clicks or placement claim; uncertain moves never replayed'}),s);
}

export type DeltaCursorOptions=Options&{x:number;y:number;cursor_template:string;counts_per_pixel_x:number;counts_per_pixel_y:number;hotspot_dx?:number;hotspot_dy?:number;threshold?:number;tolerance_px?:number;max_steps?:number;max_counts_per_step?:number;settle_ms?:number;timeout_ms?:number;button?:keyof typeof buttons;hold_ms?:number};
export async function clickDeltaCursor(ctx:LoopContext,a:DeltaCursorOptions,runtime:Runtime={}):Promise<Reply>{
 const invalid=ctx.captureError(a);if(invalid)return reply({ok:false,error:invalid,input_injected:false});
 if(![a.counts_per_pixel_x,a.counts_per_pixel_y].every(v=>Number.isFinite(v)&&v!==0&&Math.abs(v)<=100))return reply({ok:false,error:'finite nonzero signed counts_per_pixel are required',input_injected:false});
 if(sessions.has(a.pid))return reply({ok:false,error:'another_bounded_helper_session_active',input_injected:false});sessions.add(a.pid);
 const s:Session={pid:a.pid,epoch:ctx.epoch(a.pid),generation:inputGenerations.get(a.pid)??0,deadline:performance.now()+(a.timeout_ms??15000),signal:runtime.signal,id:`delta-cursor-${a.pid}-${++nextSession}`};
 const observations:Obj[]=[],motions:unknown[]=[];let click:CliResult|undefined,error:string|undefined,failure:Obj={},attempted=false,aligned=false;
 try{
  const capabilities=await status(ctx,s,buttons[a.button??'left']);if(capabilities.relative_scope_version!==1)throw new Error('scoped_relative_motion_unavailable');
  await idleJobs(ctx,s);await sample(ctx,s);const [desired]=await points(ctx,s,a,[{x:a.x,y:a.y}]);
  let previous=Infinity,stable=0;
  for(let step=0;step<=(a.max_steps??12);++step){
   await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);await sample(ctx,s);
   const found=await ctx.run([...ctx.at(a.pid),'find-image',resolve(a.cursor_template),'--threshold',String(a.threshold??.95),'--preserve-map'],budget(ctx,s,1500));check(ctx,s);
   const j=obj(found.json),match=Array.isArray(j?.matches)?obj(j.matches[0]):undefined,center=match?.center;
   if(!found.ok||j?.capture_space_preserved!==true||canonicalSequenceScope(j.capture_scope)!==s.scope||j.match_count!==1||!Array.isArray(j.matches)||j.matches.length!==1||!Array.isArray(center)||center.length!==2||!center.every(v=>typeof v==='number'&&Number.isFinite(v))||typeof match?.score!=='number'||match.score<(a.threshold??.95)||match.score>1)throw new NativeFailure('cursor_sprite_missing_ambiguous_or_scope_changed',found.json);
   const cursor={x:Number(center[0])+(a.hotspot_dx??0),y:Number(center[1])+(a.hotspot_dy??0)},scope=JSON.parse(s.scope!);
   if(cursor.x<0||cursor.y<0||cursor.x>=scope.client_w||cursor.y>=scope.client_h)throw new Error('cursor_hotspot_outside_client');
   const distance=Math.hypot(desired.x-cursor.x,desired.y-cursor.y);observations.push({step,cursor,target:desired,error_px:distance,receipt:j});
   if(distance<=(a.tolerance_px??3)){
    if(++stable>=2){aligned=true;break;}
   }else{
    stable=0;if(step>0&&distance>=previous-.25)throw new Error('cursor_did_not_move_toward_target');
    if(step===(a.max_steps??12))break;
    let dx=(desired.x-cursor.x)*a.counts_per_pixel_x,dy=(desired.y-cursor.y)*a.counts_per_pixel_y;
    const scale=Math.max(1,Math.abs(dx)/(a.max_counts_per_step??200),Math.abs(dy)/(a.max_counts_per_step??200));dx=Math.round(dx/scale);dy=Math.round(dy/scale);
    if(!dx&&!dy)throw new Error('cursor_target_below_count_resolution');
    attempted=true;const move=await json(ctx,s,{cmd:'move',dx,dy,raw:true,steps:1,step_ms:0,...nativeFields(s)},1500);motions.push(move.json);check(ctx,s);
    const m=obj(move.json);if(!move.ok||m?.capture_scope_checked!==true||m.unbounded!==true||m.raw_dx!==dx||m.raw_dy!==dy)throw new NativeFailure('cursor_motion_unconfirmed_no_retry',move.json);
   }
   previous=distance;await nap(Math.min(a.settle_ms??75,budget(ctx,s)));
  }
  if(!aligned)throw new Error('cursor_alignment_step_limit');
  await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);await sample(ctx,s);
  const required=(a.hold_ms??140)+2500;if(s.deadline-performance.now()<required)throw new Error('insufficient_budget_for_click');
  attempted=true;click=await json(ctx,s,{cmd:'click',button:a.button??'left',count:1,ms:a.hold_ms??140,settle_ms:0,...nativeFields(s)},required);const j=obj(click.json);
  if(!click.ok||j?.capture_scope_checked!==true||j.input_admission_version!==1||j.admission_deadline_qpc_ticks!==s.nativeDeadline||j.button_down_count!==1||j.button_released!==true)throw new NativeFailure('cursor_click_unconfirmed_no_retry',click.json);
 }catch(e){error=e instanceof Error?e.message:String(e);failure=failureView(e);}
 return finish(ctx,a,reply({ok:!error,pid:a.pid,cursor_aligned:aligned,observations,motions,click_receipt:click?.json??null,input_injected:attempted?null:false,...failure,...(error?{error}:{}),game_response_measured:false,scope:'rendered cursor-template hotspot verified twice within tolerance, bounded raw relative corrections, then one click without absolute coordinates; no failed input is replayed'}),s);
}
type HoldJob=Obj&{job_id:string;job_state:string;job_complete:boolean;hold_ready:boolean};
function holdJob(value:unknown,ctx:LoopContext,s:Session,duration:number,accepted:boolean,id?:string):HoldJob|undefined{
 const j=obj(value),match=typeof j?.job_id==='string'?/^([1-9][0-9]{0,9}):([1-9][0-9]{0,19}):([1-9][0-9]{0,19})$/.exec(j.job_id):null;
 if(!j||!match||BigInt(match[1])!==BigInt(s.pid)||BigInt(match[2])>18446744073709551615n||BigInt(match[3])>18446744073709551615n||j.job_kind!=='keys'||j.owner!==ctx.owner||j.accepted!==accepted||id!==undefined&&j.job_id!==id||j.capture_scope_checked!==true||j.input_admission_version!==1||j.admission_deadline_qpc_ticks!==s.nativeDeadline||j.requested_ms!==duration||typeof j.hold_ready!=='boolean'||typeof j.job_complete!=='boolean'||!['queued','running','completed','cancelled','failed'].includes(String(j.job_state))||j.job_complete!==['completed','cancelled','failed'].includes(String(j.job_state))||j.job_complete&&j.hold_ready||j.ok!==(accepted||j.job_state==='completed'))return;
 return j as HoldJob;
}
export type ClickToMoveOptions=Options&{x:number;y:number;region:Rect;button?:'left'|'right';timeout_ms?:number;hold_ms?:number;poll_ms?:number;stable_samples?:number};
export async function clickToMove(ctx:LoopContext,a:ClickToMoveOptions,runtime:Runtime={}):Promise<Reply>{
 const invalid=ctx.captureError({...a,region:a.capture_after===undefined||a.capture_after===false?undefined:a.region});if(invalid)return reply({ok:false,error:invalid,input_injected:false});
 if(sessions.has(a.pid))return reply({ok:false,error:'another_bounded_helper_session_active',input_injected:false});sessions.add(a.pid);
 const s:Session={pid:a.pid,epoch:ctx.epoch(a.pid),generation:inputGenerations.get(a.pid)??0,deadline:performance.now()+(a.timeout_ms??10000),signal:runtime.signal,id:`loops-${a.pid}-${++nextSession}`};
 const duration=a.hold_ms??5000;let baseline:Sample|undefined,last:Sample|undefined,move:CliResult|undefined,initial:CliResult|undefined,admitted:HoldJob|undefined,job:HoldJob|undefined,error:string|undefined,changed=false,stable=false,consecutive=0,samples=0,release=false,cleanup:Obj={attempted:false},inputAttempted=false,p:Point|undefined,completedReadReceipt:unknown,failure:Obj={};
 try{
  if(duration>(a.timeout_ms??10000))throw new Error('hold_ms_exceeds_session_timeout');await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);baseline=await sample(ctx,s,a.region);last=baseline;p=(await points(ctx,s,a,[{x:a.x,y:a.y}]))[0];
  if(s.deadline-performance.now()<duration+1000)throw new Error('insufficient_budget_for_one_bounded_hold');
  inputAttempted=true;move=await json(ctx,s,{cmd:'mouse',x:p.x,y:p.y,steps:1,frame_paced:false,...nativeFields(s)},1000);const moved=obj(move.json);
  if(!move.ok||moved?.capture_scope_checked!==true||moved.pointer_scope_version!==1||moved.input_admission_version!==1||moved.admission_deadline_qpc_ticks!==s.nativeDeadline||moved.x!==p.x||moved.y!==p.y)throw new Error('position_acknowledgement_unconfirmed_no_hold_sent');
  await status(ctx,s,buttons[a.button??'left']);initial=await json(ctx,s,{cmd:'key',key:a.button==='right'?'rbutton':'lbutton',ms:duration,async:true,...nativeFields(s)},1500);
  admitted=initial.ok?holdJob(initial.json,ctx,s,duration,true):undefined;if(!admitted)throw new Error('hold_acknowledgement_unconfirmed_no_retry');job=admitted;
  for(;;){
   const st=await status(ctx,s,buttons[a.button??'left'],true);if(st.vcur_x!==p.x||st.vcur_y!==p.y)throw new Error('virtual_cursor_changed');
   if(!job.job_complete){const r=await ctx.run([...ctx.at(a.pid),'job-status',admitted.job_id],budget(ctx,s)),next=holdJob(obj(r.json)?.job,ctx,s,duration,false,admitted.job_id);check(ctx,s);if(!r.ok||!next)throw new Error('exact_hold_status_unavailable');job=next;
    if(job.job_complete){release=true;const observed=qualified(ctx,{...r,json:job,ok:job.job_state==='completed'});completedReadReceipt=observed.json;if(job.job_state!=='completed'||!observed.ok)throw new Error(`native_hold_${job.job_state}_input_unconfirmed`);}}
   if(job.hold_ready||job.job_complete){const seen=await sample(ctx,s,a.region);samples++;if(seen.hash!==baseline.hash)changed=true;if(changed){consecutive=seen.hash===last!.hash?consecutive+1:0;if(consecutive>=(a.stable_samples??3)){stable=true;last=seen;break;}}last=seen;}
   await nap(Math.min(a.poll_ms??100,budget(ctx,s)));
  }
 }catch(e){error=e instanceof Error?e.message:String(e);failure=failureView(e);}
 finally{
  if(admitted&&!job?.job_complete&&ctx.epoch(a.pid)===s.epoch){const cancelled=await ctx.run([...ctx.at(a.pid),'job-cancel',admitted.job_id],1500).catch(()=>undefined),terminal=holdJob(obj(cancelled?.json)?.job,ctx,s,duration,false,admitted.job_id);release=cancelled?.ok===true&&terminal?.job_complete===true;cleanup={attempted:true,job_id:admitted.job_id,confirmed:release,receipt:cancelled?.json??null};if(release&&terminal){job=terminal;if(terminal.job_state==='failed'||terminal.job_state==='cancelled'&&terminal.job_reason!=='explicit_cancel')error??='native_hold_ended_before_verified_cleanup';}}
  else if(admitted&&!job?.job_complete)cleanup={attempted:false,reason:'attachment_changed; native bounded duration remains active'};
 }
 const ok=changed&&stable&&release&&!error,out=reply({ok,pid:a.pid,...(s.mapping_used?{mapping_used:s.mapping_used}:{}),input_session:{id:s.id,kind:'one_bounded_native_mouse_hold',timeout_ms:a.timeout_ms??10000,admission_deadline_qpc_ticks:s.nativeDeadline??null},move_receipt:move?.json??null,hold_input:initial?.json??null,job_id:admitted?.job_id??null,final_job:job??null,completed_input_read_receipt:completedReadReceipt??null,release_confirmed:release,hold_may_still_be_active:!!initial&&!release&&obj(initial.json)?.input_injected!==false,cleanup,changed,stable,samples,displacement:'UNKNOWN',displacement_px:null,baseline_observation:baseline?.receipt??null,final_observation:last?.receipt??null,input_injected:inputAttempted?null:false,...failure,...(!ok?{error:error??(!changed?'selected_region_did_not_change':!stable?'selected_region_did_not_stabilize':'exact_hold_release_unconfirmed')}:{}),scope:'selected-region change followed by identical hashes during one bounded hold; displacement and causal game movement are not measured; no repeated or indefinite button-down',hint:'Choose a region tracking the character/world rather than an animated HUD. Use observe_settle for observation-only stability.'});
 return finish(ctx,a,out,s);
}
export function registerAgentLoops(register:Register,ctx:LoopContext,capture:z.ZodRawShape,region:z.ZodType<Rect,z.ZodTypeDef,unknown>,coord:z.ZodRawShape){
 const point=z.union([z.object({x:z.number().finite(),y:z.number().finite()}).strict(),z.tuple([z.number().finite(),z.number().finite()]).transform(([x,y])=>({x,y}))]);
 const common={...capture,...coord,pid:z.number().int().positive(),button:z.enum(['left','right','middle','x1','x2']).optional(),timeout_ms:z.number().int().min(500).max(60000).optional(),hold_ms:z.number().int().min(1).max(1000).optional(),hover_ms:z.number().int().min(0).max(1000).optional(),hover_timeout_ms:z.number().int().min(1).max(1500).optional(),interval_ms:z.number().int().min(0).max(1000).optional(),verify_change:z.boolean().optional(),verify_region:region.optional(),verify_settle_ms:z.number().int().min(0).max(2000).optional(),min_change_pct:z.number().min(0).max(100).optional(),pixel_tolerance:z.number().int().min(0).max(255).optional()};
 const policy={verify_policy:z.enum(['stop','report_only']).optional().describe('Requires verify_change. Default stop stops after unchanged_limit consecutive measured unchanged clicks; report_only records pixels and continues without retry. Neither mode measures placement.'),unchanged_limit:z.number().int().min(1).max(256).optional().describe('Stop policy only: consecutive measured unchanged clicks, default1. A changed click or skipped point resets the streak.'),verify:z.literal('tooltip').optional().describe('Opt in to one scoped hover/OCR probe before each click. Caller reject_texts skip matching points; empty OCR also skips. Unknown/failed observations stop without clicking. No placement verdict or click retry.'),reject_texts:z.array(z.string().min(1).max(256)).min(1).max(16).optional().describe('Required with verify:tooltip. Literal normalized reject phrases; no regex/fuzzy matching.'),tooltip_hover_ms:z.number().int().min(0).max(2000).optional().describe('Tooltip dwell before its one OCR read, default400ms.'),probe_region:region.optional().describe('Explicit client-pixel tooltip region for all points; default a clamped400x200 box around each converted point.')};
 const description='Place multiple clicks in one bounded MCP session. Each point goes through ordinary native click admission with the same observed capture scope and a shared native deadline; every original receipt is returned. Stops on uncertainty, failed receipt, user activity, scope/attachment change, cancellation or deadline; no point is replayed. Native CLI requests remain per point (native_batch:false). capture_after observes only after the whole batch; use observe_settle or wait_text for visual verification.';
 register('click_many','Click several points',description+' points accepts {x,y} or [x,y]. completed_points counts injected clicks; placement remains unknown. verify_change requires verify_region; default stop/1 is preserved, or choose report_only/unchanged_limit. next_index is zero-based next unprocessed point; uncertain_index must not be replayed. verify:tooltip adds bounded reject-text/empty skips before any click. compact:true keeps counts, point pixels/reasons and stop metadata.',{...common,...policy,points:z.array(point).min(1).max(256)},async(a,r)=>clickMany(ctx,a as ClickManyOptions,r));
 register('click_grid','Click a grid',description+' Grid order is row-major; at most256 points. Same verification/tooltip policies and zero-based next_index as click_many.',{...common,...policy,x0:z.number().finite(),y0:z.number().finite(),dx:z.number().finite(),dy:z.number().finite(),cols:z.number().int().min(1).max(256),rows:z.number().int().min(1).max(256)},async(a,r)=>clickGrid(ctx,a as ClickGridOptions,r));
 register('hover_many','Probe several tooltips','Hover points ({x,y} or [x,y]) once each and return per-point scoped region hash plus optional tooltip OCR. Default probe is a clamped400x200 box around each converted point; explicit probe_region overrides it. compact:true exposes x/y/text, actual probe region and separate capture/OCR QPC. Stops on user activity, other input, scope/attachment change, cancellation or uncertainty. No clicking or placement inference.',{...capture,...coord,pid:z.number().int().positive(),points:z.array(point).min(1).max(64),probe_region:region.optional(),ocr:z.boolean().optional(),hover_ms:z.number().int().min(0).max(2000).optional(),timeout_ms:z.number().int().min(1500).max(60000).optional()},async(a,r)=>hoverMany(ctx,a as HoverManyOptions,r));
 register('click_delta_cursor','Click with a delta-driven cursor','Locate one rendered cursor sprite and move it toward client/capture x,y with bounded raw deltas. Requires signed counts per pixel on each axis and a PNG cursor template. Verifies its hotspot twice, then clicks once without absolute cursor placement. Missing/ambiguous sprites, no progress, user activity or scope changes stop without clicking. Animation outside the cursor does not need to settle.',
  {...common,x:z.number().finite(),y:z.number().finite(),cursor_template:z.string().min(1),counts_per_pixel_x:z.number().finite().min(-100).max(100),counts_per_pixel_y:z.number().finite().min(-100).max(100),hotspot_dx:z.number().finite().optional(),hotspot_dy:z.number().finite().optional(),threshold:z.number().min(.01).max(1).optional(),tolerance_px:z.number().min(.5).max(20).optional(),max_steps:z.number().int().min(1).max(30).optional(),max_counts_per_step:z.number().int().min(1).max(500).optional(),settle_ms:z.number().int().min(0).max(1000).optional()},async(a,r)=>clickDeltaCursor(ctx,a as DeltaCursorOptions,r));
 register('click_to_move','Hold and verify movement pixels','Move to one point, run ONE bounded native mouse-button hold, then watch a scoped region for change followed by stability. Cancels only its exact job. Native duration releases after disconnect; late queued admission is refused. Returns displacement:UNKNOWN because hash change/stability does not measure character movement or distance. Stops on manual activity, cursor/scope/attachment changes or uncertain receipts; never retries, sends indefinite down, or globally releases. Use observe_settle for read-only stability.',
  {...capture,...coord,pid:z.number().int().positive(),x:z.number().finite(),y:z.number().finite(),region,button:z.enum(['left','right']).optional(),timeout_ms:z.number().int().min(1500).max(60000).optional(),hold_ms:z.number().int().min(100).max(20000).optional(),poll_ms:z.number().int().min(50).max(1000).optional(),stable_samples:z.number().int().min(2).max(10).optional()},async(a,r)=>clickToMove(ctx,a as ClickToMoveOptions,r));
}
