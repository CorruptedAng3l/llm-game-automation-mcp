// Retained review images from the SAME exact-job observer. No native input.
import {mkdirSync,readFileSync,rmSync,statSync,writeFileSync} from 'node:fs';
import {resolve,join,dirname} from 'node:path';
import {randomUUID} from 'node:crypto';
import {captureDir,runVinput} from './vinput.js';
import {decodePng,encodePng,type Image} from './png.js';
import {resample} from './imgtools.js';
import type {Job,SequenceWaitContext} from './sequencewait.js';

type Obj=Record<string,unknown>;
type Rect={x:number;y:number;w:number;h:number};
export type SequenceCaptureOptions={capture_every_ms?:number;capture_max_frames?:number;capture_region?:Rect;capture_max_width?:number;capture_out_dir?:string};
export type CapturePlan={interval:number;maxFrames:number;region?:Rect;maxWidth:number;directory:string};
const object=(v:unknown):Obj|undefined=>v!==null&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const integer=(v:unknown,min:number,max:number):v is number=>typeof v==='number'&&Number.isInteger(v)&&v>=min&&v<=max;
const uint=(v:unknown):v is string=>typeof v==='string'&&/^[1-9][0-9]{0,19}$/.test(v)&&BigInt(v)<=18446744073709551615n;
const MAX_FRAME_BYTES=4*1024*1024,MAX_TOTAL_BYTES=32*1024*1024,MAX_FRAME_PIXELS=1024*1024;

export function prepareCapturePlan(a:SequenceCaptureOptions):CapturePlan|undefined{
 if(a.capture_every_ms===undefined){if([a.capture_max_frames,a.capture_region,a.capture_max_width,a.capture_out_dir].some(v=>v!==undefined))throw new Error('capture options require capture_every_ms');return;}
 if(!integer(a.capture_every_ms,250,30000)||!integer(a.capture_max_frames??16,1,32)||!integer(a.capture_max_width??640,16,960))throw new Error('periodic capture needs interval250..30000ms, max_frames1..32 and max_width16..960');
 if(a.capture_region&&!(['x','y','w','h'] as const).every(k=>integer(a.capture_region![k],k==='x'||k==='y'?0:1,32768)))throw new Error('capture_region must be a bounded client rectangle');
 if(a.capture_out_dir!==undefined&&(!a.capture_out_dir.trim()||a.capture_out_dir.includes('\0')))throw new Error('capture_out_dir must be a nonempty path without NUL');
 const directory=resolve(a.capture_out_dir??captureDir());mkdirSync(directory,{recursive:true});
 return {interval:a.capture_every_ms,maxFrames:a.capture_max_frames??16,region:a.capture_region,maxWidth:a.capture_max_width??640,directory};
}

