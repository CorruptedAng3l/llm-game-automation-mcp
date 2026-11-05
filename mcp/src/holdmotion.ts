import {runVinput,type CliResult} from './vinput.js';
import {canonicalSequenceScope} from './sequenceconditions.js';
import {motionSample,motionDifference} from './motion.js';
type Obj=Record<string,unknown>;type Rect={x:number;y:number;w:number;h:number};
const u64=(v:unknown):v is string=>typeof v==='string'&&/^[1-9][0-9]{0,19}$/.test(v)&&BigInt(v)<=18446744073709551615n;
export async function measureTimedInput(prefix:string[],region:Rect,epoch:()=>number,apply:(scope:string)=>Promise<CliResult>,run=runVinput):Promise<CliResult>{
 const initial=epoch();
 async function sample(){const r=await run([...prefix,'json',JSON.stringify({cmd:'frame-hash',observation_only:true,motion_sample:true,timeout_ms:500,region:`${region.x},${region.y},${region.w},${region.h}`})],1500),j=r.json as Obj,scope=canonicalSequenceScope(j?.capture_scope),motion=motionSample(j);if(!r.ok||!scope||!motion||j.capture_space_updated!==false||j.timing_clock!=='QueryPerformanceCounter'||!u64(j.capture_qpc_ticks)||!u64(j.qpc_frequency)||epoch()!==initial)throw new Error('scoped region motion observation unavailable or attachment changed');return {scope,motion,qpc:j.capture_qpc_ticks,frequency:j.qpc_frequency};}
 let before:Awaited<ReturnType<typeof sample>>;
 try{before=await sample();}catch(e){return {ok:false,json:{ok:false,input_injected:false,error:String(e)},argv:[],stderr:'',timedOut:false,exitCode:null};}
 const r=await apply(before.scope);const base={region,metric:'coarse_grid_area_average_RGB',measurement:'region_pixels_only',pixel_change_observed:null as boolean|null,input_consumption_measured:false,note:'This measures selected-region pixels only. No pixel change does not establish whether the input was read or accepted; native polling receipts are a separate observation.',pixel_tolerance:8,application_response:null,avatar_movement_measured:false,before_capture_qpc_ticks:before.qpc,scope:'region pixel difference across one completed timed input; animation/camera/UI changes are not avatar displacement or causal input response'};
 if(!r.ok)return {...r,json:{...(r.json as Obj),input_motion:{...base,measured:false,reason:'native input did not complete successfully'}}};
 try{const after=await sample();if(after.scope!==before.scope||after.frequency!==before.frequency||BigInt(after.qpc)<BigInt(before.qpc))throw new Error('capture scope/clock changed');const diff=motionDifference(before.motion,after.motion,8);if(!diff)throw new Error('motion sample shape changed');return {...r,json:{...(r.json as Obj),input_motion:{...base,measured:true,...diff,pixel_change_observed:diff.changed_pct>0,after_capture_qpc_ticks:after.qpc}}};}
 catch(e){return {...r,json:{...(r.json as Obj),input_motion:{...base,measured:false,reason:String(e)},input_motion_warning:'Input receipt retained; following region observation failed. Do not repeat the delivered input.'}};}
}
