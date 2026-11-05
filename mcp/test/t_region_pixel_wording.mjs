import {measureTimedInput} from '../build/holdmotion.js';
import {presentInputReads} from '../build/inputreads.js';
import {compactReply} from '../build/compactreply.js';
import {makeChecker} from './lib.mjs';
const {state,check}=makeChecker(),scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'},region={x:1,y:2,w:20,h:30};
const native={ok:true,accepted:false,job_id:'123:456:7',job_kind:'keys',job_state:'completed',job_complete:true,input_injected:true,transitions:2,input_read_observation:{version:1,started:true,terminal:true,frozen:true,kind:'keys',application_response:null,applicable:true,measured:true,keys_requested:1,keys_observed:0,all_requested_observed:false,outcome:'not_observed',pad_returns:'0',keys:[{vk:9,measured:true,poll_reads:'0',down_returns:'0',message_dispatches:'1'}]}};
const result=j=>({ok:j.ok!==false,json:j,argv:[],stderr:'',timedOut:false,exitCode:j.ok===false?1:0});
const snap=rgb=>result({ok:true,capture_scope:scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',capture_qpc_ticks:'1000',qpc_frequency:'1000',motion_sample_version:1,motion_sample_w:1,motion_sample_h:1,motion_sample_rgb:rgb});
async function run(observations){let reads=0,inputs=0;const r=await measureTimedInput([],region,()=>0,async()=>{inputs++;return result(native);},async()=>observations[Math.min(reads++,observations.length-1)]);return {r,inputs};}
let m=await run([snap('000000'),snap('000000')]),j=presentInputReads(m.r).json;
check(m.r.ok&&m.inputs===1&&j.input_motion.measurement==='region_pixels_only'&&j.input_motion.pixel_change_observed===false&&j.input_motion.input_consumption_measured===false,'unchanged selected pixels do not change successful native delivery into an unread verdict');
check(j.input_motion.note.includes('does not establish')&&j.input_read_observation===native.input_read_observation&&j.window_messages_dispatched===true&&j.reader_return_observed===false,'exact native polling evidence and separately dispatched window messages stay distinct');
let short=compactReply({content:[{type:'text',text:JSON.stringify(j)}],isError:false},true,'key');let compact=JSON.parse(short.content[0].text);
check(compact.input_motion.pixel_change_observed===false&&compact.input_motion.measurement==='region_pixels_only'&&compact.input_read.measurement==='polling_returns_only'&&compact.application_response===null,'compact labels region pixels and polling returns without a game-acceptance claim');
m=await run([snap('000000'),snap('ffffff')]);check(m.r.ok&&m.r.json.input_motion.pixel_change_observed===true&&m.r.json.input_motion.application_response===null,'changed pixels remain only changed pixels');
m=await run([snap('000000'),result({ok:false,error:'after capture unavailable'})]);check(m.r.ok&&m.r.json.input_motion.pixel_change_observed===null&&!m.r.json.input_motion.measured&&m.inputs===1,'unavailable pixels stay null and retain the one completed input');
console.log(`region pixel wording: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
