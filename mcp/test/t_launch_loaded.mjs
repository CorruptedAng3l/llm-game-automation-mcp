import {loadedLaunchFailure} from '../build/launchstate.js';
import {fullStatus,makeChecker,startFake} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const loaded={ok:false,pid,injected:true,ready:false,failure_stage:'readiness',error:'queue_not_progressing',inject:{ok:false,pid,injected:true,readiness:{ok:false,hook_ready:true,window_ready:true,queue_ready:false,readiness_reason:'queue_not_progressing'}}};
let view=loadedLaunchFailure(loaded,true,7200);
check(view?.launched===true&&view.keep_pid===true&&view.launch_ok===false&&view.liveness==='not_verified','loaded failure is retained without asserting liveness/readiness');
check(view?.claim_pending===true&&view.claim_ttl_s_requested===7200&&view.claim_confirmation==='not_observed','requested claim TTL is retained as pending confirmation');
check(loadedLaunchFailure(loaded,true)?.claim_ttl_s_requested===3600,'default requested TTL is explicit');
check(!('claim_pending' in loadedLaunchFailure(loaded,false)),'unrequested claim is not invented');
for(const bad of [null,{}, {...loaded,pid:String(pid)},{...loaded,pid:1.5},{...loaded,pid:4294967296},{...loaded,injected:'true'},{...loaded,inject:null},{...loaded,inject:{...loaded.inject,pid:pid+1}},{...loaded,inject:{...loaded.inject,injected:false}},{...loaded,inject:{...loaded.inject,injected:'true'}},{...loaded,target_exited:true},{...loaded,target_exited:'false'},{...loaded,inject:{...loaded.inject,target_exited:true}},{...loaded,ready:'false'},{...loaded,launched:false},{ok:false,error:'CreateProcess failed',inject:null}])
 check(loadedLaunchFailure(bad,true,7200)===undefined,'absent, malformed, mismatched, pre-load or exited receipts cannot establish a usable PID');
const t=await startFake({replies:{launch:loaded,status:fullStatus()}});
try{
 const r=await t.call('launch',{exe:'C:/fixture/game.exe',claim:true,claim_ttl_s:7200,arm_pad:true});
 check(r.isError&&r.json?.ok===false&&r.json?.launched===true&&r.json?.keep_pid===true&&r.json?.ready===false,'actual MCP preserves readiness failure and adds keep-PID diagnostics');
 check(r.json?.claim_pending===true&&r.json?.claim_ttl_s_requested===7200,'actual MCP retains requested TTL after an unready loaded launch');
 check(t.callsOf('claim').length===0&&t.callsOf('pad').length===0&&t.callsOf('json').length===0,'failure diagnostics neither claim nor arm/inject any fallback input');
 t.setScenario({replies:{launch:{ok:false,error:'CreateProcess failed 740',inject:null}}});
 const before=await t.call('launch',{exe:'C:/fixture/game.exe',claim:true});
 check(before.isError&&before.json?.launched===undefined&&before.json?.keep_pid===undefined,'failed-before-load MCP launch never claims a launched game');
}finally{await t.close();}
console.log(`LAUNCH_LOADED_RESULT ${JSON.stringify({completed:true,passed:state.passes,failed:state.failures,scope:'pure receipt classification plus actual MCP against a fake CLI; no native process liveness/readiness claim'})}`);
process.exit(state.failures?1:0);
