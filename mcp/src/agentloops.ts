import {z} from 'zod';
import type {CliResult} from './vinput.js';
import {canonicalSequenceScope} from './sequenceconditions.js';
type Obj=Record<string,unknown>;
type Reply={content:({type:'text';text:string}|{type:'image';data:string;mimeType:string})[];isError?:boolean};
type Rect={x:number;y:number;w:number;h:number};type Point={x:number;y:number};
type Mapping={scale:number;offset_x:number;offset_y:number};
type Runtime={signal?:AbortSignal};
type Options={pid:number;coord_space?:'client'|'capture';mapping?:Mapping;capture_after?:boolean|number;capture_settle_ms?:number;region?:Rect;max_width?:number;scale?:number;format?:'png'|'jpeg';quality?:number};
export type LoopContext={owner:string;at:(pid:number)=>string[];run:(argv:string[],budget:number)=>Promise<CliResult>;epoch:(pid:number)=>number;
 map:(pid:number,options:Options,points:[number,number][],expectedScope:unknown)=>{pts:[number,number][];mapped:boolean}|{error:string}|Promise<{pts:[number,number][];mapped:boolean}|{error:string}>;
 captureError:(options:Options)=>string|undefined;finish:(pid:number,out:Reply,options:Options,epoch:number)=>Promise<Reply>;qualifyInput?:(r:CliResult)=>CliResult};
