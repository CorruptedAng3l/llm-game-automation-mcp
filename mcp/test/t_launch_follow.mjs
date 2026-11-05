import { makeChecker, startFake, fullStatus } from './lib.mjs';
const {check,state}=makeChecker();
const pid=process.pid,stamp='134383241453920903';
const t=await startFake({replies:{launch:{ok:true,pid,injected:true,followed_from:123,process_created:stamp},status:fullStatus()}});
try {
  let r=await t.call('launch',{follow_pid:123,follow_created:stamp,wait_exe:'SkyrimSE.exe',claim:true,claim_ttl_s:7200,timeout_s:5});
  const a=t.callsOf('launch')[0]??[];
  check(!r.isError&&a.includes('--follow-pid')&&a.includes('123')&&a.includes('--follow-created')&&a.includes(stamp),'continuation preserves exact creation stamp string',r.text);
  check(!a.includes('--exe')&&!a.includes('--steam')&&t.callsOf('launch').length===1,'continuation never requests another executable launch');
  check(t.callsOf('claim').some(c=>c.includes('7200')),'final child gets requested claim TTL');
  check(r.json?.followed_from===123,'follow provenance survives MCP reply');
  t.clearCalls();r=await t.call('launch',{exe:'x.exe',follow_pid:123});
  check(r.isError&&t.calls().length===0,'multiple launch sources refused before native call');
  t.clearCalls();r=await t.call('launch',{exe:'x.exe',follow_created:stamp});
  check(r.isError&&t.calls().length===0,'orphan continuation stamp refused');
  const list=await t.client.listTools();const launch=list.tools.find(x=>x.name==='launch');
  check(launch.description.includes('optional args')&&launch.description.includes('launcher_ready')&&launch.description.includes('5:0000065434'),'launch arguments, launcher continuation and Steam fallback discoverable');
} finally {await t.close();}
console.log(`launch follow MCP: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
