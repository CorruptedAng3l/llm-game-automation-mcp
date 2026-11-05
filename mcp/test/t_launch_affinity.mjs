import { makeChecker, startFake, fullStatus } from './lib.mjs';
const {check,state}=makeChecker();const t=await startFake({replies:{launch:{ok:true,pid:process.pid,injected:true,cpu_affinity:'0xF'},status:fullStatus()}});
try {
  let r=await t.call('launch',{exe:'C:/fixture/game.exe',cpu_affinity:'0xF',early:true});
  const a=t.callsOf('launch')[0]??[];
  check(!r.isError&&a.includes('--cpu-affinity')&&a.includes('0xF')&&a.includes('--early'),'explicit affinity forwards unchanged with early injection');
  check(r.json?.cpu_affinity==='0xF','native affinity receipt is retained');
  for(const mask of ['0x0','0x000','F','0xG']){t.clearCalls();r=await t.call('launch',{exe:'C:/fixture/game.exe',cpu_affinity:mask});check(r.isError&&t.calls().length===0,'invalid/zero affinity refuses before native launch');}
  t.clearCalls();r=await t.call('launch',{steam_appid:'10150',wait_exe:'prototypef.exe',cpu_affinity:'0xF'});
  check(r.isError&&t.calls().length===0,'indirect launcher affinity refused before startup');
}finally{await t.close();}
console.log(`launch affinity MCP: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
