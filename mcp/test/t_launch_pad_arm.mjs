// Fake SDK contract only: no native executable, DLL, game or input.
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const {state,check}=makeChecker(),pid=process.pid;
const launch={ok:true,pid,injected:true,foreground_restored:true,launch_marker:"keep-original"};
const arm={ok:true,pad_readiness_version:1,arm_accepted:true,pad_armed:true,poll_observed:true,neutral_return_observed:true,superseded:false,application_ready:null,device_arrival_announced:false};
const base={exe:"C:/synthetic/Game.exe",timeout_s:1,settle_s:0};
const t=await startFake();
function scenario(replies={}){t.setScenario({replies:{launch,status:fullStatus(),claim:{ok:true},json:arm,...replies}});t.clearCalls();}
const words=(name)=>cmdOf(t.callsOf(name)[0]??[]);
const flag=(name,key)=>{const w=words(name),i=w.indexOf(key);return i>=0?w[i+1]:undefined;};
const armPayload=()=>{try{return JSON.parse(words("json")[1]??"{}");}catch{return {};}};
async function call(name,args){try{return await t.call(name,args);}catch(e){return {isError:true,json:null,text:String(e)};}}
try{
 const tools=(await t.client.listTools()).tools;
 check(["launch","attach","claim"].every(name=>tools.find(x=>x.name===name)?.inputSchema?.properties?.claim_ttl_s),"all requested tools expose claim_ttl_s");
 scenario();let r=await call("launch",base);
 check(!r.isError&&r.json?.pid===pid&&t.callsOf("json").length===0&&r.json?.pad_arm_attempted===undefined,"default launch stays unarmed");
 scenario();r=await call("launch",{...base,claim:true,claim_ttl_s:9000,arm_pad:true,pad_wait_ms:125});
 check(!r.isError&&r.json?.pid===pid&&r.json?.launch_marker==="keep-original"&&r.json?.injected===true,"optional arm preserves original successful launch fields",r.text);
 check(flag("launch","--claim-ttl")==="9000"&&flag("claim","--ttl")==="9000","initial native claim and matching-owner renewal use explicit seconds");
 check(armPayload().cmd==="pad-arm"&&armPayload().wait_ms===125&&t.callsOf("json").length===1&&t.callsOf("json")[0].includes("mcp-test"),"one owned neutral-arm request uses existing protocol");
 check(r.json?.pad_arm_acknowledged===true&&r.json?.pad_arm?.poll_observed===true&&r.json?.pad_arm?.application_ready===null&&r.json?.pad_arm?.device_arrival_announced===false,"typed API return evidence is retained without glyph/game claim");
 check(t.callsOf("inject").length===0&&["pad","release","job-cancel","pad-timeline","pad-ramp"].every(name=>t.callsOf(name).length===0),"launch arm never reinjects, presses, clears, cancels or retries");
 check(t.calls().map(c=>cmdOf(c)[0]).join(",")==="launch,claim,json,status","arm occurs once after launch and claim, before diagnostic status");
 scenario();r=await call("launch",{...base,claim:true});
 check(!r.isError&&flag("launch","--claim-ttl")==="3600"&&flag("claim","--ttl")==="3600"&&t.callsOf("json").length===0,"existing default claim TTL remains3600 seconds without arming");
 for(const args of [{...base,pad_wait_ms:5},{...base,claim_ttl_s:9000},{...base,arm_pad:true,pad_wait_ms:2001},...([0,86401,1.5].map(claim_ttl_s=>({...base,claim:true,claim_ttl_s})))]){
  scenario();r=await call("launch",args);check(r.isError&&t.calls().length===0,"invalid/contradictory launch options refuse before any launch or claim",r.text);
 }
 scenario();r=await call("attach",{pid,claim_ttl_s:9000});
 check(r.isError&&t.calls().length===0,"attach TTL without claim refuses before injection");
 scenario({json:{ok:false,error:"pad-arm refused: a queued/running pad job exists"}});
 r=await call("launch",{...base,arm_pad:true});
 check(r.isError&&r.json?.ok===false&&r.json?.launch_ok===true&&r.json?.launched===true&&r.json?.pid===pid&&r.json?.launch_marker==="keep-original"&&r.json?.pad_arm_acknowledged===false&&r.json?.pad_arm?.ok===false&&typeof r.json?.pad_arm_error==="string","busy arm is a failed compound request with successful launch/PID preserved",r.text);
 check(t.callsOf("launch").length===1&&t.callsOf("json").length===1&&t.callsOf("pad").length===0,"busy arm is not retried or replaced");
 for(const json of [{ok:true},{...arm,poll_observed:"true"},{...arm,pad_armed:false,superseded:true},{...arm,poll_observed:false,neutral_return_observed:null,pad_armed:true,superseded:true}]){
  scenario({json});r=await call("launch",{...base,arm_pad:true});
  check(r.isError&&r.json?.ok===false&&r.json?.launch_ok===true&&r.json?.pid===pid&&r.json?.pad_arm_acknowledged===false&&t.callsOf("json").length===1,"malformed/incoherent arm ACK cannot claim readiness or cause relaunch",r.text);
 }
 scenario({json:{...arm,poll_observed:false,neutral_return_observed:null}});
 r=await call("launch",{...base,arm_pad:true,pad_wait_ms:0});
 check(!r.isError&&r.json?.pad_arm_acknowledged===true&&r.json?.pad_arm?.poll_observed===false&&r.json?.pad_arm?.neutral_return_observed===null&&armPayload().wait_ms===0,"valid unobserved arm stays explicitly unobserved");
 scenario({claim:{ok:false,claim_refused:true,error:"claimed by another"}});
 r=await call("launch",{...base,claim:true,arm_pad:true});
 check(r.isError&&r.json?.ok===false&&r.json?.launch_ok===true&&r.json?.pid===pid&&r.json?.claim_ok===false&&r.json?.claim?.claim_refused===true&&r.json?.pad_arm_attempted===false&&t.callsOf("json").length===0,"claim refusal preserves launched target and prevents subsequent arming",r.text);
 scenario({launch:{ok:false,pid,error:"launch failed"}});r=await call("launch",{...base,claim:true,arm_pad:true});
 check(r.isError&&t.calls().length===1&&t.callsOf("json").length===0&&t.callsOf("claim").length===0,"failed launch never causes a claim or arm");
 scenario({launch:{...launch,pid:0}});r=await call("launch",{...base,arm_pad:true});
 check(r.isError&&r.json?.ok===false&&r.json?.launch_ok===true&&r.json?.pid===0&&r.json?.pad_arm_attempted===false&&t.callsOf("json").length===0,"unusable returned PID does not authorize an arm request");
 scenario({launch:{...launch,pid:0}});r=await call("launch",{...base,claim:true});
 check(r.isError&&r.json?.ok===false&&r.json?.launch_ok===true&&r.json?.claim_ok===false&&t.callsOf("claim").length===0,"unusable PID cannot silently complete a requested claim");
 scenario({claim:{ok:false,claim_refused:true,error:"claimed by another"}});r=await call("launch",{...base,claim:true});
 check(r.isError&&r.json?.ok===false&&r.json?.launch_ok===true&&r.json?.pid===pid&&r.json?.claim?.claim_refused===true,"requested claim failure alone is a failed compound request with launch retained");
 scenario();r=await call("attach",{pid,claim:true,claim_ttl_s:9000,arm_pad:true,pad_wait_ms:50});
 check(!r.isError&&t.callsOf("inject").length===1&&flag("claim","--ttl")==="9000"&&armPayload().wait_ms===50&&r.json?.pad_arm?.poll_observed===true,"attach custom TTL and original P5 arm remain compatible",r.text);
 scenario({claim:{ok:false,claim_refused:true,error:"claimed by another"}});r=await call("attach",{pid,claim:true,arm_pad:true});
 check(r.isError&&r.json?.pid===pid&&r.json?.injected===true&&r.json?.claim?.claim_refused===true&&t.callsOf("json").length===0,"attach returns actual claim failure before optional arming",r.text);
 for(const claim_ttl_s of [1,86400,undefined]){
  scenario();r=await call("claim",{pid,...(claim_ttl_s===undefined?{}:{claim_ttl_s})});
  check(!r.isError&&t.callsOf("claim").length===1&&flag("claim","--ttl")===String(claim_ttl_s??3600)&&t.callsOf("claim")[0].includes("mcp-test")&&!t.callsOf("claim")[0].includes("--force-claim"),"explicit claim uses bounded native seconds and same owner without takeover",r.text);
 }
 scenario({claim:{ok:false,claim_refused:true,error:"claimed by another"}});r=await call("claim",{pid,claim_ttl_s:9000});
 check(r.isError&&r.json?.claim_refused===true&&t.callsOf("json").length===0&&t.callsOf("release").length===0,"explicit foreign-owner refusal stays a refusal without fallback",r.text);
}finally{await t.close();}
console.log(`launch pad/claim: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
