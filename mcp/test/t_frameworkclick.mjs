import {frameworkClickTiming} from '../build/frameworkclick.js';
import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {check,state}=makeChecker();
let p=frameworkClickTiming(undefined,undefined,true);
check(p.hold===140&&p.hover===80&&p.argv.join()==='--framework_default_hold=true,--framework_default_hover=true','omitted values keep fallback defaults and request fresh native framework selection');
p=frameworkClickTiming(2000,0,true);check(p.hold===2000&&p.hover===0&&!p.argv.length,'explicit long hold and zero hover remain exact with no framework override');
p=frameworkClickTiming(500,undefined,true);check(p.argv.join()==='--framework_default_hover=true','only omitted hover requests native default');
p=frameworkClickTiming(undefined,350,true);check(p.argv.join()==='--framework_default_hold=true','only omitted hold requests native default');
p=frameworkClickTiming(undefined,undefined,false);check(p.argv.join()==='--framework_default_hold=true','unpositioned click never requests a hover delay');
check(frameworkClickTiming(undefined,undefined,true).budgetHold+frameworkClickTiming(undefined,undefined,true).budgetHover===750,'omitted batch timing reserves full possible Chromium400/350 duration');
check(frameworkClickTiming(2000,0,true).budgetHold===2000&&frameworkClickTiming(2000,0,true).budgetHover===0,'explicit batch values keep their exact timing budget');
const t=await startFake({}),pid=process.pid;
try{
 let r=await t.call('click',{pid,x:100,y:120,edge_margin:0});let c=t.callsOf('click')[0];
 check(!r.isError&&t.callsOf('click').length===1&&c.includes('--framework_default_hold=true')&&c.includes('--framework_default_hover=true'),'actual MCP click delegates omitted timing to current native framework once');
 t.clearCalls();r=await t.call('click',{pid,x:100,y:120,hold_ms:2000,hover_ms:0,edge_margin:0});c=t.callsOf('click')[0];
 check(!r.isError&&t.callsOf('click').length===1&&!c.some(v=>v.includes('framework_default'))&&c[c.indexOf('--ms')+1]==='2000'&&c[c.indexOf('--settle-ms')+1]==='0','actual MCP preserves explicit caller timing and performs no verification retry');
 t.clearCalls();r=await t.call('click',{pid,hold_ms:400});c=t.callsOf('click')[0];check(!r.isError&&!c.some(v=>v.includes('framework_default'))&&!c.includes('--settle-ms'),'current-cursor click does not invent hover or override its explicit hold');
}finally{await t.close();}
const batch=await startFake({}, {VINPUT_EXE:fileURLToPath(new URL('./fake_agent_loops.mjs',import.meta.url))});
const requests=()=>batch.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])).filter(q=>q.cmd==='click');
try{
 let r=await batch.call('click_many',{pid,points:[[100,120]],timeout_ms:10000});let q=requests()[0];
 check(!r.isError&&requests().length===1&&q.framework_default_hold===true&&q.framework_default_hover===true&&q.ms===140&&q.settle_ms===80,'actual MCP batch sends native omitted-value flags once with backward-compatible fallback values');
 batch.clearCalls();r=await batch.call('click_many',{pid,points:[[100,120]],timeout_ms:10000,hold_ms:500,hover_ms:0});q=requests()[0];
 check(!r.isError&&requests().length===1&&q.framework_default_hold===undefined&&q.framework_default_hover===undefined&&q.ms===500&&q.settle_ms===0,'actual MCP batch preserves explicit500/0 timing within its existing bounds without framework overrides or replay');
 batch.clearCalls();r=await batch.call('click_many',{pid,points:[[100,120]],timeout_ms:4000});
 check(r.isError&&requests().length===0&&r.json.input_session.stop_reason==='insufficient_budget_for_next_bounded_click','batch refuses before input when full possible framework timing cannot fit remaining whole-point budget');
}finally{await batch.close();}
console.log(`framework click: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
