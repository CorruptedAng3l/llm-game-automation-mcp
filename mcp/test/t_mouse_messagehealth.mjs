import { mouseMessageHealth, compactStatus } from '../build/status.js';
import {presentMouseDiagnostics} from '../build/actiondiagnostics.js';
import { fullStatus, makeChecker, startFake } from './lib.mjs';
const {check,state}=makeChecker();
const high=fullStatus({}, {wm_mouse:2408,wm_mouse_dropped:2091,wm_mouse_owned:300,wm_mouse_owned_delivered:300,wm_mouse_owned_dropped:0,wm_mouse_unrecognized_dropped:2091});
let h=mouseMessageHealth(high);
check(h.owned_delivered===300&&h.owned_dropped===0&&/2091 of 2408/.test(h.warning),'high aggregate drop ratio retains successful owned delivery');
check(/unrecognized|Unrecognized/.test(h.warning)&&/does not identify/.test(h.warning),'warning does not attribute every suppressed message to our input or JavaFX');
check(compactStatus(high).warnings.some(x=>x.includes('Mouse message isolation')),'default compact status surfaces high mouse drop ratio');
check(!mouseMessageHealth(fullStatus({}, {wm_mouse:10,wm_mouse_dropped:9})).warning,'small sample does not raise ratio warning');
const startup=fullStatus({}, {wm_mouse:15,wm_mouse_dropped:15,wm_mouse_owned:0,wm_mouse_owned_delivered:0,wm_mouse_owned_dropped:0,wm_mouse_unrecognized_dropped:15});
const startupHealth=mouseMessageHealth(startup);
check(!startupHealth.warning&&/without vinput-owned provenance/.test(startupHealth.note)&&/startup/.test(startupHealth.note)&&/does not prove/.test(startupHealth.note),'startup drops explain unrecognized provenance even below the ratio-warning threshold');
check(compactStatus(startup).mouse_messages.note===startupHealth.note,'default status includes startup counter explanation');
check(!mouseMessageHealth(fullStatus({}, {wm_mouse:1,wm_mouse_dropped:0,wm_mouse_owned:1,wm_mouse_owned_delivered:1,wm_mouse_owned_dropped:0,wm_mouse_unrecognized_dropped:0})).note,'no unrecognized drops need no provenance note');
check(!mouseMessageHealth(fullStatus({}, {wm_mouse:100,wm_mouse_dropped:10})).warning,'low drop ratio remains quiet');
const chromium=fullStatus({mouse_message_unrecognized_types:{WM_MOUSEMOVE:64,WM_LBUTTONDOWN:7}},{wm_mouse:261,wm_mouse_dropped:71,wm_mouse_owned:190,wm_mouse_owned_delivered:190,wm_mouse_owned_dropped:0,wm_mouse_unrecognized_dropped:71});
check(/27%/.test(mouseMessageHealth(chromium).warning)&&mouseMessageHealth(chromium).unrecognized_types.WM_LBUTTONDOWN===7,'27-percent PR4 sample warns and names observed unrecognized message types');
check(!mouseMessageHealth({...chromium,mouse_message_unrecognized_types:{WM_MOUSEMOVE:72}}).unrecognized_types,'incoherent histogram cannot invent per-type attribution');
check(mouseMessageHealth(fullStatus({}, {wm_mouse:100,wm_mouse_dropped:80})).owned_dropped===null,'older DLL source accounting is explicitly unknown');
check(mouseMessageHealth(fullStatus({}, {wm_mouse:5,wm_mouse_dropped:8}))===undefined,'incoherent counter snapshot does not invent a ratio');
check(mouseMessageHealth(fullStatus({}, {wm_mouse:'100',wm_mouse_dropped:80}))===undefined,'malformed counter type is not coerced');
for(const patch of [{wm_mouse_owned:60,wm_mouse_owned_delivered:60,wm_mouse_owned_dropped:0,wm_mouse_unrecognized_dropped:0},{wm_mouse_owned:50,wm_mouse_owned_delivered:0,wm_mouse_owned_dropped:50,wm_mouse_unrecognized_dropped:1},{wm_mouse_owned:0,wm_mouse_owned_delivered:0,wm_mouse_owned_dropped:0,wm_mouse_unrecognized_dropped:1000}])check(mouseMessageHealth(fullStatus({}, {wm_mouse:100,wm_mouse_dropped:50,...patch})).owned_received===null,'incoherent subgroup accounting stays unknown');
const t=await startFake({replies:{status:high}});
try{const r=await t.call('status',{pid:process.pid});check(!r.isError&&r.json?.mouse_messages?.owned_delivered===300&&r.json?.warnings?.some(x=>x.includes('2091 of 2408')),'MCP status returns owned accounting and warning without input calls');check(t.callsOf('keydown').length===0&&t.callsOf('mouse').length===0,'diagnosis is read-only');}finally{await t.close();}
const receipt={ok:true,mouse_message_health_version:1,mouse_message_counters:high.counters};
const d=presentMouseDiagnostics({ok:true,json:receipt,argv:[],exitCode:0,stderr:'',timedOut:false});check(d.ok&&d.json.mouse_message_warning?.includes('2091 of 2408')&&d.json.mouse_messages.owned_delivered===300,'action adapter adds qualified cumulative warning without changing delivery success');
const u=await startFake({replies:{click:receipt}});try{const r=await u.call('click',{pid:process.pid,x:20,y:20,edge_margin:0});check(!r.isError&&r.json.mouse_message_warning?.includes('2091 of 2408')&&u.callsOf('click').length===1&&u.callsOf('status').length===0,'actual action reply warns with one input and no extra status polling');}finally{await u.close();}
console.log(`mouse message health: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
