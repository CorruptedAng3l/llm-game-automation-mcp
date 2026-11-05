type Obj=Record<string,unknown>;
const object=(v:unknown):v is Obj=>!!v&&typeof v==='object'&&!Array.isArray(v);
function api(value:unknown):string|undefined{if(typeof value!=='string')return;const n=value.toLowerCase().replace(/[^a-z0-9]/g,'');if(['d3d9','d3d11','d3d12','dxgi','gl','opengl','vk','vulkan','ddraw','directdraw'].includes(n))return n;}
function evidence(v:unknown,key=''):boolean{
 if(object(v))return Object.entries(v).some(([k,x])=>evidence(x,k));
 if(/warning|error|refus|fail|lost|blocked|hresult/i.test(key))return typeof v==='string'?v!==''&&!/^(null|none|s_ok|0x0+|0)$/i.test(v):v===true||typeof v==='number'&&v!==0;
 return /present|flip|blt|frames/i.test(key)&&typeof v==='number'&&v>0;
}
/** Drop only inactive legacy backend blocks with neither activity nor failures. */
export function compactRenderBackends(value:unknown,inherited?:string):unknown{
 if(Array.isArray(value))return value.map(x=>compactRenderBackends(x,inherited));if(!object(value))return value;
 const active=api(value.frame_api)??api(value.capture_path)??inherited,out:Obj={};
 for(const [k,v] of Object.entries(value)){
  if((k==='d3d9'||k==='ddraw')&&active&&!(k==='d3d9'?active==='d3d9':['ddraw','directdraw'].includes(active))&&object(v)&&!evidence(v))continue;
  out[k]=compactRenderBackends(v,active);
 }
 return out;
}
