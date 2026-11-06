import {readFileSync} from 'node:fs';
const {projectInputReadObservation:project,qualifyInputReceiptV2:qualify}=await import(process.env.RECEIPT_PROJECTION_MODULE??'../build/inputreceiptprojection.js');
let passed=0,failed=0;const check=(ok,name)=>{console.log(`${ok?'PASS':'FAIL'} ${name}`);ok?passed++:failed++;};
const names=['GetAsyncKeyState','GetKeyState','GetKeyboardState','DirectInputState','DirectInputBuffer','GetRawInputData','GetRawInputBuffer'];
const channel=(reads='0',down='0')=>({measured:true,reads,down_returns:down});
const key=(vk=87,high='0',messages='1')=>({vk,measured:true,polling_measured:true,messages_measured:true,poll_reads:high,down_returns:high,message_dispatches:messages,
 channels:Object.fromEntries(names.map(name=>[name,channel(name==='GetAsyncKeyState'?high:'0',name==='GetAsyncKeyState'?high:'0')])),observed_channels:[...(BigInt(high)>0n?['GetAsyncKeyState']:[]),...(BigInt(messages)>0n?['WindowMessageDispatch']:[])]});
const message=()=>({version:2,started:true,terminal:true,frozen:true,kind:'keys',application_response:null,keys:[key()],pad_returns:'0',measured:true,applicable:true,all_requested_observed:true,
 keys_observed:1,keys_requested:1,keys_with_poll_high:0,keys_with_message_dispatch:1,outcome:'observed',polling_measured:true,all_requested_polling_observed:false,polling_outcome:'not_observed',all_requested_messages_dispatched:true,observed_channels:['WindowMessageDispatch']});
