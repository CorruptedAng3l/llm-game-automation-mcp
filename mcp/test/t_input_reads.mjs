import {presentInputReads} from '../build/inputreads.js';
import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const key=(vk=87,high='0',reads=high,messages='1')=>({vk,measured:true,poll_reads:reads,down_returns:high,message_dispatches:messages});
const observation=(extra={})=>({version:1,started:true,terminal:true,frozen:true,kind:'keys',application_response:null,window_ms:80,keys:[key()],pad_returns:'0',measured:true,applicable:true,all_requested_observed:false,keys_observed:0,keys_requested:1,outcome:'not_observed',...extra});
const native=(o=observation(),extra={})=>({ok:true,accepted:false,job_id:`${pid}:123:4`,owner:'mcp-test',job_kind:'keys',job_state:'completed',job_complete:true,input_injected:true,transitions:2,input_read_observation:o,...extra});
const result=j=>({ok:true,json:j,argv:['key','w'],stderr:'',timedOut:false,exitCode:0});
let original=native(),r=presentInputReads(result(original));
check(!r.ok&&r.json.ok===false&&r.json.scheduler_ok===true&&r.json.input_receipt===original,'unread completed input returns a distinct error while retaining exact original scheduler receipt');
check(r.json.input_injected===true&&r.json.reader_return_observed===false&&r.json.error.includes('do not replay automatically'),'negative reader result never claims that input was not sent or suggests replay');
original=native(observation({keys:[key(87,'0','0','999')]}));r=presentInputReads(result(original));check(!r.ok,'many window-message dispatches alone do not establish a polling high-state return');
original=native(observation({keys:[key(87,'2','5')],keys_observed:1,all_requested_observed:true,outcome:'observed'}));r=presentInputReads(result(original));check(r.ok&&r.json.reader_return_observed===true&&r.json.application_response===null,'positive polling evidence remains qualified without a gameplay-success claim');
original=native(observation({keys:[key(87,'1'),key(16)],keys_observed:1,keys_requested:2,outcome:'partial'}));r=presentInputReads(result(original));check(!r.ok&&r.json.input_receipt===original&&r.json.error.includes('some requested keys'),'partial chord observation cannot qualify the whole chord');
original=native(observation({kind:'pad',keys:[],keys_requested:0,pad_returns:'0'}),{job_kind:'pad'});r=presentInputReads(result(original));check(!r.ok,'zero job-owned pad API returns is explicit rather than an unqualified success');
original=native(observation({kind:'pad',keys:[],keys_requested:0,pad_returns:'18446744073709551615',all_requested_observed:true,outcome:'observed'}),{job_kind:'pad'});r=presentInputReads(result(original));check(r.ok&&r.json.reader_return_observed===true,'exact uint64 pad return evidence avoids Number truncation');
for(const [name,change] of [ ['untyped terminal',{terminal:1}],['missing freeze',{frozen:undefined}],['contradictory count',{keys:[key(87,'2','1')]}],['duplicate key',{keys:[key(),key()],keys_requested:2}],['overflow count',{keys:[key(87,'18446744073709551616')]}],['unavailable native snapshot',{measured:false,outcome:'unavailable'}],['unknown version',{version:2}] ]){
 r=presentInputReads(result(native(observation(change))));check(r.ok&&typeof r.json.input_read_warning==='string'&&r.json.reader_return_observed!==true,`${name} reports unavailable evidence without inventing absence or a positive reader`);
}
original=native(undefined);delete original.input_read_observation;r=presentInputReads(result(original));check(r.ok&&r.json.input_read_warning,'older DLL completion carries an explicit reader-evidence warning');
original=native(observation({terminal:false,started:false,frozen:false,outcome:'pending'}),{accepted:true,job_state:'queued',job_complete:false,input_injected:null});r=presentInputReads(result(original));check(r.ok&&r.json===original,'asynchronous admission is not turned into a premature reader verdict');
original=native(observation({kind:'keys',keys:[],keys_requested:0,applicable:false,measured:false,outcome:'not_applicable'}),{input_injected:false});r=presentInputReads(result(original));check(r.ok&&r.json===original,'wait-only plan is not required to emit or observe input');
const failed={...result(native()),ok:false};check(presentInputReads(failed)===failed,'an existing transport/scheduler failure is preserved');
const t=await startFake({replies:{key:native()}});
try{
 r=await t.call('key',{pid,key:'w'});check(r.isError&&r.json.input_receipt?.job_id===`${pid}:123:4`&&t.callsOf('key').length===1,'actual MCP propagates unread result with one original action and exact receipt');
 t.setScenario({replies:{key:native(observation({keys:[key(87,'1')],keys_observed:1,all_requested_observed:true,outcome:'observed'}))}});t.clearCalls();
 r=await t.call('key',{pid,key:'w'});check(!r.isError&&r.json.reader_return_observed===true&&t.callsOf('key').length===1,'actual MCP accepts measured reader return without additional input or CLI polling');
}finally{await t.close();}
console.log(`input reads: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