type Frame=Obj&{path:string;w:number;h:number};
export class SequenceCaptures{
 readonly frames:Frame[]=[];
 private nextDue:number|undefined;
 private disabled=false;
 private totalBytes=0;
 private skipped=0;
 private warnings:string[]=[];
 private capped=false;
 private preview:Obj|undefined;
 private finished=false;
 constructor(private ctx:SequenceWaitContext,private plan:CapturePlan,private scope:string,private frequency:string,
  private scopeKey:(value:unknown)=>string|undefined,private run=runVinput,private qpcFloor?:string){}
 due(now:number):boolean{
  if(this.disabled)return false;
  if(this.nextDue===undefined)this.nextDue=now;
  if(this.frames.length>=this.plan.maxFrames){this.capped=true;return false;}
  return now>=this.nextDue;
 }
 async capture(before:Job,budget:number):Promise<Frame|undefined>{
  if(this.disabled||this.frames.length>=this.plan.maxFrames)return;
  const now=performance.now();
  const skipped=this.nextDue===undefined?0:Math.max(0,Math.floor((now-this.nextDue)/this.plan.interval));this.skipped+=skipped;
  this.nextDue=(this.nextDue??now)+(skipped+1)*this.plan.interval;
  const path=join(this.plan.directory,`pid${this.ctx.pid}_sequence_${randomUUID()}.png`);
  try{
   if(this.ctx.currentEpoch()!==this.ctx.epoch||this.ctx.signal?.aborted)throw new Error('attachment changed or request cancelled before periodic capture');
   if(budget<1)throw new Error('periodic observation budget expired');
   const request={cmd:'capture',path,observation_only:true,preserve_capture_space:true,after_frames:0,timeout_ms:Math.min(500,Math.ceil(budget)),max_width:this.plan.maxWidth,format:'png',...(this.plan.region?{region:`${this.plan.region.x},${this.plan.region.y},${this.plan.region.w},${this.plan.region.h}`}:{})};
   const r=await this.run([...this.ctx.prefix,'json',JSON.stringify(request)],Math.min(1000,Math.ceil(budget))),j=object(r.json);
   if(this.ctx.currentEpoch()!==this.ctx.epoch||this.ctx.signal?.aborted)throw new Error('attachment changed or request cancelled during periodic capture');
   if(!r.ok||!j||j.path!==path||this.scopeKey(j.capture_scope)!==this.scope||j.capture_space_updated!==false||j.timing_clock!=='QueryPerformanceCounter'||j.qpc_frequency!==this.frequency||!uint(j.capture_qpc_ticks)||!integer(j.w,1,this.plan.maxWidth)||!integer(j.h,1,32768)||!integer(j.region_client_w,1,32768)||!integer(j.region_client_h,1,32768)||!integer(j.origin_x,0,32768)||!integer(j.origin_y,0,32768))throw new Error(`periodic scoped capture unavailable: ${String(j?.error??'missing scope/clock/path/mapping contract')}`);
   const previous=this.frames.at(-1);if(previous&&BigInt(j.capture_qpc_ticks)<BigInt(previous.capture_qpc_ticks as string))throw new Error('periodic capture QPC regressed');
   if(this.qpcFloor&&BigInt(j.capture_qpc_ticks)<BigInt(this.qpcFloor))throw new Error('periodic capture predates its scoped baseline');
   const bytes=statSync(path).size;
   if(bytes>MAX_FRAME_BYTES||this.totalBytes+bytes>MAX_TOTAL_BYTES-MAX_FRAME_BYTES||j.w*j.h>MAX_FRAME_PIXELS)throw new Error('periodic capture file/pixel budget exceeded');
   const png=readFileSync(path);if(png.length<24||png.readUInt32BE(16)!==j.w||png.readUInt32BE(20)!==j.h)throw new Error('periodic PNG header dimensions did not match its receipt');
   const decoded=decodePng(png);if(decoded.width!==j.w||decoded.height!==j.h)throw new Error('periodic image dimensions did not match its receipt');
   const frame:Frame={index:this.frames.length,path,w:j.w,h:j.h,bytes,job_id:before.job_id,job_state_before:before.job_state,step_index_before:before.sequence_step??null,job_state_after:null,step_index_after:null,bracket_confirmed:false,
    capture_scope:j.capture_scope,timing_clock:j.timing_clock,qpc_frequency:j.qpc_frequency,capture_qpc_ticks:j.capture_qpc_ticks,frame_qpc_ticks:j.method!=='printwindow'&&uint(j.frame_qpc_ticks)?j.frame_qpc_ticks:null,frame_index:j.method!=='printwindow'&&integer(j.captured_present??j.frame,1,2147483647)?j.captured_present??j.frame:null,method:typeof j.method==='string'?j.method:null,
    mapping:{scale:j.w/j.region_client_w,scale_y:j.h/j.region_client_h,offset_x:j.origin_x,offset_y:j.origin_y},observation_scope:'before/after exact-job witnesses bracket this independent capture only when bracket_confirmed:true; no exact step-to-pixel causality, scanout or application-response claim'};
   this.totalBytes+=bytes;this.frames.push(frame);return frame;
  }catch(e){try{rmSync(path,{force:true});}catch{/* Generated path only. */}this.disabled=true;this.warnings.push(`Periodic recording stopped: ${String(e)}. Native input/observer receipts are retained; do not replay the sequence.`);return;}
 }
 confirm(frame:Frame,after:Job){if(frame.job_id!==after.job_id)throw new Error('post-capture witness identifies a different job');frame.job_state_after=after.job_state;frame.step_index_after=after.sequence_step??null;frame.job_complete_after=after.job_complete;frame.bracket_confirmed=true;}
 finish():Obj{
  if(!this.finished){this.finished=true;if(this.frames.length)try{this.preview=this.sheet();}catch(e){this.warnings.push(`Saved frames retained; review contact sheet unavailable: ${String(e)}`);}}
  return {requested:true,interval_ms:this.plan.interval,max_frames:this.plan.maxFrames,recorded_frames:this.frames.length,frames:this.frames,skipped_intervals:this.skipped,capped:this.capped||this.frames.length>=this.plan.maxFrames,recording_disabled:this.disabled,saved_bytes:this.totalBytes+Number(this.preview?.bytes??0),retained_frame_bytes:this.totalBytes,file_budget_bytes:MAX_TOTAL_BYTES,
   ...(this.preview?{contact_sheet:this.preview}:{}),...(this.warnings.length?{warnings:this.warnings}:{}),scope:'serial captures inside this exact guarded job observer; first running witness then interval deadlines; late intervals skipped, not replayed; job checkpoints bracket each frame; native guard expiry remains the input cleanup bound'};
 }
 private sheet():Obj{
  const columns=Math.min(4,this.frames.length),cellW=320,cellH=240,width=columns*cellW,height=Math.ceil(this.frames.length/columns)*cellH,data=Buffer.alloc(width*height*4,24);
  for(let n=3;n<data.length;n+=4)data[n]=255;
  for(let n=0;n<this.frames.length;n++){
   const frame=this.frames[n],decoded=decodePng(readFileSync(frame.path)),factor=Math.min(1,(cellW-4)/decoded.width,(cellH-4)/decoded.height),w=Math.max(1,Math.floor(decoded.width*factor)),h=Math.max(1,Math.floor(decoded.height*factor)),small=resample(decoded,w,h);
   const ox=n%columns*cellW+2,oy=Math.floor(n/columns)*cellH+2;
   for(let y=0;y<h;y++)small.data.copy(data,((oy+y)*width+ox)*4,y*w*4,(y+1)*w*4);
  }
  let image:Image={width,height,data},png=encodePng(image);if(png.length>MAX_FRAME_BYTES){image=resample(image,Math.max(1,Math.floor(width/2)),Math.max(1,Math.floor(height/2)));png=encodePng(image);}
  if(png.length>MAX_FRAME_BYTES)throw new Error('review sheet exceeds its reserved file budget');
  const path=join(dirname(this.frames[0].path),`pid${this.ctx.pid}_sequence_sheet_${randomUUID()}.png`);writeFileSync(path,png);
  return {path,w:image.width,h:image.height,bytes:png.length,columns,tiles:this.frames.length,row_major:true,coordinate_system:'contact_sheet_not_client_pixels',note:'Tiles follow frames[] order. Use each saved frame and mapping for coordinates; the sheet is not a client screenshot.'};
 }
}

export function sequenceFollowAction(value:unknown,pid:number):Obj|undefined{
 const j=object(value);if(!integer(pid,1,4294967295)||j?.ok!==true||j.accepted!==true||j.job_complete!==false||typeof j.job_id!=='string')return;
 const parts=j.job_id.split(':');if(parts.length!==3||parts[0]!==String(pid)||!uint(parts[1])||!uint(parts[2]))return;
 return {tool:'job_status',arguments:{pid,id:j.job_id},note:'This sequence is already admitted. Follow this exact job; do not run it again. For a future new sequence, choose wait:true up front to block through its final state.'};
}
