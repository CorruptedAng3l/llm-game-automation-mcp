import {keyScheduleLines} from '../build/keyschedule.js';
import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker();
let built=keyScheduleLines({at_ms:[50,455,860],key:'space',hold_ms:20});
check(built.lines?.join('\n')==='at 50 key space 20\nat 455 key space 20\nat 860 key space 20','schedule retains absolute offsets including initial delay');
for(const schedule of [{at_ms:[0,10],key:'space',hold_ms:20},{at_ms:[1,1],key:'space'},{at_ms:[118000],key:'space'},{at_ms:[0],key:'w space'},{at_ms:[],key:'space'}])check(!!keyScheduleLines(schedule).error,'invalid or overlapping complete schedule refused');
const pid=process.pid,t=await startFake({replies:{seq:{ok:true,accepted:true,job_id:`${pid}:100:1`}}});
try{
 let r=await t.call('key_sequence',{pid,schedule:{at_ms:[50,455,860],key:'space',hold_ms:20}});
 check(!r.isError&&t.lastSeqText()==='at 50 key space 20\nat 455 key space 20\nat 860 key space 20\n','MCP sends one absolute native script without host per-tap sleeps');
 t.clearCalls();r=await t.call('key_sequence',{pid,steps:[{action:'wait',ms:100}]});
 check(!r.isError&&t.lastSeqText()==='wait 100\n','wait-only sequence passes through with no invented key');
 for(const args of [{},{steps:[{action:'wait',ms:1}],schedule:{at_ms:[0],key:'space'}},{schedule:{at_ms:[0,10],key:'space',hold_ms:20}}]){t.clearCalls();r=await t.call('key_sequence',{pid,...args});check(r.isError&&t.calls().length===0,'invalid sequence form refuses before native I/O');}
}finally{await t.close();}
console.log(`key schedule: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
