// Pure, bounded pixel observation. This module never sends input or captures.
import {motionDifference,motionSample,type MotionSample} from './motion.js';

type Obj=Record<string,unknown>;
export type CameraMethod='swapchain'|'printwindow';
export type CameraObservationMode='passive'|'post_input';
export type CameraObservationMetric='exact_hash'|'rgb_grid';
export type CameraObservationOutcome='observing'|'settled'|'no_change'|'timeout'|'unavailable';
export type CameraFrame={
 readonly scope:string;readonly method:CameraMethod;readonly capturePath:string|null;
 readonly qpc:bigint;readonly frequency:bigint;readonly hash:string;
 readonly frameQpc:bigint|null;readonly frameIndex:bigint|null;
 readonly region:string|null;readonly motion:MotionSample|undefined;
};
export type CameraFrameResult={ok:true;sample:CameraFrame}|{ok:false;reason:string;error:string};
export type CameraObservationOptions={
 mode?:CameraObservationMode;metric?:CameraObservationMetric;quiet_ms?:number;
 min_fresh_samples?:number;timeout_ms?:number;max_samples?:number;
 max_change_pct?:number;pixel_tolerance?:number;
};
export type CameraObservationReport={
 camera_observation_version:1;advisory:true;mode:CameraObservationMode;metric:CameraObservationMetric;
 outcome:CameraObservationOutcome;terminal:boolean;stop_reason:string|null;error:string|null;
 settled:boolean;passive_quiet:boolean;post_input_pixels_settled:boolean;
 pixel_change_observed:boolean|null;onset_observed:boolean|null;
 quiet_required_ms:number;quiet_observed_ms:number;quiet_fresh_samples:number;min_fresh_samples:number;
 samples:number;rejected_samples:number;max_samples:number;elapsed_ms:number;timeout_ms:number;
 max_change_pct:number;pixel_tolerance:number;last_change_pct:number|null;
 timing_clock:'QueryPerformanceCounter';qpc_frequency:string|null;
 baseline_capture_qpc_ticks:string|null;last_capture_qpc_ticks:string|null;
 onset_capture_qpc_ticks:string|null;quiet_anchor_capture_qpc_ticks:string|null;
 before_hash:string|null;after_hash:string|null;capture_scope:Obj|null;method:CameraMethod|null;
 frame_freshness:'unmeasured_printwindow'|'unmeasured_native_source'|'native_hook_witness_unadvanced'|'native_hook_witness_advanced'|'unavailable';
 native_source_frame_progress_observed:boolean|null;last_frame_qpc_ticks:string|null;last_frame_index:string|null;
 rendered_frame_freshness_measured:false;actual_angle:null;camera_angle_measured:false;
 input_consumption_measured:false;queue_drain_measured:false;do_not_repeat:boolean;guidance:string;scope:string;
};
const obj=(v:unknown):Obj|undefined=>v!==null&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const integer=(v:unknown,low:number,high:number):v is number=>typeof v==='number'&&Number.isSafeInteger(v)&&v>=low&&v<=high;
const u64=(v:unknown,positive=false):v is string=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=18446744073709551615n&&(!positive||BigInt(v)>0n);
const scopeFields=['hwnd','thread','adoption_generation','geometry_epoch','lifetime_cookie','client_w','client_h','geometry_witness'];
/** Native-object canonical key, deliberately separate from legacy angle-profile keys. */
export function canonicalCameraScope(value:unknown):string|undefined{
 const s=obj(value);
 if(!s||Object.keys(s).length!==scopeFields.length||Object.keys(s).some(k=>!scopeFields.includes(k))||
  typeof s.hwnd!=='string'||!/^0x[0-9a-f]{1,16}$/i.test(s.hwnd)||BigInt(s.hwnd)===0n||
  !integer(s.thread,1,4294967295)||!u64(s.adoption_generation,true)||!u64(s.geometry_epoch)||!u64(s.lifetime_cookie,true)||
  !integer(s.client_w,1,32768)||!integer(s.client_h,1,32768)||typeof s.geometry_witness!=='string'||
  !s.geometry_witness.length||s.geometry_witness.length>512||s.geometry_witness.includes('\0'))return;
 return JSON.stringify({hwnd:s.hwnd,thread:s.thread,adoption_generation:s.adoption_generation,geometry_epoch:s.geometry_epoch,
  lifetime_cookie:s.lifetime_cookie,client_w:s.client_w,client_h:s.client_h,geometry_witness:s.geometry_witness});
}
const invalid=(reason:string,error:string):CameraFrameResult=>({ok:false,reason,error});
/** Parse known bounded fields only; no reference to the potentially large raw receipt is retained. */
export function parseCameraFrame(value:unknown,options:{require_motion?:boolean}={}):CameraFrameResult{
 const r=obj(value),scope=canonicalCameraScope(r?.capture_scope);
 if(!r||r.ok!==true)return invalid('capture_unavailable','capture did not report exact boolean success');
 if(!scope)return invalid('capture_scope_invalid','capture scope lacks the exact bounded native window/lifetime/geometry fields');
 if(r.capture_space_updated!==false)return invalid('capture_map_not_preserved','capture must confirm capture_space_updated:false');
 if(r.method!=='swapchain'&&r.method!=='printwindow')return invalid('capture_method_invalid','capture method must identify swapchain or printwindow');
 if(r.timing_clock!=='QueryPerformanceCounter'||!u64(r.capture_qpc_ticks,true)||!u64(r.qpc_frequency,true))
  return invalid('capture_clock_invalid','capture needs positive canonical QPC ticks/frequency');
 if(typeof r.hash!=='string'||!/^[0-9a-f]{16}$/i.test(r.hash))return invalid('capture_hash_invalid','capture needs one exact16-digit source-region hash');
 if(r.capture_path!==undefined&&(typeof r.capture_path!=='string'||!r.capture_path.length||r.capture_path.length>64||r.capture_path.includes('\0')))
  return invalid('capture_path_invalid','capture path must be a bounded nonempty string when supplied');
 let region:string|null=null;
 const regionKeys=['origin_x','origin_y','region_client_w','region_client_h'];
 if(regionKeys.some(k=>r[k]!==undefined)){
  const [x,y,w,h]=regionKeys.map(k=>r[k]);const s=JSON.parse(scope) as Obj;
  if(![x,y,w,h].every(v=>typeof v==='number'&&Number.isFinite(v))||Number(x)<0||Number(y)<0||Number(w)<=0||Number(h)<=0||
   Number(x)+Number(w)>Number(s.client_w)||Number(y)+Number(h)>Number(s.client_h))return invalid('capture_region_invalid','capture region witness must fit its current client');
  region=JSON.stringify([x,y,w,h]);
 }
 const hasMotion=['motion_sample_version','motion_sample_w','motion_sample_h','motion_sample_rgb'].some(k=>r[k]!==undefined);
 const motion=motionSample(r);
 if((options.require_motion||hasMotion)&&!motion)return invalid('motion_sample_invalid','bounded RGB-grid metadata is missing or invalid');
 let frameQpc:bigint|null=null,frameIndex:bigint|null=null;
 if(r.method==='swapchain'){
  if(r.frame_qpc_ticks!==undefined&&r.frame_qpc_ticks!==null){
   if(!u64(r.frame_qpc_ticks))return invalid('source_frame_clock_invalid','native source-frame QPC must be a canonical bounded string');
   if(BigInt(r.frame_qpc_ticks)>0n)frameQpc=BigInt(r.frame_qpc_ticks);
   if(frameQpc!==null&&frameQpc>BigInt(r.capture_qpc_ticks))return invalid('source_frame_clock_future','native source-frame witness is newer than its capture');
  }
  for(const k of ['frame_index','captured_present','frame'])if(r[k]!==undefined&&r[k]!==null&&!integer(r[k],0,Number.MAX_SAFE_INTEGER))
   return invalid('source_frame_index_invalid','native source-frame index must be a nonnegative exact integer');
  const n=r.frame_index??r.captured_present??r.frame;
  if(typeof n==='number'&&n>0)frameIndex=BigInt(n);
 }
 // PrintWindow may carry aggregate Present counters. They never establish its pixel freshness.
 return {ok:true,sample:{scope,method:r.method,capturePath:typeof r.capture_path==='string'?r.capture_path:null,
  qpc:BigInt(r.capture_qpc_ticks),frequency:BigInt(r.qpc_frequency),hash:r.hash.toUpperCase(),frameQpc,frameIndex,region,motion}};
}
type Settings=Required<CameraObservationOptions>;
function settings(options:CameraObservationOptions):Settings{
 const s:Settings={mode:options.mode??'passive',metric:options.metric??'exact_hash',quiet_ms:options.quiet_ms??500,
  min_fresh_samples:options.min_fresh_samples??2,timeout_ms:options.timeout_ms??4000,max_samples:options.max_samples??60,
  max_change_pct:options.max_change_pct??0,pixel_tolerance:options.pixel_tolerance??8};
 if(!['passive','post_input'].includes(s.mode)||!['exact_hash','rgb_grid'].includes(s.metric)||!integer(s.quiet_ms,100,2000)||
  !integer(s.min_fresh_samples,2,10)||!integer(s.timeout_ms,100,60000)||!integer(s.max_samples,3,60)||s.max_samples<=s.min_fresh_samples||
  !Number.isFinite(s.max_change_pct)||s.max_change_pct<0||s.max_change_pct>100||!integer(s.pixel_tolerance,0,255)||
  s.metric==='exact_hash'&&s.max_change_pct!==0)throw new RangeError('camera observation needs quiet_ms100..2000, min_fresh_samples2..10, timeout_ms100..60000, max_samples3..60 greater than min_fresh_samples, and valid metric/tolerance');
 return s;
}
/**
 * Wall milliseconds are supplied by the caller's monotonic clock. The baseline
 * may precede a known input ACK; construct at the start of the observation budget.
 * Only this policy's QPC samples can establish its quiet interval.
 */
