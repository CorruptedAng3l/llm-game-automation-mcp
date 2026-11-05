// Fake SDK/CLI only: completed action presentation does not change native replies or success.
import {makeChecker,startFake} from "./lib.mjs";
const {state,check}=makeChecker(),pid=process.pid;
const done={ok:true,accepted:false,job_id:"done-1",job_state:"completed",job_complete:true,input_injected:true,transitions:2,pad_updates:3,delivery:"virtual publication; application response is not implied"};
const t=await startFake({replies:{key:done,pad:done,json:done,seq:done}});
function completed(r,label){
 check(!r.isError&&r.json?.ok===true&&!("accepted" in r.json)&&r.json?.queued_async===undefined,label+": completed outcome omits ambiguous acceptance without inventing queue history",r.text);
 check(r.json?.job_id===done.job_id&&r.json?.job_state==="completed"&&r.json?.job_complete===true&&r.json?.input_injected===true&&r.json?.transitions===2&&r.json?.pad_updates===3&&r.json?.delivery===done.delivery,label+": identity/state/channel evidence preserved",r.text);
}
try{
 completed(await t.call("key",{pid,key:"a",hold_ms:150}),"synchronous key");
 completed(await t.call("pad",{pid,buttons:"a",ms:150,capture_after:false}),"synchronous pad");
 const move=await t.call("pad_move",{pid,direction:"up",ms:150,capture_after:false});completed(move,"pad_move after final metadata merge");
 check(move.json?.pad_move?.direction==="up"&&move.json?.pad_move?.screen_motion_verified===false,"controller annotation preserved without application claim",move.text);
 const fast=await t.call("key",{pid,key:"a",hold_ms:150,async:true});completed(fast,"fast-completed async request");
 check(fast.json?.note===undefined,"completed async reply no longer claims to be running",fast.text);
 const seq=await t.call("key_sequence",{pid,steps:[{action:"key",key:"a",ms:150}]});completed(seq,"completed sequence with jobExtra");
 check(seq.json?.note===undefined,"completed sequence cannot reintroduce running hint",seq.text);
 const running={ok:true,accepted:true,job_id:"run-1",job_state:"running",job_complete:false,pad_updates:1};
 t.setScenario({replies:{key:running}});const live=await t.call("key",{pid,key:"a",hold_ms:1000,async:true});
 check(!live.isError&&live.json?.accepted===true&&live.json?.queued_async===undefined&&live.json?.job_state==="running"&&/running in the background/.test(live.json?.note??""),"running async accepted:true and hint unchanged",live.text);
 const refused={ok:false,accepted:false,job_id:"bad-1",job_state:"failed",job_complete:true,error:"publication refused",input_injected:true};
 t.setScenario({replies:{key:refused}});const bad=await t.call("key",{pid,key:"a",hold_ms:150,async:true});
 check(bad.isError&&JSON.stringify(bad.json)===JSON.stringify(refused),"failed job remains exact refusal and partial effects, no success or hint",bad.text);
 t.setScenario({replies:{key:{...done,_exit:2}}});const wrongExit=await t.call("key",{pid,key:"a",hold_ms:150,async:true});
 check(wrongExit.isError&&wrongExit.json?.accepted===false&&wrongExit.json?.queued_async===undefined&&wrongExit.json?.job_state==="completed","nonzero CLI exit does not become success or normalized ACK",wrongExit.text);
 for(const [name,payload] of [
  ["nonjob",{ok:true,accepted:false,buttons:4096}],
  ["missing job id",{...done,job_id:undefined}],
  ["empty job id",{...done,job_id:""}],
  ["string accepted",{...done,accepted:"false"}],
  ["accepted true completed",{...done,accepted:true}],
  ["wrong state case",{...done,job_state:"Completed"}],
 ]){
  t.setScenario({replies:{key:payload}});const r=await t.call("key",{pid,key:"a",hold_ms:150});
  check(!r.isError&&JSON.stringify(r.json)===JSON.stringify(payload),name+" is not rewritten",r.text);
 }
 const nested={ok:true,job:done};t.setScenario({replies:{"job-status":nested}});const status=await t.call("job_status",{pid,id:"done-1"});
 const nestedView={...status.json.job};delete nestedView.input_read_warning;
 check(!status.isError&&typeof status.json.job.input_read_warning==='string'&&JSON.stringify(nestedView)===JSON.stringify(done),"nested job-status keeps every raw job field and adds only the advisory reader warning",status.text);
}finally{await t.close();}
console.log(`completed job ACK: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
