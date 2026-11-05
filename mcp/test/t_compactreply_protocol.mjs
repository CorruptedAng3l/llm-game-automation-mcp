import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const observation={version:1,started:true,terminal:true,frozen:true,kind:'keys',application_response:null,measured:true,applicable:true,all_requested_observed:true,keys_observed:1,keys_requested:1,outcome:'observed',scope:'native job scope '.repeat(100),keys:[{vk:87,measured:true,poll_reads:'2',down_returns:'1',message_dispatches:'1',channels:{GetKeyboardState:{measured:true,reads:'2',down_returns:'1'}}}]};
const native={ok:true,accepted:false,job_id:`${pid}:123:4`,owner:'mcp-test',job_kind:'keys',job_state:'completed',job_complete:true,input_injected:true,input_read_observation:observation};
const t=await startFake({replies:{key:native,'job-status':{ok:true,job:native}}},{VINPUT_MCP_COMPACT:'false'});
try{
 const listing=await t.client.listTools();
 check(listing.tools.every(x=>x.inputSchema.properties.compact?.type==='boolean'),'every registered tool and alias advertises one boolean compact option');
 let r=await t.call('key',{pid,key:'w'});check(r.json.input_read_observation&&!r.json.compact,'default actual MCP reply retains native full evidence');
 t.clearCalls();r=await t.call('key',{pid,key:'w',compact:true,capture_after:0,max_width:160});
 check(!r.isError&&r.json.compact&&r.json.input_read.keys[0].channels[0]==='GetKeyboardState'&&r.json.application_response===null,'actual MCP input compacts only at the final boundary and names the observed reader');
 check(r.image&&r.json.capture_after.path&&t.callsOf('key').length===1&&t.callsOf('capture').length===1,'compact key keeps one original action, one capture and inline image');
 check(t.calls().every(a=>!a.includes('--compact')&&!a.some(v=>v.includes('"compact":'))),'presentation option never reaches native command arguments');
 r=await t.call('job_status',{pid,id:native.job_id,compact:true});check(r.json.job.job_id===native.job_id&&r.json.job.job_state==='completed'&&r.json.job.input_read.keys[0].channels[0]==='GetKeyboardState','nested asynchronous job outcome retains continuation and reader summary');
 r=await t.call('capture',{pid,compact:true});check(!r.isError&&r.json.compact&&r.json.path&&r.image&&r.json.mapping,'standalone capture uses the same compact mode and preserves image mapping');
 t.clearCalls();r=await t.call('type',{pid,text:'hello',compact:true});check(!r.isError&&r.json.compact&&t.callsOf('json').length===1&&r.json.text_input_observation.backend==='WM_CHAR'&&r.json.text_input_observation.as_keys_hint,'type alias accepts the global option and still sends one action with its consumption hint');
 for(const [name,args,cmd] of [['key_down',{key:'w'},'down'],['key_up',{key:'w'},'up'],['move_relative',{dx:3,dy:1},'move'],['scroll',{notches:1,edge_margin:0},'scroll'],['pad',{buttons:'a',ms:50},'pad'],['release',{},'release']]){
  t.clearCalls();r=await t.call(name,{pid,...args,compact:true});check(!r.isError&&r.json.compact&&t.callsOf(cmd).length===1,'the same final serializer covers '+name+' without replay');
 }
 t.setScenario({replies:{'click-text':{ok:true,clicked:true,match:{text:'PLAY',center:[10,20],box:[1,2,30,10]},match_count:1,click:{ok:true,di_poll_channel:'DirectInputMouseState',di_poll_confirmed:true}}}});
 r=await t.call('click_text',{pid,text:'PLAY',compact:true});check(!r.isError&&r.json.clicked===true&&r.json.match.center[0]===10&&r.json.click.di_poll_channel==='DirectInputMouseState'&&r.json.click.di_poll_confirmed,'compact visual text click retains matched coordinates and its nested reader channel');
 t.setScenario({replies:{ocr:{ok:true,text:'READY',line_count:1,lines:[{text:'READY',box:[1,2,30,10],words:[{text:'READY',box:[1,2,30,10]}]}],words:[{text:'READY',box:[1,2,30,10]}],capture_scope:{id:'exact-frame',pid},capture_qpc_ticks:'18446744073709551614',ocr_confidence_note:'confidence unknown'}}});
 r=await t.call('ocr',{pid,compact:true,words:true});check(!r.isError&&r.json.text==='READY'&&r.json.lines[0].box[2]===30&&r.json.capture_scope.id==='exact-frame'&&r.json.capture_qpc_ticks==='18446744073709551614'&&r.json.ocr_confidence_note&&r.json.words[0].box[2]===30&&r.json.lines[0].words[0].text==='READY','compact read-only OCR retains requested words, coordinates, exact capture provenance and notes');
 t.setScenario({replies:{seq:{ok:true,accepted:true,job_id:native.job_id,job_state:'queued',job_complete:false,input_injected:null,hold_ready:false,anchor_source:'pending_present_hook',present_anchor_target_index:'1000',anchor_qpc_ticks:'0',sequence_start:{observed:true,observed_capture_qpc_ticks:'18446744073709551614',native_deadlines_anchor:'native_present_hook',game_event_onset_measured:false},input_read_observation:{...observation,terminal:false,frozen:false,outcome:'pending'}}}});
 r=await t.call('key_sequence',{pid,steps:[{action:'hold',key:'w',ms:20}],compact:true});check(!r.isError&&r.json.job_state==='queued'&&r.json.job_id===native.job_id&&r.json.input_injected===null&&r.json.input_read.keys[0].channels===undefined,'compact queued keyboard sequence preserves admission without a premature reader claim');
 check(r.json.anchor_source==='pending_present_hook'&&r.json.present_anchor_target_index==='1000'&&r.json.anchor_qpc_ticks==='0'&&r.json.sequence_start.observed_capture_qpc_ticks==='18446744073709551614'&&r.json.sequence_start.game_event_onset_measured===false,'actual MCP boundary retains exact queued sequence anchor provenance');
 t.setScenario({replies:{launch:{ok:true,pid,process_created:'18446744073709551614',launch_state:'launcher_ready',waiting_for:'Game.exe',input_ready:false}}});
 r=await t.call('launch',{steam_appid:'123',wait_exe:'Game.exe',claim:true,compact:true});check(!r.isError&&r.json.next_action?.arguments.follow_pid===pid&&r.json.next_action.arguments.follow_created==='18446744073709551614'&&r.json.next_action.arguments.wait_exe==='Game.exe'&&r.json.next_action.arguments.claim,'compact actual launcher reply preserves its exact follow action');
 t.clearCalls();r=await t.call('key',{pid,key:'w',compact:'yes'});check(r.isError&&!t.callsOf('key').length,'invalid compact option is rejected by schema before input');
 t.setScenario({replies:{key:{ok:false,error:'blocked',anticheat_refused:true,product:'guard',_stderr:'original stderr'}}});
 r=await t.call('key',{pid,key:'w',compact:true});check(r.isError&&r.json.anticheat_refused&&r.json.product==='guard'&&r.text.includes('original stderr'),'compact errors retain refusal details and native stderr');
}finally{await t.close();}
const env=await startFake({replies:{key:native}},{VINPUT_MCP_COMPACT:'true'});
try{
 let r=await env.call('key',{pid,key:'w'});check(r.json.compact&&r.json.input_read&&!r.json.input_read_observation,'environment selects compact by default');
 r=await env.call('key',{pid,key:'w',compact:false});check(!r.json.compact&&r.json.input_read_observation.scope===observation.scope,'explicit false overrides environment and restores exact full receipt');
}finally{await env.close();}
console.log(`compact reply protocol: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
