import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const scene=patch=>({replies:{status:{ok:true,pid,key_sequence_version:1},jobs:{ok:true,jobs:[]}},...patch});
const t=await startFake(scene(),{VINPUT_EXE:fileURLToPath(new URL('./fake_keydrive.mjs',import.meta.url)),VINPUT_MCP_COMPACT:'false'});
const requests=()=>t.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])),drive=()=>requests().filter(q=>q.cmd==='key-drive');
const args={pid,phases:[{ms:600,throttle:true,steer:-.25},{ms:200,brake:true}],bindings:{throttle:'w+shift'},period_ms:200,capture_after:false};
try{
 const tools=(await t.client.listTools()).tools,key=tools.find(x=>x.name==='key_drive'),seq=tools.find(x=>x.name==='key_sequence');
 check(key?.inputSchema.properties.phases.maxItems===128&&key.inputSchema.properties.period_ms.maximum===1000,'public protocol lists bounded keyboard drive timeline');
 check(seq?.inputSchema.properties.steps.items.properties.action.enum.includes('pwm')&&seq.inputSchema.properties.steps.items.properties.duty.maximum===1,'public sequence schema lists fractional PWM duty');
 let r=await t.call('key_drive',args),q=drive()[0];
 check(!r.isError&&r.json.ok&&r.json.job_kind==='key_drive'&&r.json.owner==='mcp-test'&&r.json.capture_scope_checked&&r.json.key_drive.vehicle_motion_measured===false,'actual MCP stdio preserves native drive identity and digital-intent scope');
 check(drive().length===1&&requests()[0].cmd==='ping'&&requests()[0].input_job_support&&q.script==='drive 600 1 -0.25 0\ndrive 200 0 0 1\n'&&q.throttle_key==='w+shift'&&!q.replace&&!q.async,'actual protocol emits one finite guarded native command after tiny capability ping');
 check(!t.callsOf('capture').length,'explicit capture_after false produces no post-action image');
 t.clearCalls();r=await t.call('key_drive',{...args,async:true});
 check(!r.isError&&r.json.accepted&&r.json.next_action?.tool==='job_status'&&r.json.next_action.arguments.job_id===r.json.job_id&&drive().length===1,'async protocol gives exact-job status continuation without replay');
 for(const a of [{...args,phases:[]},{...args,phases:[{ms:100,steer:.01}],period_ms:20},{...args,phases:[{ms:60000},{ms:10}]}]){t.clearCalls();r=await t.call('key_drive',a);check(r.isError&&drive().length===0,'invalid bounded drive refuses before native admission');}
 t.setScenario(scene({capability_patch:{key_drive_version:0}}));t.clearCalls();r=await t.call('key_drive',args);check(r.isError&&r.json.input_injected===false&&drive().length===0,'old native drive capability refuses before input');
 t.setScenario(scene({native_patch:{owner:'other'}}));t.clearCalls();r=await t.call('key_drive',args);check(r.isError&&r.json.do_not_repeat&&r.json.input_may_have_been_applied&&drive().length===1&&!requests().some(q=>q.cmd==='release'||q.cmd==='job-cancel'),'unowned malformed ACK never triggers replay or arbitrary cleanup');
 t.setScenario(scene());t.clearCalls();r=await t.call('key_sequence',{pid,steps:[{action:'pwm',key:'a',ms:800,duty:.25,period_ms:200}],capture_after:false});
 check(!r.isError&&t.callsOf('seq').length===1&&t.lastSeqText()==='pwm a 800 0.25 200\n','public sequence PWM reaches CLI as one exact finite fractional-duty line');
 for(const step of [{action:'wait',ms:10,duty:.5},{action:'pwm',key:'a',ms:800,duty:30,period_ms:200},{action:'pwm',key:'a',ms:1,duty:.5,period_ms:200},...['lbutton','VK_RBUTTON','vk1','VK_0x02','w+vk00006'].map(key=>({action:'pwm',key,ms:800,duty:.5,period_ms:200}))]){t.clearCalls();r=await t.call('key_sequence',{pid,steps:[step],capture_after:false});check(r.isError&&!t.callsOf('seq').length,'detached, mouse or invalid PWM step options cannot schedule a sequence');}
}finally{await t.close();}
console.log(`key drive protocol: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
