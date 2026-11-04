// MCP-layer fake CLI only. Native OCR/map behavior has a separate owned fixture.
import { makeChecker,startFake } from "./lib.mjs";
import { fileURLToPath } from "node:url";
const {state,check}=makeChecker(),pid=process.pid;
// Private native status has canonical fullscreen diagnostics but no fs_refused alias.
const status={ok:true,frames:0,frame_api:"none",fullscreen:{refused:0,failures:0,blocked_before_first_present:false},counters:{present:0,present_ours:0}};
const t=await startFake({png:{w:640,h:360,core_capture:true},replies:{status,capture:{ok:true,method:"printwindow",capture_path:"printwindow",frame:0}}},{VINPUT_EXE:fileURLToPath(new URL("./fake_present_zero.mjs",import.meta.url))});
try{
 const r=await t.call("click_text",{pid,text:"Play",capture_after:true});
 const caps=t.callsOf("capture"),last=caps.at(-1)??[];
 check(!r.isError&&r.json?.ok===true&&!!r.image&&caps.length===2,"boolean look recovers zero-Present PrintWindow image without retrying action");
 check(t.callsOf("click-text").length===1,"the action itself is sent exactly once");
 check(last[last.indexOf("--after-frames")+1]==="0"&&last[last.indexOf("--method")+1]==="printwindow","recovery is a fresh explicit PrintWindow request without a Present wait");
 check(r.json?.capture_after?.fallback?.present_wait_satisfied===false,"recovery explicitly denies a rendered-frame guarantee");
 t.setScenario({png:{w:640,h:360,core_capture:true},replies:{status:{...status,counters:{...status.counters,fs_refused:0}},capture:{ok:true,method:"printwindow",capture_path:"printwindow",frame:0}}});t.clearCalls();
 const publicShape=await t.call("click_text",{pid,text:"Play",capture_after:true});
 check(!!publicShape.image&&t.callsOf("capture").length===2,"public optional alias and canonical diagnostics also permit recovery");
 await t.call("capture",{pid,scale:0.5});t.clearCalls();const strict=await t.call("click_text",{pid,text:"Play",capture_after:3});
 check(!strict.isError&&t.callsOf("capture").length===1&&/Present calls/.test(strict.json?.capture_after_error??"")&&!strict.image,"numeric Present request stays strict while retaining successful action");
 await t.call("move",{pid,x:40,y:30,coord_space:"capture",edge_margin:0});const move=t.callsOf("mouse").at(-1)??[],i=move.indexOf("mouse");
 check(move[i+1]==="80"&&move[i+2]==="60","failed capture leaves last displayed-image mapping unchanged");
 t.setScenario({png:{w:640,h:360,core_capture:true},replies:{status,capture:{ok:false,error:"PW readback denied"}}});t.clearCalls();
 const retryFailure=await t.call("key",{pid,key:"a",capture_after:true});
 check(t.callsOf("capture").length===2&&retryFailure.json?.capture_after_error==="PW readback denied"&&retryFailure.json?.capture_after_fallback?.present_wait_satisfied===false,"failed PrintWindow retry keeps successful action and explicit fallback error");
 await t.call("move",{pid,x:40,y:30,coord_space:"capture",edge_margin:0});const still=t.callsOf("mouse").at(-1)??[],j=still.indexOf("mouse");
 check(still[j+1]==="80"&&still[j+2]==="60","failed retry also preserves the prior displayed-image mapping");
 t.clearCalls();t.setScenario({replies:{status:{...status,counters:{...status.counters,fs_refused:1}},capture:{ok:true,method:"printwindow",w:1,h:1}}});
 const blocked=await t.call("key",{pid,key:"a",capture_after:true});
 check(t.callsOf("capture").length===1&&!blocked.image,"a fullscreen-refusal loop is not bypassed by PrintWindow recovery");
 for(const [name,change] of [
  ["missing canonical diagnostics",{fullscreen:undefined}],
  ["mistyped refused counter",{fullscreen:{...status.fullscreen,refused:"0"}}],
  ["canonical refusal",{fullscreen:{...status.fullscreen,refused:1}}],
  ["fullscreen failure",{fullscreen:{...status.fullscreen,failures:1}}],
  ["blocked native fullscreen",{fullscreen:{...status.fullscreen,blocked_before_first_present:true}}],
  ["mistyped optional alias",{counters:{...status.counters,fs_refused:"0"}}],
  ["blocked D3D9",{d3d9:{blocked_before_first_present:true}}],
 ]){
  t.setScenario({replies:{status:{...status,...change},capture:{ok:true,method:"printwindow",w:1,h:1}}});t.clearCalls();
  const refusal=await t.call("key",{pid,key:"a",capture_after:true});
  check(t.callsOf("capture").length===1&&!refusal.image,`${name}: no fallback bypass`);
 }
}finally{await t.close();}
console.log(state.failures?`capture_after_printwindow: FAIL (${state.failures})`:`capture_after_printwindow: PASS (${state.passes})`);
process.exit(state.failures?1:0);
