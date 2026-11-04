// Fake-only transport/metadata checks; native semantics have a separate owned XInput fixture.
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const {state,check}=makeChecker(),pid=process.pid;
const arm={ok:true,pad_readiness_version:1,arm_accepted:true,pad_armed:true,poll_observed:true,neutral_return_observed:true,superseded:false,application_ready:null,device_arrival_announced:false};
const t=await startFake({replies:{status:fullStatus(),json:arm}});
try{
 let r=await t.call("attach",{pid});
 check(!r.isError&&t.callsOf("json").length===0,"default attach does not arm or change pad state");
 t.clearCalls();r=await t.call("attach",{pid,arm_pad:true,pad_wait_ms:250});
 const call=t.callsOf("json")[0]??[],words=cmdOf(call),payload=JSON.parse(words[1]??"{}");
 check(!r.isError&&payload.cmd==="pad-arm"&&payload.wait_ms===250&&call.includes("mcp-test"),"opt-in sends one owned neutral-arm request",r.text);
 check(t.callsOf("pad").length===0&&t.callsOf("json").length===1,"no button/stick/release/cancel/rumble recipe is substituted");
 check(r.json?.pad_arm?.poll_observed===true&&r.json?.pad_arm?.application_ready===null&&r.json?.pad_arm?.device_arrival_announced===false,"actual bounded arm observation is preserved without game/hotplug claims",r.text);
 t.clearCalls();r=await t.call("attach",{pid,pad_wait_ms:250});
 check(r.isError&&t.calls().length===0,"wait without opt-in refuses before injection");
 t.setScenario({replies:{status:fullStatus(),json:{...arm,poll_observed:false,neutral_return_observed:null}}});
 t.clearCalls();r=await t.call("attach",{pid,arm_pad:true,pad_wait_ms:0});
 check(!r.isError&&r.json?.pad_arm?.poll_observed===false&&r.json?.pad_arm?.neutral_return_observed===null,"no poll remains explicitly unobserved, no fallback input");
 t.setScenario({replies:{json:{ok:false,error:"pad-arm refused: queued pad job"}}});
 t.clearCalls();r=await t.call("attach",{pid,arm_pad:true});
 check(r.isError&&r.json?.injected===true&&t.callsOf("json").length===1&&t.callsOf("pad").length===0,"busy arm refusal is returned without replacement/retry",r.text);
 t.setScenario({replies:{json:{ok:true}}});
 t.clearCalls();r=await t.call("attach",{pid,arm_pad:true});
 check(r.isError&&t.callsOf("pad").length===0,"old/malformed success cannot become a controller readiness claim");
 t.setScenario({replies:{json:{...arm,poll_observed:"true"}}});
 t.clearCalls();r=await t.call("attach",{pid,arm_pad:true});
 check(r.isError&&t.callsOf("pad").length===0,"string poll witness is refused without fallback");
 t.setScenario({replies:{json:{...arm,pad_armed:false,superseded:true}}});
 t.clearCalls();r=await t.call("attach",{pid,arm_pad:true});
 check(r.isError&&t.callsOf("pad").length===0,"superseded arm cannot claim observed readiness");
 t.setScenario({replies:{json:{...arm,poll_observed:false,neutral_return_observed:null,pad_armed:true,superseded:true}}});
 t.clearCalls();r=await t.call("attach",{pid,arm_pad:true});
 check(r.isError&&t.callsOf("pad").length===0,"non-polling ACK still requires armed and superseded to be inverse");
}finally{await t.close();}
console.log(`pad readiness: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
