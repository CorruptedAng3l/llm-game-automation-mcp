type Obj=Record<string,unknown>;
type Reply={content:({type:'text';text:string}|{type:'image';data:string;mimeType:string})[];isError?:boolean};
const object=(v:unknown):Obj|undefined=>v!==null&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
/** Promote already-measured visual facts; no extra capture/input or causal claim. */
export function actionFeedback(value:unknown):Obj|undefined{
 const j=object(value);if(!j||j.ok!==true||j.accepted===true||j.job_state==='queued'||j.job_state==='running')return;
 const motion=object(j.input_motion),camera=object(j.camera_observation),capture=object(j.capture_after),history=object(capture?.capture_observation);
 let changed:boolean|undefined,scope:string|undefined;
 if(motion?.measured===true&&motion.measurement==='region_pixels_only'&&typeof motion.pixel_change_observed==='boolean'&&motion.application_response===null){changed=motion.pixel_change_observed;scope='selected-region pixels bracket one completed timed input; input causality remains unmeasured';}
 else if(camera?.camera_observation_version===1&&camera.advisory===true&&camera.mode==='post_input'&&typeof camera.pixel_change_observed==='boolean'&&typeof camera.samples==='number'&&camera.samples>=2&&camera.input_consumption_measured===false&&camera.actual_angle===null){changed=camera.pixel_change_observed;scope='selected client-region pixels from the retained pre-input baseline through bounded post-acknowledgement observations; pixels do not establish camera angle, consumption or input causality';}
 else if(history?.version===1&&history.measured===true&&typeof history.pixel_hash_changed==='boolean'){changed=history.pixel_hash_changed;scope='following source-region pixels versus the previous comparable capture in this window lifetime; previous capture need not precede this action';}
 if(changed===undefined)return;
 return {visual_change_observed:changed,visual_observation:{changed,scope,application_response:null,input_consumption_measured:false},...(!changed?{visual_response_note:'Following comparable pixels are unchanged. Input consumption and game response remain unmeasured; do not replay the delivered input automatically.'}:{})};
}
export function withActionFeedback(out:Reply):Reply{
 const first=out.content.find(c=>c.type==='text');if(!first||first.type!=='text')return out;
 try{const j=JSON.parse(first.text),extra=actionFeedback(j);if(extra)first.text=JSON.stringify({...j,...extra});}catch{/* Preserve malformed/native diagnostics exactly. */}return out;
}
