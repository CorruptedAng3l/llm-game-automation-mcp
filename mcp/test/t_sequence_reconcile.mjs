import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const snap={ok:true,hash:'a'.repeat(16),capture_space_updated:false,capture_scope:scope,timing_clock:'QueryPerformanceCounter',capture_qpc_ticks:'1000',qpc_frequency:'1000'};
const initial={ok:true,accepted:true,job_id:`${pid}:987654321:1`,job_kind:'sequence',job_state:'queued',job_complete:false,owner:'mcp-test',requested_ms:5000,input_injected:null,transitions:0,capture_scope:scope,sequence_observer_reconcile_version:1,observer_guard_clock:'QueryInterruptTime',observer_guard_sample_ms:'8900',observer_guard_remaining_ms:4000};
const scene=extra=>({replies:{status:{ok:true,pid,key_sequence_version:1,sequence_timing_version:1,sequence_guard_version:1,input_scope_version:1},jobs:{ok:true,jobs:[]},seq:initial},observations:[snap],guard_states:['running','completed'],lost_guard_ack_at:1,reconciliation:{},...extra});
const t=await startFake(scene(),{VINPUT_EXE:fileURLToPath(new URL('./fake_sequence_conditions.mjs',import.meta.url)),VINPUT_MCP_COMPACT:'false'});
const base={pid,steps:[{action:'hold',key:'w',ms:3000},{action:'wait',ms:2000}],wait:true,abort_on:{kind:'frame_change',poll_ms:20}};
const set=s=>{t.setScenario(s);t.clearCalls();},beats=()=>t.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])).filter(x=>x.cmd==='sequence-observe');
const witness=(good,name,r)=>{if(!good)console.error('RECONCILE_DIAGNOSTIC '+JSON.stringify({reply:r,calls:t.timedCalls()}));check(good,name);};
try{
 let r=await t.call('key_sequence',base),d=r.json.sequence_guard?.observer_diagnostics;
 witness(!r.isError&&r.json.job_state==='completed'&&d.failures===1&&d.entries[0].reconciled===true&&d.entries[0].receipt.error==='modeled acknowledgement loss'&&t.callsOf('job-status').length===1,'one lost regular ACK reconciles only through exact read-only current serial/scope/lease proof',r);
 check(t.callsOf('seq').length===1&&beats().map(x=>x.observation_seq).join()==='1,2'&&!beats().some(x=>x.abort)&&!t.callsOf('job-cancel').length,'reconciliation continues with the next serial and never replays input or the uncertain control');
 set(scene({reconciliation:{state:'completed'}}));r=await t.call('key_sequence',base);
 witness(!r.isError&&r.json.job_complete&&beats().length===1&&t.callsOf('job-status').length===1,'exact terminal reconciliation finishes without reviving or renewing the job',r);
 for(const patch of [{owner:'other'},{job_id:`${pid}:987654321:2`},{job_kind:'keys'},{capture_scope:{...scope,geometry_epoch:'1'}},{guard_observation_seq:0},{guard_observation_seq:'1'},{observer_guard_remaining_ms:0},{observer_guard_remaining_ms:100},{observer_guard_clock:'GetTickCount'},{observer_guard_sample_ms:9},{sequence_observer_reconcile_version:0},{observer_guard_ms:5000}]){
  set(scene({reconciliation:{job_patch:patch}}));r=await t.call('key_sequence',base);
  witness(r.isError&&r.json.sequence_guard.outcome==='observer_unavailable'&&beats().length===1&&t.callsOf('job-status').length===1&&!r.json.sequence_guard.observer_diagnostics.entries[0].reconciled,'unproved read-only reconciliation stops without more control: '+JSON.stringify(patch),r);
 }
 set(scene({reconciliation:{job_patch:{observer_guard_remaining_ms:2700},reply_patch:{_delay_ms:300}}}));r=await t.call('key_sequence',base);
 witness(r.isError&&r.json.sequence_guard.observer_diagnostics.entries[0].reconciliation.predicates.lease_current===false&&beats().length===1,'full read-only round-trip is subtracted before reserving observation/poll/renewal work',r);
 set(scene({initial_patch:{sequence_observer_reconcile_version:0}}));r=await t.call('key_sequence',base);
 check(r.isError&&!t.callsOf('job-status').length&&beats().length===1,'old/unconfirmed capability never attempts reconciliation');
 set(scene({lost_guard_ack_at:undefined,guard_reply_patch:{guard_ack:false}}));r=await t.call('key_sequence',base);
 check(r.isError&&!t.callsOf('job-status').length&&beats().length===1,'typed ACK mismatch is not transport loss and cannot trigger reconciliation');
 set(scene({lost_guard_ack_at:2,observations:[snap,{...snap,hash:'b'.repeat(16),capture_qpc_ticks:'1010'}]}));r=await t.call('key_sequence',base);
 witness(r.isError&&!t.callsOf('job-status').length&&beats().length===2&&beats()[1].abort,'uncertain abort ACK remains untouched and relies on finite native expiry',r);
 set(scene({lost_guard_ack_at:[1,2,3,4,5],guard_states:['running']}));r=await t.call('key_sequence',base);d=r.json.sequence_guard.observer_diagnostics;
 witness(r.isError&&beats().length===5&&t.callsOf('job-status').length===4&&d.failures===5&&d.retained===4&&d.dropped===1,'lifetime reconciliation and diagnostic storage caps stop a repeatedly failing transport',r);
 set(scene({lost_guard_reply:{custom:'x'.repeat(10000)},reconciliation:{state:'completed'}}));r=await t.call('key_sequence',base);d=r.json.sequence_guard.observer_diagnostics;
 witness(!r.isError&&d.entries[0].receipt.truncated===true&&d.entries[0].receipt.sha256.length===64&&Buffer.byteLength(d.entries[0].receipt.json_prefix)<=4096,'oversized failed receipt is explicitly byte-capped and hashed, never silently complete',r);
 set(scene({observations:[{ok:false,error:'pre-admission observation failed',custom:'retain preflight source'}]}));r=await t.call('key_sequence',base);
 witness(r.isError&&r.json.input_injected===false&&!t.callsOf('seq').length&&r.json.sequence_observation_failure?.receipt.custom==='retain preflight source'&&r.json.sequence_observation_failure.predicates.transport_ok===false,'pre-admission capture refusal retains raw predicate/timing evidence without sending input',r);
 set(scene({lost_guard_ack_at:undefined,guard_states:['running'],observations:[snap,{ok:false,error:'post-admission observation failed',custom:'retain observed source'}]}));r=await t.call('key_sequence',base);
 witness(r.isError&&r.json.sequence_guard.outcome==='aborted_observer_error'&&r.json.sequence_guard.observation_failure?.receipt.custom==='retain observed source'&&beats().length===2&&beats()[1].abort&&t.callsOf('seq').length===1,'post-admission observation failure retains its exact small receipt and sends only one exact abort',r);
 set(scene({lost_guard_ack_at:undefined,guard_states:['running'],guard_reply_patch:{_delay_ms:400,returned_native_tag:'retain detached ACK'}}));const pending=t.call('key_sequence',base),until=Date.now()+3000;
 while(!beats().length&&Date.now()<until)await new Promise(resolve=>setTimeout(resolve,10));await t.call('detach',{pid});r=await pending;d=r.json.sequence_guard?.observer_diagnostics;
 witness(r.isError&&d?.entries[0]?.predicates.attachment_current===false&&d.entries[0].receipt.returned_native_tag==='retain detached ACK'&&beats().length===1&&!t.callsOf('job-status').length,'actual MCP detach during heartbeat retains the returned ACK and sends no old-target follow-up',r);
}finally{await t.close();}
console.log(`sequence reconcile: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