type Register=<S extends z.ZodRawShape>(name:string,title:string,description:string,shape:S,run:(a:z.objectOutputType<S,z.ZodTypeAny>,runtime?:Runtime)=>Promise<Reply>,readOnly?:boolean)=>void;
const obj=(v:unknown):Obj|undefined=>v&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const reply=(j:Obj):Reply=>({content:[{type:'text',text:JSON.stringify(j)}],isError:j.ok!==true});
const u64=(v:unknown):v is string=>typeof v==='string'&&/^[1-9][0-9]{0,19}$/.test(v)&&BigInt(v)<=18446744073709551615n;
const nap=(ms:number)=>new Promise<void>(r=>setTimeout(r,ms));
const sessions=new Set<number>();let nextSession=0;
const inputGenerations=new Map<number,number>();
export function noteOtherInput(pid:number){inputGenerations.set(pid,(inputGenerations.get(pid)??0)+1);}
class NativeFailure extends Error{constructor(message:string,readonly receipt:unknown){super(message);}}
const failureView=(e:unknown):Obj=>e instanceof NativeFailure?{failure_receipt:e.receipt,...(obj(e.receipt)?.anticheat_refused===true?{anticheat_refused:true}:{})}:{};
type Session={pid:number;epoch:number;generation:number;deadline:number;signal?:AbortSignal;scope?:string;nativeDeadline?:string;frequency?:string;ownerCount?:number;id:string};
function check(ctx:LoopContext,s:Session){if(s.signal?.aborted)throw new Error('request_cancelled');if(ctx.epoch(s.pid)!==s.epoch)throw new Error('attachment_changed');if((inputGenerations.get(s.pid)??0)!==s.generation)throw new Error('other_input_command_started');if(performance.now()>=s.deadline)throw new Error('session_deadline');}
function budget(ctx:LoopContext,s:Session,maximum=1000){check(ctx,s);return Math.max(1,Math.min(maximum,Math.floor(s.deadline-performance.now())));}
async function json(ctx:LoopContext,s:Session,request:Obj,maximum=1000){return ctx.run([...ctx.at(s.pid),'json',JSON.stringify(request)],budget(ctx,s,maximum));}
type Sample={hash:string;scope:string;receipt:Obj};
async function sample(ctx:LoopContext,s:Session,region?:Rect):Promise<Sample>{
 const r=await json(ctx,s,{cmd:'frame-hash',observation_only:true,timeout_ms:Math.min(500,budget(ctx,s)),...(region?{region:`${region.x},${region.y},${region.w},${region.h}`}:{})});check(ctx,s);
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
async function finish(ctx:LoopContext,a:Options,out:Reply,s:Session){try{return await ctx.finish(a.pid,out,a,s.epoch);}catch(e){const original=JSON.parse((out.content[0] as {text:string}).text);return reply({...original,ok:false,post_capture_error:String(e)});}finally{sessions.delete(a.pid);}}
const nativeFields=(s:Session)=>({expected_capture_scope:s.scope,admit_before_qpc_ticks:s.nativeDeadline,admit_qpc_frequency:s.frequency});
const qualified=(ctx:LoopContext,r:CliResult)=>ctx.qualifyInput?.(r)??r;
async function points(ctx:LoopContext,s:Session,a:Options,input:Point[]):Promise<Point[]>{
 const mapped=await ctx.map(a.pid,a,input.map(p=>[p.x,p.y]),JSON.parse(s.scope!));check(ctx,s);if('error' in mapped)throw new Error(mapped.error);
 const scope=JSON.parse(s.scope!) as {client_w:number;client_h:number};
 if(mapped.pts.length!==input.length||mapped.pts.some(p=>p.length!==2||!Number.isFinite(p[0])||!Number.isFinite(p[1])||p[0]<0||p[1]<0||p[0]>=scope.client_w||p[1]>=scope.client_h))throw new Error('point_outside_current_client');
 const result=mapped.pts.map(([x,y])=>({x:Math.round(x),y:Math.round(y)}));if(result.some(p=>p.x>=scope.client_w||p.y>=scope.client_h))throw new Error('rounded_point_outside_current_client');return result;
}
const buttons={left:1,right:2,middle:4,x1:5,x2:6} as const;
export type ClickManyOptions=Options&{points:Point[];button?:keyof typeof buttons;timeout_ms?:number;hold_ms?:number;hover_ms?:number;hover_timeout_ms?:number;interval_ms?:number};
export type ClickGridOptions=Omit<ClickManyOptions,'points'>&{x0:number;y0:number;dx:number;dy:number;cols:number;rows:number};
export function gridPoints(a:Pick<ClickGridOptions,'x0'|'y0'|'dx'|'dy'|'cols'|'rows'>):Point[]{
 if(!Number.isInteger(a.cols)||!Number.isInteger(a.rows)||a.cols<1||a.rows<1||a.cols*a.rows>256||![a.x0,a.y0,a.dx,a.dy].every(Number.isFinite))throw new Error('grid requires1..256 finite row-major points');
 return Array.from({length:a.cols*a.rows},(_,i)=>({x:a.x0+i%a.cols*a.dx,y:a.y0+Math.floor(i/a.cols)*a.dy}));
}
function clickAck(r:CliResult,p:Point,s:Session){const j=obj(r.json);return r.ok&&j?.ok===true&&j.pointer_scope_version===1&&j.capture_scope_checked===true&&j.input_admission_version===1&&j.admission_deadline_qpc_ticks===s.nativeDeadline&&j.button_down_count===1&&j.button_released===true&&j.last_press_x===p.x&&j.last_press_y===p.y;}
export async function clickMany(ctx:LoopContext,a:ClickManyOptions,runtime:Runtime={}):Promise<Reply>{
 const invalid=ctx.captureError(a);if(invalid)return reply({ok:false,error:invalid,input_injected:false});
 if(!a.points.length||a.points.length>256||a.points.some(p=>!Number.isFinite(p.x)||!Number.isFinite(p.y)))return reply({ok:false,error:'points require1..256 finite coordinates',input_injected:false});
 if(sessions.has(a.pid))return reply({ok:false,error:'another_bounded_helper_session_active',input_injected:false});sessions.add(a.pid);
 const s:Session={pid:a.pid,epoch:ctx.epoch(a.pid),generation:inputGenerations.get(a.pid)??0,deadline:performance.now()+(a.timeout_ms??30000),signal:runtime.signal,id:`loops-${a.pid}-${++nextSession}`},receipts:Obj[]=[];let error:string|undefined,baseline:Obj|undefined,failure:Obj={};
 try{
  await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);baseline=(await sample(ctx,s)).receipt;const client=await points(ctx,s,a,a.points);
  for(const [index,p] of client.entries()){
   await status(ctx,s,buttons[a.button??'left']);await idleJobs(ctx,s);
   const hold=a.hold_ms??140,hover=a.hover_ms??80,hoverTimeout=a.hover_timeout_ms??1000,required=hold+hover+hoverTimeout+2500;
   if(s.deadline-performance.now()<required)throw new Error('insufficient_budget_for_next_bounded_click');
   const native=await json(ctx,s,{cmd:'click',button:a.button??'left',x:p.x,y:p.y,count:1,ms:hold,settle_ms:hover,hover_timeout_ms:hoverTimeout,...nativeFields(s)},required);
   const result=qualified(ctx,native),confirmed=clickAck(result,p,s);receipts.push({index,requested:a.points[index],client:p,confirmed,receipt:native.json,...(result.json!==native.json?{qualified_receipt:result.json}:{})});
   if(!confirmed)throw new Error('click_acknowledgement_unconfirmed_no_retry');check(ctx,s);
   if(index<client.length-1&&(a.interval_ms??0)>0)await nap(Math.min(a.interval_ms!,budget(ctx,s)));
  }
 }catch(e){error=e instanceof Error?e.message:String(e);failure=failureView(e);}
 const out=reply({ok:!error,pid:a.pid,input_session:{id:s.id,kind:'bounded_mcp_loop',native_batch:false,timeout_ms:a.timeout_ms??30000,admission_deadline_qpc_ticks:s.nativeDeadline??null,stop_reason:error??'all_points_acknowledged'},requested_points:a.points.length,completed_points:receipts.filter(r=>r.confirmed).length,points:receipts,baseline_observation:baseline??null,input_injected:receipts.length?null:false,...failure,...(error?{error}:{}),game_response_measured:false,scope:'one bounded MCP session; each point follows ordinary native click admission and exact scope/deadline checks; no failed or uncertain point is replayed',hint:'Use wait_text for any-of banners or observe_settle for scoped stable frames; capture_after is one image after the entire batch.'});
 return finish(ctx,a,out,s);
}
export async function clickGrid(ctx:LoopContext,a:ClickGridOptions,runtime:Runtime={}):Promise<Reply>{try{return await clickMany(ctx,{...a,points:gridPoints(a)},runtime);}catch(e){return reply({ok:false,error:String(e),input_injected:false});}}
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
 const ok=changed&&stable&&release&&!error,out=reply({ok,pid:a.pid,input_session:{id:s.id,kind:'one_bounded_native_mouse_hold',timeout_ms:a.timeout_ms??10000,admission_deadline_qpc_ticks:s.nativeDeadline??null},move_receipt:move?.json??null,hold_input:initial?.json??null,job_id:admitted?.job_id??null,final_job:job??null,completed_input_read_receipt:completedReadReceipt??null,release_confirmed:release,hold_may_still_be_active:!!initial&&!release&&obj(initial.json)?.input_injected!==false,cleanup,changed,stable,samples,displacement:'UNKNOWN',displacement_px:null,baseline_observation:baseline?.receipt??null,final_observation:last?.receipt??null,input_injected:inputAttempted?null:false,...failure,...(!ok?{error:error??(!changed?'selected_region_did_not_change':!stable?'selected_region_did_not_stabilize':'exact_hold_release_unconfirmed')}:{}),scope:'selected-region change followed by identical hashes during one bounded hold; displacement and causal game movement are not measured; no repeated or indefinite button-down',hint:'Choose a region tracking the character/world rather than an animated HUD. Use observe_settle for observation-only stability.'});
 return finish(ctx,a,out,s);
}
export function registerAgentLoops(register:Register,ctx:LoopContext,capture:z.ZodRawShape,region:z.ZodType<Rect,z.ZodTypeDef,unknown>,coord:z.ZodRawShape){
 const common={...capture,...coord,pid:z.number().int().positive(),button:z.enum(['left','right','middle','x1','x2']).optional(),timeout_ms:z.number().int().min(500).max(60000).optional(),hold_ms:z.number().int().min(1).max(1000).optional(),hover_ms:z.number().int().min(0).max(1000).optional(),hover_timeout_ms:z.number().int().min(1).max(1500).optional(),interval_ms:z.number().int().min(0).max(1000).optional()};
 const description='Place multiple clicks in one bounded MCP session. Each point goes through ordinary native click admission with the same observed capture scope and a shared native deadline; every original receipt is returned. Stops on uncertainty, failed receipt, user activity, scope/attachment change, cancellation or deadline; no point is replayed. Native CLI requests remain per point (native_batch:false). capture_after observes only after the whole batch; use observe_settle or wait_text for visual verification.';
 register('click_many','Click several points',description,{...common,points:z.array(z.object({x:z.number().finite(),y:z.number().finite()}).strict()).min(1).max(256)},async(a,r)=>clickMany(ctx,a as ClickManyOptions,r));
 register('click_grid','Click a grid',description+' Grid order is row-major; at most256 points.',{...common,x0:z.number().finite(),y0:z.number().finite(),dx:z.number().finite(),dy:z.number().finite(),cols:z.number().int().min(1).max(256),rows:z.number().int().min(1).max(256)},async(a,r)=>clickGrid(ctx,a as ClickGridOptions,r));
 register('click_to_move','Hold and verify movement pixels','Move to one point, run ONE bounded native mouse-button hold, then watch a scoped region for change followed by stability. Cancels only its exact job. Native duration releases after disconnect; late queued admission is refused. Returns displacement:UNKNOWN because hash change/stability does not measure character movement or distance. Stops on manual activity, cursor/scope/attachment changes or uncertain receipts; never retries, sends indefinite down, or globally releases. Use observe_settle for read-only stability.',
  {...capture,...coord,pid:z.number().int().positive(),x:z.number().finite(),y:z.number().finite(),region,button:z.enum(['left','right']).optional(),timeout_ms:z.number().int().min(1500).max(60000).optional(),hold_ms:z.number().int().min(100).max(20000).optional(),poll_ms:z.number().int().min(50).max(1000).optional(),stable_samples:z.number().int().min(2).max(10).optional()},async(a,r)=>clickToMove(ctx,a as ClickToMoveOptions,r));
}