const native=observation=>({ok:true,accepted:false,job_state:'completed',job_complete:true,input_injected:true,job_id:'1:2:3',input_read_observation:observation});
let o=message(),p=project(o),q=qualify(native(o));
check(p.outcome==='observed'&&p.channels[0]==='WindowMessageDispatch'&&p.keys[0].channels[0]==='WindowMessageDispatch','message-only dispatch is named observed input');
check(p.polling_outcome==='not_observed'&&p.reader_return_observed===false&&p.polling_channels.length===0,'message-only observation preserves the separate no-polling-return fact');
check(q.input_observed&&q.reader_return_observed===false&&!q.input_read_warning&&q.input_read_note.includes('does not prove application consumption')&&q.application_response===null,'completed message input is successful without a false negative or consumption claim');
o=message();o.keys=[key(87,'1','0')];Object.assign(o,{keys_with_poll_high:1,keys_with_message_dispatch:0,polling_outcome:'observed',all_requested_polling_observed:true,all_requested_messages_dispatched:false,observed_channels:['GetAsyncKeyState']});p=project(o);
check(p.outcome==='observed'&&p.reader_return_observed&&p.channels[0]==='GetAsyncKeyState','GetAsyncKeyState remains an observed polling channel');
const polling=structuredClone(o);o.keys[0].poll_reads='9007199254740993';o.keys[0].down_returns='9007199254740993';o.keys[0].channels.GetAsyncKeyState=channel('9007199254740993','9007199254740993');
check(project(o).reader_return_observed===true,'large exact uint64 counters never pass through Number');
o=message();o.keys.push(key(65,'1','0'));Object.assign(o,{keys_requested:2,keys_observed:2,keys_with_poll_high:1,polling_outcome:'partial',observed_channels:['GetAsyncKeyState','WindowMessageDispatch'],all_requested_messages_dispatched:false});p=project(o);
check(p.outcome==='observed'&&p.polling_outcome==='partial'&&p.reader_return_observed&&p.keys.length===2,'mixed message/poll chord separates full observed input from partial polling');
o=message();o.keys.push(key(65,'0','0'));Object.assign(o,{keys_requested:2,keys_observed:1,outcome:'partial',all_requested_observed:false,all_requested_messages_dispatched:false});q=qualify(native(o));
check(q.input_observed&&q.input_read_warning.includes('Some requested keys')&&q.ok,'partial chord warns without changing scheduler success');
o=message();o.keys[0].polling_measured=false;o.keys[0].poll_reads=null;o.keys[0].down_returns=null;o.keys[0].channels=Object.fromEntries(names.map(name=>[name,{measured:false,reads:null,down_returns:null}]));Object.assign(o,{polling_measured:false,polling_outcome:'unavailable',all_requested_polling_observed:null});p=project(o);
check(p.outcome==='observed'&&p.reader_return_observed===null&&p.channels[0]==='WindowMessageDispatch','unavailable poll snapshots cannot erase valid exact message dispatch');
o=structuredClone(polling);o.keys[0].messages_measured=false;o.keys[0].message_dispatches=null;o.all_requested_messages_dispatched=null;p=project(o);
check(p.reader_return_observed===true&&p.window_messages_dispatched===null,'invalid dispatch evidence cannot erase valid polling high');
o=message();o.keys[0].polling_measured=false;o.keys[0].modifier_alias_ambiguous=true;Object.assign(o,{polling_measured:false,polling_outcome:'unavailable',all_requested_polling_observed:null});p=project(o);
check(p.outcome==='observed'&&p.keys[0].modifier_alias_ambiguous&&p.reader_return_observed===null,'modifier dispatch remains observed while alias polling remains ambiguous');
for(const mutate of [
 o=>{o.keys[0].down_returns='2'},o=>{o.keys[0].message_dispatches=1},o=>{o.keys[0].message_dispatches='18446744073709551616'},
 o=>{o.keys[0].observed_channels=['GetAsyncKeyState']},o=>{o.observed_channels=[]},o=>{o.keys_observed=0},
 o=>{o.keys.push(structuredClone(o.keys[0]));o.keys_requested=2},o=>{o.keys[0].channels.OtherApi=channel()},
 o=>{o.keys[0].measured=false},o=>{o.all_requested_polling_observed=true},o=>{o.application_response=true},
]){o=message();mutate(o);p=project(o);check(p.outcome==='unavailable'&&p.channels===undefined,'malformed or inconsistent evidence cannot become named observed input');}
o=message();Object.assign(o,{terminal:false,frozen:false,outcome:'pending'});p=project(o);
check(p.outcome==='pending'&&p.channels===undefined,'running or unstarted observations do not name final readers');
o=message();Object.assign(o,{measured:false,outcome:'unavailable'});check(project(o).outcome==='unavailable','global unavailable evidence does not become absence');
o=message();o.version=1;check(project(o)===undefined&&qualify(native(o))===undefined,'legacy version1 retains the existing presenter path');
o=message();const queued={...native(o),accepted:true,job_state:'queued',job_complete:false,input_injected:null};check(qualify(queued)===queued,'queued admission remains unchanged without a premature observed verdict');
const pad={...message(),kind:'pad',keys:[],keys_requested:0,pad_returns:'2',polling_outcome:'observed',all_requested_polling_observed:true,observed_channels:['XInput']};
check(project(pad).channels[0]==='XInput'&&project(pad).reader_return_observed,'job-owned pad returns remain named API observations');
if(process.env.VINPUT_RECEIPT_NATIVE_LOG){
 const lines=readFileSync(process.env.VINPUT_RECEIPT_NATIVE_LOG,'utf8').split(/\r?\n/).filter(line=>line.startsWith('RECEIPT '));
 check(lines.length===5,'actual native serializer emitted all five pinned projection cases');
 for(const line of lines){const match=/^RECEIPT (\S+) (.*)$/.exec(line);const value=JSON.parse(match[2]).input_read_observation;const projection=project(value);check(projection.input_observed===true&&projection.channels.length>0&&projection.application_response===null,`actual native ${match[1]} receipt projects without losing observed channel`);if(match[1]==='mixed_unknown'){const enriched=qualify(native(value));check(projection.outcome==='unavailable'&&projection.reader_return_observed===true&&enriched.reader_return_observed===true&&enriched.input_read_warning,'known GetAsyncKeyState high is retained despite a different unavailable modifier');}}
}
console.log(`receipt projection: ${passed} PASS / ${failed} FAIL`);process.exitCode=failed?1:0;
