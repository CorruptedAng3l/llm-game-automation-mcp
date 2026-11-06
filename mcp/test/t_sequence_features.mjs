import {fileURLToPath} from 'node:url';
import {existsSync,readdirSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const snap=(rgb='ff0000',patch={})=>({ok:true,hash:'a'.repeat(16),capture_scope:scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',qpc_frequency:'1000',capture_qpc_ticks:'1000',motion_sample_version:1,motion_sample_w:1,motion_sample_h:1,motion_sample_rgb:rgb,...patch});
const initial={ok:true,accepted:true,job_id:`${pid}:987654321:1`,job_kind:'sequence',job_state:'queued',job_complete:false,owner:'mcp-test',requested_ms:5000,input_injected:null,transitions:0,sequence_steps:2};
const scene=extra=>({replies:{status:{ok:true,pid,key_sequence_version:1,sequence_timing_version:1,sequence_guard_version:1,input_scope_version:1,capture_output_version:1,capture_scope_version:1},jobs:{ok:true,jobs:[]},seq:initial},observations:[snap()],guard_states:['running'],...extra});
const t=await startFake(scene(),{VINPUT_EXE:fileURLToPath(new URL('./fake_sequence_conditions.mjs',import.meta.url)),VINPUT_MCP_COMPACT:'false'});
const base={pid,steps:[{action:'key',key:'enter',ms:100},{action:'hold',key:'w',ms:4900}],wait:true},colour={kind:'colour',region:{x:0,y:0,w:10,h:10},colour:[255,0,0],tolerance:0,min_match_pct:100,poll_ms:20};
const set=s=>{t.setScenario(s);t.clearCalls();},requests=()=>t.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])),beats=()=>requests().filter(q=>q.cmd==='sequence-observe');
const witness=(good,name,r)=>{if(!good)console.error('SEQUENCE_FEATURE_DIAGNOSTIC '+JSON.stringify({reply:r,calls:t.timedCalls()}));check(good,name);};
try{
 const schema=(await t.client.listTools()).tools.find(v=>v.name==='key_sequence').inputSchema,p=schema.properties;
 const dereference=value=>{for(let n=0;n<4&&value?.$ref?.startsWith('#/');n++)value=value.$ref.slice(2).split('/').map(k=>k.replace(/~1/g,'/').replace(/~0/g,'~')).reduce((o,k)=>o?.[k],schema);return value;};
 const afterStep=dereference(p.abort_on?.properties?.after_step),kind=dereference(p.abort_on?.properties?.kind);
 check(p.capture_mode?.enum?.join()==='first,last'&&afterStep?.minimum===1&&afterStep?.maximum===1024&&kind?.enum?.includes('colour'),'sequence schemas expose bounded last-N, exact after-step arming and colour');
 set(scene());let r=await t.call('key_sequence',{...base,abort_on:colour});
 check(r.isError&&r.json.input_injected===false&&!t.callsOf('seq').length,'already-matching colour refuses admission without an explicit later arming policy');
 set(scene({guard_steps:[0,0,1,1]}));r=await t.call('key_sequence',{...base,abort_on:{...colour,after_step:1}});
 witness(r.isError&&r.json.sequence_guard?.outcome==='aborted_colour'&&r.json.sequence_guard?.after_step===1&&r.json.sequence_guard.step_index_before_observation===1&&r.json.sequence_guard.matched_observation.colour_match_pct===100&&t.callsOf('seq').length===1,'colour already visible permits one dismiss step and arms only at typed native step1',r);
 check(requests().filter(q=>q.cmd==='frame-hash').length===2&&beats().filter(q=>q.abort).length===1&&beats().at(-1).reason==='colour','no colour sample is used before the step gate; one exact abort uses native colour reason');
 set(scene({guard_steps:['1']}));r=await t.call('key_sequence',{...base,abort_on:{...colour,after_step:1}});
 witness(r.isError&&r.json.sequence_guard?.outcome==='aborted_observer_error'&&beats().at(-1)?.reason==='observer_error'&&requests().filter(q=>q.cmd==='frame-hash').length===1,'untyped step witness aborts once instead of guessing that dismiss completed',r);
 set(scene({colour_capability:{sequence_abort_colour_version:0}}));r=await t.call('key_sequence',{...base,abort_on:{...colour,after_step:1}});
 check(r.isError&&r.json.input_injected===false&&!t.callsOf('seq').length,'old colour-reason capability refuses before any input');
 set(scene({observations:[snap('0000ff'),snap('ff00000000ff',{motion_sample_w:2,motion_sample_h:1})],guard_states:['running','completed']}));r=await t.call('key_sequence',{...base,abort_on:{...colour,min_match_pct:51}});
 witness(!r.isError&&r.json.sequence_wait?.completed===true&&!beats().some(q=>q.abort),'50percent coarse colour match cannot satisfy an explicit51percent threshold',r);
 for(const args of [{...base,start_on:{...colour,after_step:1}},{...base,abort_on:{...colour,region:undefined}},{...base,abort_on:{...colour,colour:[256,0,0]}},{...base,abort_on:{kind:'text',text:'x',colour:[1,2,3]}}]){set(scene());r=await t.call('key_sequence',args);check(r.isError&&!t.callsOf('seq').length,'invalid/detached colour or step options refuse before scheduling');}
}finally{await t.close();}
const record=await startFake(scene({guard_states:[...Array(14).fill('running'),'completed']}),{VINPUT_EXE:fileURLToPath(new URL('./fake_sequence_recording.mjs',import.meta.url)),VINPUT_MCP_COMPACT:'false'});
try{
 const directory=join(record.dir,'last');const r=await record.call('key_sequence',{...base,capture_every_ms:250,capture_mode:'last',capture_max_frames:2,capture_out_dir:directory}),v=r.json.sequence_captures;
 if(r.isError||v?.evicted_frames<1)console.error('SEQUENCE_RING_PROTOCOL_DIAGNOSTIC '+JSON.stringify({reply:r,calls:record.timedCalls()}));
 check(!r.isError&&v.mode==='last'&&v.total_recorded_frames>2&&v.recorded_frames===2&&v.evicted_frames===v.total_recorded_frames-2&&v.frames[0].index===v.total_recorded_frames-2,'actual MCP keeps last two frames from the same original job instead of stopping after first two');
 check(v?.frames.every(f=>f.bracket_confirmed&&f.job_id===initial.job_id&&existsSync(f.path)&&f.mapping.scale>0)&&existsSync(v.contact_sheet.path)&&readdirSync(dirname(v.frames[0].path)).length===3&&record.callsOf('seq').length===1,'ring protocol retains exact saved frame/step/map proof, one sheet and one admission');
}finally{await record.close();}
console.log(`sequence features: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
