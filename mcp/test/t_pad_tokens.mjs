// Actual MCP validation, including an old/fake native endpoint that accepts any token.
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const t=await startFake({replies:{pad:{ok:true},json:{ok:true},'hold-while':{ok:true}}});
try{
 const invalid=[
  ['pad',{buttons:'lb+rx',capture_after:false}],
  ['pad',{buttons:'a+bogus',capture_after:false}],
  ['pad_ramp',{axis:'rx',from:0,to:1000,ms:100,buttons:'rb+ly',capture_after:false}],
  ['pad_timeline',{states:[{at_ms:0,buttons:'a'},{at_ms:100,buttons:'lb+rx'}],ms:150,capture_after:false}],
  ['hold_while',{pad_buttons:'rx',until_text:'HUD',timeout_ms:100}],
  ['pad_timeline',{states:[{at_ms:0,buttons:'off'},{at_ms:100}],ms:150,capture_after:false}],
 ];
 for(const [name,args] of invalid){
  t.clearCalls();const r=await t.call(name,{pid,...args});
  check(r.isError&&/supported:.*a b x y/.test(r.text)&&t.calls().length===0,`${name} rejects invalid button tokens before any CLI or partial state`,r.text);
 }
 t.clearCalls();let r=await t.call('pad',{pid,buttons:'a+LT, rt rb',capture_after:false});
 check(!r.isError&&t.callsOf('pad').length===1&&cmdOf(t.callsOf('pad')[0])[1]==='a+LT, rt rb','native-compatible aliases/case/separators are preserved exactly');
 t.clearCalls();r=await t.call('pad',{pid,buttons:'lb',rx:28000,ms:150,capture_after:false});
 check(!r.isError&&t.callsOf('pad').length===1&&t.callsOf('pad')[0].includes('--rx'),'right-stick X stays a separate axis field');
 t.clearCalls();r=await t.call('pad',{pid,buttons:'off',capture_after:false});
 check(!r.isError&&cmdOf(t.callsOf('pad')[0])[1]==='off','explicit direct-pad off cleanup remains available');
}finally{await t.close()}
console.log(`pad tokens: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
