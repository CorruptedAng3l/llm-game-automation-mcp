import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const snap=(hash='a',patch={})=>({ok:true,hash,text:'ready',capture_space_updated:false,capture_scope:scope,timing_clock:'QueryPerformanceCounter',capture_qpc_ticks:'1000',qpc_frequency:'1000',...patch});
const initial={ok:true,accepted:true,job_id:`${pid}:987654321:1`,job_kind:'sequence',job_state:'queued',job_complete:false,owner:'mcp-test',requested_ms:5000,input_injected:null,transitions:0};
const scene=over=>({replies:{status:{ok:true,pid,key_sequence_version:1,sequence_timing_version:1,sequence_guard_version:1,input_scope_version:1},jobs:{ok:true,jobs:[]},seq:initial},observations:[snap(),snap('b',{capture_qpc_ticks:'1010'})],...over});
const t=await startFake(scene(),{VINPUT_EXE:fileURLToPath(new URL('./fake_sequence_conditions.mjs',import.meta.url))});
const base={pid,steps:[{action:'key',key:'space',ms:5000}]},set=s=>{t.setScenario(s);t.clearCalls();};
const nativeCalls=()=>t.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])),heartbeats=()=>nativeCalls().filter(x=>x.cmd==='sequence-observe');
try{
 let r=await t.call('key_sequence',{...base,start_on:{kind:'frame_change',timeout_ms:1000,poll_ms:20}});
 check(!r.isError&&r.json.sequence_start?.observed_capture_qpc_ticks==='1010'&&r.json.anchor_qpc_ticks==='1010'&&r.json.sequence_start?.game_event_onset_measured===false,'start event uses observed capture QPC as checked native anchor',r.text);
 const seq=cmdOf(t.callsOf('seq')[0]??[]);
 check(seq.includes('--start-qpc')&&seq.includes('--start-frequency')&&seq.includes('--scope-json')&&seq.includes('--max-lateness-ms')&&seq[seq.indexOf('--max-lateness-ms')+1]==='1000'&&t.callsOf('seq').length===1,'one native event-anchored admission carries exact scope and explicit default lateness bound');
 check(nativeCalls().filter(x=>x.cmd==='frame-hash').every(x=>x.observation_only===true),'event captures are observation-only and preserve inactivity lease');
 set(scene({observations:[snap()]}));r=await t.call('key_sequence',{...base,start_on:{kind:'frame_change',timeout_ms:100,poll_ms:20}});
 check(r.isError&&r.json.input_injected===false&&!t.callsOf('seq').length,'bounded start timeout never sends a sequence',r.text);
 set(scene({observations:[snap(),snap('b',{capture_scope:{...scope,geometry_epoch:'1'}})]}));r=await t.call('key_sequence',{...base,start_on:{kind:'frame_change',timeout_ms:1000,poll_ms:20}});
 check(r.isError&&!t.callsOf('seq').length,'native scope changes during start observation refuse all input',r.text);
 set(scene({observations:[snap('a',{text:' READY\n TO GO '})]}));r=await t.call('key_sequence',{...base,start_on:{kind:'text',text:'ready to go',exact:true}});
 check(!r.isError&&t.callsOf('ocr').length===1&&cmdOf(t.callsOf('ocr')[0]).includes('--observation-only=true'),'normalized visible text triggers bounded scoped OCR admission',r.text);
 set(scene({observations:[snap('a',{text:'failed'})]}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'text',text:'failed'}});
 check(r.isError&&!t.callsOf('seq').length&&r.json.input_injected===false,'already-visible abort text refuses admission',r.text);
 for(const options of [{abort_on:{kind:'frame_change'}},{progress_check:{max_unchanged_ms:100}},{start_on:{kind:'region_change'}},{start_on:{kind:'text',text:'   '}}]){
  set(scene());r=await t.call('key_sequence',{...base,...options});check(r.isError&&!t.callsOf('seq').length,'invalid or detached condition refuses input: '+JSON.stringify(options),r.text);
 }
 set(scene());r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'region_change',region:{x:1,y:2,w:20,h:30},poll_ms:20}});
 check(r.isError&&r.json.sequence_guard?.outcome==='aborted_region_change'&&r.json.job_complete===true&&r.json.sequence_wait?.job_may_still_be_active===false,'scoped change aborts only the exact admitted sequence',r.text);
 check(heartbeats().length===2&&heartbeats()[0].id===initial.job_id&&heartbeats()[0].observation_seq===1&&heartbeats()[1].observation_seq===2&&heartbeats()[1].abort===true&&heartbeats()[1].reason==='region_change'&&!t.callsOf('job-cancel').length&&t.callsOf('seq').length===1,'serial guard acknowledgement and one exact abort have no replay or ambiguous cancellation retry');
 set(scene({lost_guard_ack_at:2}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});
 check(r.isError&&r.json.sequence_guard?.outcome==='observer_unavailable'&&r.json.sequence_guard?.native_guard_confirmed===true&&r.json.sequence_wait.job_may_still_be_active===true&&heartbeats().length===2&&!t.callsOf('job-cancel').length,'lost abort ACK remains uncertain and relies on confirmed native expiry without retry',r.text);
 set(scene({initial_patch:{observer_guarded:false}}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});
 check(r.isError&&r.json.sequence_guard?.native_guard_confirmed===false&&r.json.sequence_guard?.heartbeat_ms===null&&!heartbeats().length,'invalid initial guard ACK never invents a cleanup guarantee',r.text);
 set(scene({initial_patch:{anchor_qpc_ticks:'999'}}));r=await t.call('key_sequence',{...base,start_on:{kind:'text',text:'ready'}});
 check(r.isError&&r.json.sequence_wait?.outcome==='event_anchor_ack_unavailable'&&t.callsOf('seq').length===1&&!heartbeats().length,'mismatched event-anchor ACK does not replay input',r.text);
 set(scene({job_patch:{owner:'other'}}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});
 check(r.isError&&r.json.sequence_guard?.outcome==='observer_unavailable'&&heartbeats().length===1,'wrong-owner heartbeat cannot establish renewal or terminal completion',r.text);
 set(scene({observations:[snap(),{ok:false,error:'capture unavailable'}]}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});
 check(r.isError&&r.json.sequence_guard?.outcome==='aborted_observer_error'&&heartbeats().at(-1)?.reason==='observer_error','post-admission capture failure requests one exact abort',r.text);
 set(scene({observations:[snap()]}));r=await t.call('key_sequence',{...base,wait:true,progress_check:{max_unchanged_ms:100},wait_timeout_ms:2000});
 check(r.isError&&r.json.sequence_guard?.outcome==='aborted_progress_stalled'&&r.json.sequence_guard.game_progress_measured===false,'unchanged selected region stops a blind sequence without inventing gameplay progress',r.text);
 set(scene({guard_states:['completed']}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});
 check(!r.isError&&r.json.sequence_wait?.completed===true&&heartbeats().length===1,'native completion wins before further condition capture',r.text);
 set(scene({abort_completed:true}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});
 check(!r.isError&&r.json.job_state==='completed','natural completion racing an abort is reported as native completion',r.text);
 for(const field of ['sequence_guard_version','sequence_timing_version','input_scope_version']){const s=scene();delete s.replies.status[field];set(s);r=await t.call('key_sequence',{...base,start_on:{kind:'text',text:'ready'}});check(r.isError&&r.json.input_injected===false&&!t.callsOf('seq').length,`missing ${field} refuses conditional admission before input`,r.text);}
 for(const patch of [{timing_clock:'GetTickCount'},{capture_space_updated:true},{qpc_frequency:1000}]){set(scene({observations:[snap('a',patch)]}));r=await t.call('key_sequence',{...base,start_on:{kind:'text',text:'ready'}});check(r.isError&&!t.callsOf('seq').length,'invalid scope/clock observation refuses conditional input: '+JSON.stringify(patch),r.text);}
 set(scene({observations:[snap(),snap('b',{capture_qpc_ticks:'999'})]}));r=await t.call('key_sequence',{...base,start_on:{kind:'frame_change',poll_ms:20}});check(r.isError&&!t.callsOf('seq').length,'regressed observation QPC cannot become an event anchor',r.text);
 set(scene());r=await t.call('key_sequence',{...base,wait:true,wait_timeout_ms:1,abort_on:{kind:'frame_change'}});check(r.isError&&heartbeats().at(-1)?.reason==='observer_error'&&t.callsOf('seq').length===1,'guarded observer budget exhaustion requests one exact abort',r.text);
 set(scene({guard_reply_patch:{guard_renewed:false}}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});check(r.isError&&r.json.sequence_guard?.outcome==='observer_unavailable'&&heartbeats().length===1,'live heartbeat without typed renewal cannot establish guard liveness',r.text);
 set(scene({guard_states:['completed'],job_patch:{guard_observation_seq:'1'}}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});check(r.isError&&r.json.sequence_guard?.outcome==='observer_unavailable'&&heartbeats().length===1,'terminal heartbeat must retain typed native serial provenance',r.text);
 set(scene({initial_patch:{guard_observation_seq:'0'}}));r=await t.call('key_sequence',{...base,wait:true,abort_on:{kind:'frame_change'}});check(r.isError&&r.json.sequence_guard?.native_guard_confirmed===false&&!heartbeats().length,'initial guard acknowledgement requires exact numeric initial serial',r.text);
}finally{await t.close();}
console.log(`sequence conditions: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
