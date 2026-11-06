import {topmostView,configView,compactStatus} from '../build/status.js';
import {makeChecker,startFake,fullStatus,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const topmost={version:1,policy:'strip_background',reason:'demotion_observed',topmost_stripped:true,current_topmost:false,hwnd:'0x12',thread:12,adoption_generation:'1',binding_lifetime:'3',demotion_calls:'1',demotions_observed:'1',reapplications_observed:'0',reassertion_requests:'0',reassertions_prevented:'0',refusals:'0'};
const status=fullStatus({hidden_window_version:1,window_adopted:true,window_hidden:false,presentation_window:{version:1,hidden_candidate_count:0,ambiguous:false,capacity_unconfirmed:false,evidence_unconfirmed:false,candidates_truncated:false,topmost}});
check(topmostView(status)?.scope_confirmed===true&&configView(status).topmost==='strip','typed observed demotion retains its adopted scope and effective strip policy');
for(const patch of [{demotions_observed:'0'},{current_topmost:true},{binding_lifetime:'0'},{demotion_calls:'18446744073709551616'},{thread:'12'},{version:2}])check(!topmostView({...status,presentation_window:{topmost:{...topmost,...patch}}}),'malformed or contradictory topmost receipt cannot establish observed stripping');
check(configView({...status,presentation_window:{topmost:{...topmost,policy:'keep',topmost_stripped:false}}}).topmost==='keep','keep maps to the caller config without inventing restoration');
const t=await startFake({replies:{status,launch:{ok:true,pid,injected:true,input_ready:true},config:{...status,ok:true}}});
try{
 let r=await t.call('status',{pid});check(r.json?.presentation_window?.topmost?.scope_confirmed===true,'compact MCP status retains measured topmost evidence');
 t.clearCalls();r=await t.call('launch',{exe:'E:/fixture/game.exe'});check(!r.isError&&r.json.topmost?.demotions_observed==='1'&&r.json.topmost?.scope_confirmed===true,'launch summary preserves measured status demotion');
 check(t.callsOf('status').length===1&&t.callsOf('json').length===0,'launch summary uses the existing status read with no extra setter');
 t.clearCalls();r=await t.call('config',{pid,topmost:'keep'});check(cmdOf(t.callsOf('config')[0]).includes('topmost=keep'),'explicit keep reaches native config');
 t.setScenario({replies:{launch:{ok:true,pid,injected:true,input_ready:false}}});t.clearCalls();r=await t.call('launch',{exe:'E:/fixture/game.exe'});check(r.json.topmost===undefined&&t.callsOf('status').length===0,'pending input queue neither blocks on full status nor invents demotion');
}finally{await t.close();}
console.log(`topmost MCP: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
