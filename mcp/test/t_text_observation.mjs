import {join} from 'node:path';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const caps={ok:true,text_word_boundary_version:1,text_line_match_version:1,text_box_stability_version:1};
const scene=help=>({replies:{help,'click-text':{ok:true,clicked:true},'wait-text':{ok:true,found:true,match:{text:'Victory'}}}});
const t=await startFake(scene({...caps,text_box_stability_version:undefined}));
try{
 let r=await t.call('click_text',{pid,text:'PLAY',stable_ms:300,timeout_ms:1000});
 check(r.isError&&r.json.input_injected===false&&t.callsOf('click-text').length===0,'old CLI cannot silently ignore position-stability before a click');
 t.setScenario(scene({...caps,text_box_stability_version:'1'}));t.clearCalls();r=await t.call('click_text',{pid,text:'PLAY',stable_ms:300,timeout_ms:1000});
 check(r.isError&&t.callsOf('click-text').length===0,'wrong-typed stability capability refuses before input');
 t.setScenario(scene(caps));t.clearCalls();r=await t.call('click_text',{pid,text:'PLAY',stable_ms:300,timeout_ms:1000});
 const click=cmdOf(t.callsOf('click-text')[0]??[]);
 check(!r.isError&&t.callsOf('click-text').length===1&&click[click.indexOf('--stable-ms')+1]==='300','supported stability delegates one native observed click without a wrapper retry');
 t.clearCalls();r=await t.call('click_text',{pid,text:'PLAY',stable_ms:300,timeout_ms:100});
 check(r.isError&&t.calls().length===0,'impossible stability budget refuses before any CLI work');
 t.setScenario(scene({...caps,text_box_stability_version:undefined}));t.clearCalls();r=await t.call('click_text',{pid,text:'PLAY'});
 check(!r.isError&&t.callsOf('click-text').length===1&&!cmdOf(t.callsOf('click-text')[0]).includes('--stable-ms'),'ordinary text click keeps its existing capability contract');
 const proof=join(t.dir,'exact matched proof.png');t.setScenario({replies:{'wait-text':{ok:true,found:true,match:{text:'Victory'},capture_qpc_ticks:'333',observation_frame_path:proof}}});t.clearCalls();
 r=await t.call('wait_text',{pid,text:'Victory',save_match:true,save_match_path:proof,compact:true});
 const wait=cmdOf(t.callsOf('wait-text')[0]??[]);
 check(!r.isError&&r.json.match_frame_saved===true&&r.json.match_frame_requested_path===proof&&r.json.observation_frame_path===proof&&wait[wait.indexOf('--observation-capture')+1]===proof,'exact requested same-frame proof path survives compact presentation');
 check(t.callsOf('wait-text').length===1&&t.callsOf('capture').length===0&&t.callsOf('ocr').length===0,'proof requests no later capture or second OCR wait');
 t.setScenario({replies:{'wait-text':{ok:true,found:true,match:{text:'Victory'},capture_qpc_ticks:'333',observation_frame_error:'save denied'}}});t.clearCalls();r=await t.call('wait_text',{pid,text:'Victory',save_match:true,save_match_path:proof,compact:true});
 check(!r.isError&&r.json.found&&!r.json.match_frame_saved&&r.json.observation_frame_error==='save denied'&&r.json.match_frame_warning&&t.callsOf('wait-text').length===1&&t.callsOf('capture').length===0,'proof save failure preserves the wait result and explicit saved:false without recapture');
 for(const args of [{out_dir:t.dir},{save_match_path:proof},{save_match:true,save_match_path:proof,out_dir:t.dir}]){
  t.clearCalls();r=await t.call('wait_text',{pid,text:'Victory',...args});check(r.isError&&t.calls().length===0,'invalid proof destinations refuse before native work');
 }
}finally{await t.close()}
console.log(`text observation: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