export class CameraObservation{
 readonly #options:Settings;readonly #started:number;readonly #deadline:number;
 #wall:number;#outcome:CameraObservationOutcome='observing';#reason:string|null=null;#error:string|null=null;
 #baseline:CameraFrame|undefined;#last:CameraFrame|undefined;#anchor:CameraFrame|undefined;
 #onset:bigint|null=null;#samples=0;#rejected=0;#fresh=0;#changed=false;#comparisons=0;#lastChange:number|null=null;#sourceProgress=false;
 constructor(baseline:unknown,options:CameraObservationOptions={},startedWallMs=0){
  this.#options=settings(options);
  if(!Number.isFinite(startedWallMs)||startedWallMs<0||startedWallMs+this.#options.timeout_ms>Number.MAX_SAFE_INTEGER)throw new RangeError('camera observation needs a bounded nonnegative monotonic wall start');
  this.#started=this.#wall=startedWallMs;this.#deadline=startedWallMs+this.#options.timeout_ms;
  const parsed=parseCameraFrame(baseline,{require_motion:this.#options.metric==='rgb_grid'});
  if(!parsed.ok){this.#fail(parsed.reason,parsed.error);return;}
  this.#baseline=this.#last=this.#anchor=parsed.sample;this.#samples=1;
 }
 #fail(reason:string,error:string){this.#outcome='unavailable';this.#reason=reason;this.#error=error;}
 #end(reason:string){
  this.#reason=reason;
  if(this.#samples<2)this.#fail(reason,'observation ended without a fresh comparable sample');
  else this.#outcome=this.#options.mode==='post_input'&&this.#onset===null?'no_change':'timeout';
 }
 #wallAdmit(now:number):boolean{
  if(this.#outcome!=='observing')return false;
  if(!Number.isFinite(now)||now<this.#wall||now<0){this.#fail('wall_clock_regressed','wall clock is unavailable or regressed');return false;}
  this.#wall=now;
  if(now>=this.#deadline){this.#end('wall_deadline');return false;}
  return true;
 }
 #difference(a:CameraFrame,b:CameraFrame):number{
  if(this.#options.metric==='exact_hash')return a.hash===b.hash?0:100;
  return motionDifference(a.motion,b.motion,this.#options.pixel_tolerance)!.changed_pct;
 }
 observe(value:unknown,wallNowMs:number):CameraObservationReport{
  if(!this.#wallAdmit(wallNowMs))return this.snapshot();
  const parsed=parseCameraFrame(value,{require_motion:this.#options.metric==='rgb_grid'});
  const reject=(reason:string,error:string)=>{this.#rejected++;this.#fail(reason,error);return this.snapshot();};
  if(!parsed.ok)return reject(parsed.reason,parsed.error);
  const next=parsed.sample,last=this.#last!;
  if(next.scope!==last.scope)return reject('capture_scope_changed','window, lifetime or geometry changed during observation');
  if(next.method!==last.method||next.capturePath!==last.capturePath)return reject('capture_method_changed','capture method or source path changed during observation');
  if(next.region!==last.region)return reject('capture_region_changed','source-region witness changed during observation');
  if(next.frequency!==last.frequency)return reject('capture_frequency_changed','capture QPC frequency changed during observation');
  if(next.qpc<=last.qpc)return reject('capture_clock_not_advanced','capture QPC did not advance strictly');
  if(this.#options.metric==='rgb_grid'&&(!next.motion||next.motion.w!==last.motion!.w||next.motion.h!==last.motion!.h))return reject('motion_layout_changed','RGB-grid layout changed during observation');
  let sourceAdvanced=false;
  for(const [a,b] of [[last.frameQpc,next.frameQpc],[last.frameIndex,next.frameIndex]]){
   if(a!==null&&(b===null||b<=a))return reject('native_source_frame_not_advanced','available native source-frame witness disappeared, repeated or regressed');
   if(a!==null&&b!==null)sourceAdvanced=true;
  }
  this.#sourceProgress||=sourceAdvanced;
  this.#last=next;this.#samples++;this.#comparisons++;
  const fromBaseline=this.#difference(this.#baseline!,next);
  if(fromBaseline>this.#options.max_change_pct)this.#changed=true;
  if(this.#options.mode==='post_input'&&this.#onset===null){
   if(fromBaseline<=this.#options.max_change_pct){if(this.#samples>=this.#options.max_samples)this.#end('sample_limit');return this.snapshot();}
   this.#onset=next.qpc;this.#anchor=next;this.#fresh=0;this.#lastChange=fromBaseline;
  }else{
   this.#lastChange=this.#difference(this.#anchor!,next);
   if(this.#lastChange>this.#options.max_change_pct){this.#anchor=next;this.#fresh=0;}
   else this.#fresh++;
  }
  if(this.#fresh>=this.#options.min_fresh_samples&&(next.qpc-this.#anchor!.qpc)*1000n>=BigInt(this.#options.quiet_ms)*next.frequency){this.#outcome='settled';this.#reason='selected_pixels_quiet';}
  else if(this.#samples>=this.#options.max_samples)this.#end('sample_limit');
  return this.snapshot();
 }
 tick(wallNowMs:number):CameraObservationReport{this.#wallAdmit(wallNowMs);return this.snapshot();}
 unavailable(error:string,wallNowMs:number):CameraObservationReport{
  if(this.#wallAdmit(wallNowMs))this.#fail('observation_unavailable',String(error).slice(0,1024));
  return this.snapshot();
 }
 snapshot():CameraObservationReport{
  const first=this.#baseline,last=this.#last,o=this.#options,settled=this.#outcome==='settled';
  const activeQuiet=o.mode==='passive'||this.#onset!==null;
  const quiet=activeQuiet&&last&&this.#anchor?Number((last.qpc-this.#anchor.qpc)*1000000n/last.frequency)/1000:0;
  const sourceAvailable=last&&(last.frameQpc!==null||last.frameIndex!==null);
  const freshness=!last?'unavailable':last.method==='printwindow'?'unmeasured_printwindow':!sourceAvailable?'unmeasured_native_source':this.#sourceProgress?'native_hook_witness_advanced':'native_hook_witness_unadvanced';
  return {camera_observation_version:1,advisory:true,mode:o.mode,metric:o.metric,outcome:this.#outcome,terminal:this.#outcome!=='observing',stop_reason:this.#reason,error:this.#error,
   settled,passive_quiet:settled&&o.mode==='passive',post_input_pixels_settled:settled&&o.mode==='post_input'&&this.#onset!==null,
   pixel_change_observed:this.#comparisons?this.#changed:null,onset_observed:o.mode==='post_input'?this.#onset!==null:null,
   quiet_required_ms:o.quiet_ms,quiet_observed_ms:quiet,quiet_fresh_samples:this.#fresh,min_fresh_samples:o.min_fresh_samples,samples:this.#samples,rejected_samples:this.#rejected,max_samples:o.max_samples,
   elapsed_ms:Math.round(this.#wall-this.#started),timeout_ms:o.timeout_ms,max_change_pct:o.max_change_pct,pixel_tolerance:o.pixel_tolerance,last_change_pct:this.#lastChange,
   timing_clock:'QueryPerformanceCounter',qpc_frequency:first?.frequency.toString()??null,baseline_capture_qpc_ticks:first?.qpc.toString()??null,last_capture_qpc_ticks:last?.qpc.toString()??null,
   onset_capture_qpc_ticks:this.#onset?.toString()??null,quiet_anchor_capture_qpc_ticks:activeQuiet?this.#anchor?.qpc.toString()??null:null,
   before_hash:first?.hash??null,after_hash:last?.hash??null,capture_scope:first?JSON.parse(first.scope) as Obj:null,method:last?.method??null,frame_freshness:freshness,
   native_source_frame_progress_observed:this.#sourceProgress?true:null,last_frame_qpc_ticks:last?.frameQpc?.toString()??null,last_frame_index:last?.frameIndex?.toString()??null,
   rendered_frame_freshness_measured:false,actual_angle:null,camera_angle_measured:false,input_consumption_measured:false,queue_drain_measured:false,do_not_repeat:o.mode==='post_input',
   guidance:o.mode==='post_input'?'This observer never sends or reverses input. Preserve the native input receipt; do not repeat it because pixels did not change or observation failed.':'Passive pixel quiet is not post-input settling, queue drain or camera-angle evidence.',
   scope:'Selected-region pixels at strictly advancing capture QPC; available native hook witnesses are checked separately. Quiet does not prove camera causality, rendered/display freshness, queue drain or gameplay response.'};
 }
}
