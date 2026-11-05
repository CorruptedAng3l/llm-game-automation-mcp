import {toClient,type Mapping} from './imgtools.js';
import {canonicalSequenceScope} from './sequenceconditions.js';
import type {CliResult} from './vinput.js';
type Obj=Record<string,unknown>;
const obj=(v:unknown):Obj|undefined=>v&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
const finite=(v:unknown):v is number=>typeof v==='number'&&Number.isFinite(v);
const id=(v:unknown):v is string=>typeof v==='string'&&/^[1-9][0-9]{0,19}$/.test(v)&&BigInt(v)<=18446744073709551615n;
export type StoredMapping={mapping:Mapping;scope?:string;mapId?:string;imageW?:number;imageH?:number;epoch:number};
export type MappingUsed={source:'explicit'|'saved_capture';capture_map_id:string|null;capture_scope:Obj|null;
 scale:number;scale_y:number;offset_x:number;offset_y:number;image_w:number|null;image_h:number|null};
export type MappingResolution={pts:[number,number][];mapped:boolean;scope?:string;mapping_used?:MappingUsed}|{error:string};
// Snapshot the conversion's actual inputs, not the map a later capture may publish.
function mappingUsed(mapping:Mapping,scope?:string,saved?:StoredMapping):MappingUsed{
 let capture_scope:Obj|null=null;try{capture_scope=obj(scope?JSON.parse(scope):undefined)??null;}catch{/* A legacy local mapping may have no typed native scope. */}
 return {source:saved?'saved_capture':'explicit',capture_map_id:saved&&id(saved.mapId)?saved.mapId:null,capture_scope,
  scale:mapping.scale,scale_y:mapping.scale_y??mapping.scale,offset_x:mapping.offset_x,offset_y:mapping.offset_y,
  image_w:saved&&Number.isInteger(saved.imageW)&&(saved.imageW??0)>0?saved.imageW!:null,image_h:saved&&Number.isInteger(saved.imageH)&&(saved.imageH??0)>0?saved.imageH!:null};
}
export function nativeMapping(value:unknown,epoch:number,pid?:number):StoredMapping|undefined{
 const j=obj(value),m=obj(j?.capture_mapping),scope=canonicalSequenceScope(j?.capture_scope);
 if(!j||j.ok!==true||j.capture_map_version!==1||!id(j.capture_map_id)||!scope||!m||
    (pid!==undefined&&j.pid!==pid)||!finite(m.scale)||m.scale<=0||!finite(m.scale_y)||m.scale_y<=0||
    !finite(m.offset_x)||!finite(m.offset_y)||!Number.isInteger(m.image_w)||!Number.isInteger(m.image_h)||(m.image_w as number)<1||(m.image_h as number)<1)return;
 return {mapping:{scale:m.scale,scale_y:m.scale_y,offset_x:m.offset_x,offset_y:m.offset_y},scope,mapId:j.capture_map_id,imageW:m.image_w as number,imageH:m.image_h as number,epoch};
}
type Run=(argv:string[],budget:number)=>Promise<CliResult>;
export class CaptureMappings{
 private readonly entries=new Map<number,StoredMapping>();
 clear(pid:number){this.entries.delete(pid);}
 remember(pid:number,entry:StoredMapping){this.entries.set(pid,entry);}
 async resolve(pid:number,options:{coord_space?:'client'|'capture';mapping?:Mapping;norm?:boolean;capture_space?:boolean},points:[number,number][],epoch:number,run:Run,prefix:string[],expectedScope?:unknown):Promise<MappingResolution>{
  if(options.coord_space!=='capture')return {pts:points,mapped:false};
  if(options.norm||options.capture_space)return {error:'coord_space "capture" cannot be combined with norm or capture_space'};
  const expected=expectedScope===undefined?undefined:canonicalSequenceScope(expectedScope);
  if(expectedScope!==undefined&&!expected)return {error:'current capture scope is malformed; no coordinate input was sent'};
  if(options.mapping){
   const transform=options.mapping;
   if(!finite(transform.scale)||transform.scale<=0||!finite(transform.scale_y??transform.scale)||(transform.scale_y??transform.scale)<=0||!finite(transform.offset_x)||!finite(transform.offset_y))return {error:'explicit capture mapping must have finite positive scales and finite offsets; no coordinate input was sent'};
   const pts=points.map(([x,y])=>toClient(transform,x,y));
   if(pts.some(p=>p.some(n=>!Number.isSafeInteger(n)||n< -2147483648||n>2147483647)))return {error:'explicit capture mapping produced coordinates outside native integer bounds; no coordinate input was sent'};
   let scope=expected;
   if(!scope){
    // The transform describes user-supplied geometry, not a target lifetime. Bind it to one fresh read-only scope.
    const observed=await run([...prefix,'json',JSON.stringify({cmd:'frame-hash',observation_only:true,timeout_ms:500})],2000);
    const j=obj(observed.json);scope=canonicalSequenceScope(j?.capture_scope);
    if(!observed.ok||j?.pid!==pid||j.capture_space_updated!==false||!scope)return {error:'explicit capture mapping needs a fresh scoped observation from a matching DLL; no coordinate input was sent'};
   }
   return {pts,mapped:true,scope,mapping_used:mappingUsed(transform,scope)};
  }
  let saved=this.entries.get(pid);if(saved?.epoch!==epoch)saved=undefined;
  if(!saved){
   const r=await run([...prefix,'json',JSON.stringify({cmd:'capture-map'})],2000);
   if(!r.ok)return {error:`capture mapping unavailable: ${String(obj(r.json)?.error??'native target/transport failure')}`};
   saved=nativeMapping(r.json,epoch,pid);
   if(!saved)return {error:'coord_space "capture" needs a capture first or a matching DLL with persisted capture-map support; an explicit mapping is also available'};
   this.entries.set(pid,saved);
  }
  if(expected&&saved.scope!==expected)return {error:'last capture belongs to a different window lifetime/geometry; capture again before coordinate input'};
  return {pts:points.map(([x,y])=>toClient(saved!.mapping,x,y)),mapped:true,...(saved.scope?{scope:saved.scope}:{}),mapping_used:mappingUsed(saved.mapping,saved.scope,saved)};
 }
}
