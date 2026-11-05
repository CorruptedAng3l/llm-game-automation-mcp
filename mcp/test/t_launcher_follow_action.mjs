import {launcherFollowAction} from '../build/launcherfollow.js';
import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const receipt={ok:true,pid,process_created:'132456789012345678',launch_state:'launcher_ready',waiting_for:'SkyrimSE.exe',input_ready:false};
let action=launcherFollowAction(receipt,true,86400);
check(action.arguments.follow_pid===pid&&action.arguments.follow_created===receipt.process_created&&action.arguments.wait_exe==='SkyrimSE.exe','launcher handoff preserves exact PID, creation stamp and intended game');
check(action.arguments.claim&&action.arguments.claim_ttl_s===86400&&!('exe'in action.arguments)&&!('steam_appid'in action.arguments),'handoff continues claim intent without another launch source');
for(const change of [{ok:false},{launch_state:'game'},{process_created:'0'},{process_created:'18446744073709551616'},{waiting_for:'../SkyrimSE.exe'},{pid:1.5}])check(!launcherFollowAction({...receipt,...change},false),'invalid or non-launcher receipt has no follow action '+JSON.stringify(change));
const t=await startFake({replies:{launch:receipt}});
try{const r=await t.call('launch',{steam_appid:'489830',wait_exe:'SkyrimSE.exe',claim:true,claim_ttl_s:86400});check(r.json.next_action?.arguments.follow_created===receipt.process_created&&t.callsOf('launch').length===1,'MCP Steam launcher reply exposes one exact follow action without issuing another launch');}finally{await t.close();}
console.log(`launcher follow action: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
