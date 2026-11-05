import {fileURLToPath} from 'node:url';
import {existsSync,readdirSync} from 'node:fs';
import {join} from 'node:path';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const snap=(text='GO',qpc='1000')=>({ok:true,text,hash:'a'.repeat(16),capture_space_updated:false,capture_scope:scope,timing_clock:'QueryPerformanceCounter',capture_qpc_ticks:qpc,qpc_frequency:'1000'});
const initial={ok:true,accepted:true,job_id:`${pid}:987654321:1`,job_kind:'sequence',job_state:'queued',job_complete:false,owner:'mcp-test',requested_ms:1000,input_injected:null,transitions:0};
const scene=over=>({replies:{status:{ok:true,pid,key_sequence_version:1,sequence_timing_version:1,sequence_guard_version:1,sequence_abort_image_version:1,input_scope_version:1,capture_output_version:1,capture_scope_version:1},jobs:{ok:true,jobs:[]},seq:initial},observations:[snap()],guard_states:['running','running','completed'],...over});
const t=await startFake(scene(),{VINPUT_EXE:fileURLToPath(new URL('./fake_sequence_recording.mjs',import.meta.url)),VINPUT_MCP_COMPACT:'false'});
const base={pid,steps:[{action:'hold',key:'w+shift',ms:1000}],wait:true,capture_every_ms:250,capture_max_frames:4};
const set=s=>{t.setScenario(s);t.clearCalls();};
const requests=()=>t.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])),beats=()=>requests().filter(x=>x.cmd==='sequence-observe'),captures=()=>requests().filter(x=>x.cmd==='capture');
const witness=(good,label,receipt)=>{if(!good)console.error('RECORDING_DIAGNOSTIC '+JSON.stringify({reply:receipt,timed_cli_calls:t.timedCalls()}));check(good,label);};
try{
 const properties=(await t.client.listTools()).tools.find(x=>x.name==='key_sequence').inputSchema.properties;
 check(properties.capture_every_ms.minimum===250&&properties.capture_max_frames.maximum===32&&properties.capture_max_width.maximum===960,'periodic recording schema exposes finite interval/image bounds');
 let r=await t.call('key_sequence',{...base,capture_out_dir:join(t.dir,'periodic'),compact:true});const view=r.json.sequence_captures,frame=view?.frames?.[0];
 check(!r.isError&&r.json.job_state==='completed'&&r.json.sequence_wait.completed===true&&view.recorded_frames===1,'one observed native sequence completes with one retained periodic frame');
 check(frame.job_id===initial.job_id&&frame.job_state_before==='running'&&frame.job_state_after==='completed'&&frame.bracket_confirmed&&frame.capture_qpc_ticks==='1010'&&frame.frame_index===10&&frame.capture_scope.hwnd===scope.hwnd,'actual MCP retains independent exact-job/frame QPC and step brackets in compact mode');
 check(r.image&&existsSync(frame.path)&&existsSync(view.contact_sheet.path)&&view.contact_sheet.coordinate_system==='contact_sheet_not_client_pixels'&&readdirSync(join(t.dir,'periodic')).length===2,'same saved frames produce one inline review sheet in caller directory with no duplicate default captures');
 check(t.callsOf('seq').length===1&&beats().length===3&&beats().every(b=>b.id===initial.job_id)&&!t.callsOf('capture-burst').length&&!t.callsOf('job-cancel').length,'periodic image is bracketed by exact acknowledged guard witnesses, never by a concurrent burst or replay');
 check(captures()[0].observation_only&&captures()[0].preserve_capture_space&&captures()[0].timeout_ms<=500&&cmdOf(t.callsOf('seq')[0]).includes('--guard-ms'),'recording-only sequence uses scoped native admission and native observer expiry');
 set(scene({recordings:[{ok:false,error:'model capture failure'}]}));r=await t.call('key_sequence',base);
 check(!r.isError&&r.json.job_state==='completed'&&r.json.sequence_captures.recorded_frames===0&&r.json.sequence_captures.warnings[0].includes('do not replay')&&t.callsOf('seq').length===1&&captures().length===1,'post-input recording failure preserves completed native input and disables only further images');
 set(scene({guard_states:['running','running','running','running','completed']}));r=await t.call('key_sequence',{...base,capture_max_frames:1});
 check(!r.isError&&r.json.sequence_captures.capped&&r.json.sequence_captures.frames.length===1&&captures().length===1&&beats().length===5,'frame cap stops image collection while exact job observation continues to natural completion');
 set(scene({lost_guard_ack_at:3}));r=await t.call('key_sequence',base);
 check(r.isError&&r.json.sequence_guard.outcome==='observer_unavailable'&&r.json.sequence_guard.native_guard_confirmed&&r.json.sequence_wait.job_may_still_be_active&&r.json.sequence_captures.frames[0].bracket_confirmed===false&&beats().length===3&&!t.callsOf('job-cancel').length,'lost post-capture ACK preserves unconfirmed bracket and relies on finite native expiry without control retry');
 set(scene({guard_states:['completed']}));r=await t.call('key_sequence',base);
 check(!r.isError&&r.json.sequence_captures.recorded_frames===0&&!captures().length,'native completion wins before any stale periodic frame is requested');
 for(const field of ['capture_output_version','capture_scope_version']){const s=scene();delete s.replies.status[field];set(s);r=await t.call('key_sequence',base);check(r.isError&&r.json.input_injected===false&&!t.callsOf('seq').length,`missing ${field} refuses recording before sequence admission`);}
 for(const args of [{...base,wait:false},{pid,steps:base.steps,capture_max_frames:2},{...base,capture_every_ms:249}]){set(scene());r=await t.call('key_sequence',args);check(r.isError&&!t.callsOf('seq').length,'invalid/detached recording options refuse before input');}
 set(scene({retain_frames:true,observations:[snap(),snap(),snap(),snap('MISSION FAILED','1030')],guard_states:['running']}));r=await t.call('key_sequence',{...base,abort_on:{kind:'text',text:'MISSION FAILED',poll_ms:20}});
 witness(r.isError&&r.json.sequence_guard.outcome==='aborted_text'&&r.json.sequence_guard.abort_confirmed&&r.json.sequence_captures.recorded_frames===1&&r.image&&beats().at(-1).abort===true&&t.callsOf('seq').length===1,'periodic capture shares the SAME guarded text-abort observer and retains review frames on exact abort',r);
 set(scene());r=await t.call('key_sequence',{pid,steps:base.steps,compact:true});
 check(!r.isError&&r.json.next_action.tool==='job_status'&&r.json.next_action.arguments.id===initial.job_id&&r.json.next_action.note.includes('future new sequence')&&!beats().length,'default async reply names exact continuation plus future wait:true without replaying admitted input');
 set(scene({guard_states:['running'],recordings:[{_delay_ms:300}]}));const controller=new AbortController();
 const cancelled=t.client.callTool({name:'key_sequence',arguments:{...base,wait_timeout_ms:10000}},undefined,{signal:controller.signal,timeout:20000}).catch(e=>e);
 const until=Date.now()+3000;while(!captures().length&&Date.now()<until)await new Promise(resolve=>setTimeout(resolve,10));controller.abort();const cancellationReceipt=await cancelled;
 await new Promise(resolve=>setTimeout(resolve,400));const count=beats().length;await new Promise(resolve=>setTimeout(resolve,300));
 witness(captures().length===1&&beats().length===count&&!t.callsOf('job-cancel').length&&t.callsOf('seq').length===1,'MCP cancellation stops observer renewals and leaves exact native expiry without replay or ambiguous cancellation',cancellationReceipt);
}finally{await t.close();}
console.log(`sequence captures protocol: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
