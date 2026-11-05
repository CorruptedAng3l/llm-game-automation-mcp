import { mouseMessageHealth, compactStatus } from '../build/status.js';
import { fullStatus, makeChecker, startFake } from './lib.mjs';
const {check,state}=makeChecker();
const high=fullStatus({}, {wm_mouse:2408,wm_mouse_dropped:2091,wm_mouse_owned:300,wm_mouse_owned_delivered:300,wm_mouse_owned_dropped:0,wm_mouse_unrecognized_dropped:2091});
let h=mouseMessageHealth(high);
check(h.owned_delivered===300&&h.owned_dropped===0&&/2091 of 2408/.test(h.warning),'high aggregate drop ratio retains successful owned delivery');
check(/unrecognized|Unrecognized/.test(h.warning)&&/does not identify/.test(h.warning),'warning does not attribute every suppressed message to our input or JavaFX');
check(compactStatus(high).warnings.some(x=>x.includes('Mouse message isolation')),'default compact status surfaces high mouse drop ratio');
check(!mouseMessageHealth(fullStatus({}, {wm_mouse:10,wm_mouse_dropped:9})).warning,'small sample does not raise ratio warning');
check(!mouseMessageHealth(fullStatus({}, {wm_mouse:100,wm_mouse_dropped:10})).warning,'low drop ratio remains quiet');
check(mouseMessageHealth(fullStatus({}, {wm_mouse:100,wm_mouse_dropped:80})).owned_dropped===null,'older DLL source accounting is explicitly unknown');
check(mouseMessageHealth(fullStatus({}, {wm_mouse:5,wm_mouse_dropped:8}))===undefined,'incoherent counter snapshot does not invent a ratio');
check(mouseMessageHealth(fullStatus({}, {wm_mouse:'100',wm_mouse_dropped:80}))===undefined,'malformed counter type is not coerced');
const t=await startFake({replies:{status:high}});
try{const r=await t.call('status',{pid:process.pid});check(!r.isError&&r.json?.mouse_messages?.owned_delivered===300&&r.json?.warnings?.some(x=>x.includes('2091 of 2408')),'MCP status returns owned accounting and warning without input calls');check(t.callsOf('keydown').length===0&&t.callsOf('mouse').length===0,'diagnosis is read-only');}finally{await t.close();}
console.log(`mouse message health: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
