import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const t=await startFake({replies:{status:{ok:true,counters:{di_kbd:0,di_data_kbd:42}},json:{ok:true,accepted_utf16_units:3,delivery_confirmed:false}}});
try{
 let r=await t.call('type_text',{pid,text:'Vin'});
 check(!r.isError&&r.json.text_input_observation.directinput_keyboard_reader_observed===true&&r.json.text_input_observation.as_keys_hint.includes('as_keys:true'),'DI keyboard reader produces an actionable as_keys hint');
 let calls=t.callsOf('json');check(calls.length===1&&JSON.parse(calls[0].at(-1)).keys===undefined,'WM_CHAR is sent exactly once without replay or silent backend replacement');
 t.clearCalls();r=await t.call('type_text',{pid,text:'Vin',as_keys:true});calls=t.callsOf('json');check(calls.length===1&&JSON.parse(calls[0].at(-1)).keys===true&&!t.callsOf('status').length,'explicit as_keys sends one key request with no advisory preflight');
 t.setScenario({replies:{status:{ok:false,error:'unavailable'},json:{ok:true,accepted_utf16_units:3}}});t.clearCalls();r=await t.call('type_text',{pid,text:'Vin'});check(!r.isError&&r.json.text_input_observation.directinput_keyboard_reader_observed===null&&t.callsOf('json').length===1,'unavailable advisory evidence stays unknown and does not prevent deliberate text entry');
 t.setScenario({replies:{status:{ok:true,counters:{di_kbd:1,di_data_kbd:0}},json:{ok:false,partial:true,accepted_utf16_units:1}}});t.clearCalls();r=await t.call('type_text',{pid,text:'Vin'});check(r.isError&&r.json.partial&&r.json.accepted_utf16_units===1&&t.callsOf('json').length===1,'partial type failure retains admission receipt and never retries with keys');
}finally{await t.close();}
console.log(`text reader hint: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
