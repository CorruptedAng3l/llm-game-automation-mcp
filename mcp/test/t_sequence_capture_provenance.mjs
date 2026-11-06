import {fileURLToPath} from 'node:url';
import {makeChecker,startFake} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'owned capture scope'};
const initial={ok:true,accepted:true,job_id:`${pid}:987654321:1`,job_kind:'sequence',job_state:'queued',job_complete:false,owner:'mcp-test',requested_ms:150,input_injected:null,transitions:0};
const completed={...initial,ok:true,accepted:false,job_state:'completed',job_complete:true,finished_qpc_ticks:'2000',qpc_frequency:'1000',finished_capture_scope:scope};
const frame={ok:true,method:'swapchain',capture_path:'d3d11',capture_scope:scope,timing_clock:'QueryPerformanceCounter',qpc_frequency:'1000',capture_qpc_ticks:'2100',frame_qpc_ticks:'2050'};
const scene=patch=>({png:{w:80,h:60,core_capture:true},replies:{status:{ok:true,pid,key_sequence_version:1},jobs:{ok:true,jobs:[]},seq:initial,capture:{...frame,...patch}},sequence_observations:[{ok:true,job:completed}]});
const t=await startFake(scene(),{VINPUT_EXE:fileURLToPath(new URL('./fake_sequence_wait.mjs',import.meta.url)),VINPUT_MCP_COMPACT:'false'});
const args={pid,steps:[{action:'hold',key:'w',ms:150}],wait:true,capture_after:true,max_width:80};
try{
 for(const [label,patch,accepted] of [['fresh',{},true],['request older',{capture_qpc_ticks:'1999'},false],['frame older',{frame_qpc_ticks:'1999'},false],['impossible frame',{frame_qpc_ticks:'2200'},false],['wrong clock',{qpc_frequency:'1001'},false],['wrong scope',{capture_scope:{...scope,geometry_epoch:'1'}},false],['PrintWindow',{method:'printwindow',frame_qpc_ticks:'100'},true]]){
  t.setScenario(scene(patch));t.clearCalls();const r=await t.call('key_sequence',args),p=r.json.sequence_capture;
  const good=!r.isError&&r.json.ok===true&&r.json.sequence_wait?.completed===true&&JSON.stringify(r.json.sequence_input)===JSON.stringify(initial)&&t.callsOf('seq').length===1&&!!r.image===accepted&&p?.ok===accepted;
  if(!good)console.error('SEQUENCE_CAPTURE_DIAGNOSTIC '+JSON.stringify({label,reply:r,calls:t.timedCalls()}));check(good,'actual MCP '+label+' image witness preserves completed input and never replays');
  if(label==='PrintWindow')check(p?.frame_qpc_ticks===null&&p?.rendered_frame_after_job_end===null&&p?.capture_after_job_end===true,'PrintWindow proves request after end while rendered freshness stays unknown');
 }
}finally{await t.close();}
console.log(`sequence capture provenance: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
