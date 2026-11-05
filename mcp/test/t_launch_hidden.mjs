import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const receipt={ok:true,pid,injected:true,window_adopted:true,window_hidden:true,show_hidden_requested:true,show_hidden_visibility_confirmed:false,show_hidden_reply:{ok:true,show_hidden_version:1,hidden_show:{ticket:'1',pending:true,completed:false,visible_after:false}}};
const t=await startFake({replies:{launch:receipt,status:{ok:true,pid,frames:1}}});
try{
 let r=await t.call('launch',{exe:'E:/fixture/game.exe',show_hidden:true});const argv=cmdOf(t.callsOf('launch')[0]);
 check(!r.isError&&argv.includes('--show-hidden'),'explicit launch option reaches native CLI');
 check(r.json.window_hidden===true&&r.json.show_hidden_visibility_confirmed===false&&r.json.show_hidden_reply.hidden_show.pending===true&&t.callsOf('json').length===0,'pending native visibility remains unconfirmed without MCP retry or fallback');
 t.clearCalls();r=await t.call('launch',{exe:'E:/fixture/game.exe'});check(!cmdOf(t.callsOf('launch')[0]).includes('--show-hidden'),'default launch requests no visibility change');
 t.setScenario({replies:{launch:{ok:false,pid,injected:true,ready:false,window_hidden:true,window_adopted:false,inject:{ok:true,pid,injected:true}}}});t.clearCalls();r=await t.call('launch',{exe:'E:/fixture/game.exe',claim:true});check(r.isError&&r.json.keep_pid===true&&r.json.window_hidden===true&&r.json.window_adopted===false&&t.callsOf('claim').length===0,'unadopted hidden load retains PID and makes no claim or launch retry');
}finally{await t.close();}
console.log(`launch hidden: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
