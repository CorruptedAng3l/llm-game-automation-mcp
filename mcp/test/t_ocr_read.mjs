import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const scope={id:'owned-capture-1',pid},timing={timing_clock:'QueryPerformanceCounter',qpc_frequency:'10000000',capture_qpc_ticks:'18446744073709551614',frame_qpc_ticks:'18446744073709551600'};
const native={ok:true,text:'Log\nOK',line_count:2,lang:'en-US',ocr_empty_retry_version:1,ocr_compact_version:1,retry_empty:true,ocr_scale:2,ocr_pass:'contrast',ocr_passes:3,ocr_retry_errors:0,capture_scope:scope,...timing,ocr_confidence:null,ocr_confidence_note:'Windows OCR gives no confidence',lines:Array.from({length:30},()=>({text:'Log OK',box:[1,2,3,4],words:Array.from({length:20},()=>({text:'Log',box:[1,2,3,4]}))}))};
const t=await startFake({replies:{ocr:native}});
try{
  let r=await t.call('ocr',{pid});check(!r.isError&&r.json.lines.length===30&&r.json.text==='Log\nOK','default OCR retains rich output');
  check(t.callsOf('ocr')[0].includes('--retry-empty=true'),'default OCR requests same-capture empty-result retry');
  t.clearCalls();r=await t.call('ocr',{pid,text_only:true});const args=t.callsOf('ocr')[0];
  check(args.includes('--text-only')&&args.includes('--retry-empty=true'),'compact OCR reaches native options');
  check(!('lines'in r.json)&&!('words'in r.json)&&!('ocr_confidence_note'in r.json)&&r.json.text==='Log\nOK'&&r.json.line_count===2,'compact OCR omits geometry/prose and preserves text/count');
  check(JSON.stringify(r.json.capture_scope)===JSON.stringify(scope)&&Object.entries(timing).every(([k,v])=>r.json[k]===v),'compact OCR preserves exact capture scope and decimal QPC provenance');
  check(r.json.ocr_scale===2&&r.json.ocr_pass==='contrast'&&r.json.ocr_passes===3&&r.json.ocr_confidence===null,'compact OCR preserves selected retry evidence without confidence invention');
  check(r.text.length<JSON.stringify(native).length/4,'compact output materially reduces repeated line/word JSON');
  t.clearCalls();await t.call('ocr',{pid,scale:3,preprocess:'threshold',retry_empty:false,tiles:false,words:true});
  const call=t.callsOf('ocr')[0];check(call.includes('--retry-empty=false')&&!call.includes('--tiles')&&!call.includes('--lines-only')&&call.includes('threshold')&&call.includes('3'),'explicit scale/preprocessing/retry/tiles/word options forwarded');
  t.setScenario({replies:{ocr:{ok:true,text:'old CLI text',line_count:1,lines:[{text:'old CLI text'}]}}});
  r=await t.call('ocr',{pid,text_only:true});check(!r.isError&&!('lines'in r.json)&&/did not confirm/.test(r.json.ocr_retry_warning),'older CLI compact fallback remains small and exposes unconfirmed retries');
  r=await t.call('ocr',{pid,retry_empty:false});check(!('ocr_retry_warning'in r.json),'explicit retry opt-out does not imply missing support');
  t.setScenario({replies:{ocr:{ok:false,error:'OCR language unavailable'}}});
  r=await t.call('ocr',{pid,text_only:true});check(r.isError&&r.json.error==='OCR language unavailable','compact mode preserves engine errors');
  t.clearCalls();r=await t.call('ocr',{pid,preprocess:'fake'});check(r.isError&&t.callsOf('ocr').length===0,'invalid preprocessing refuses before capture');
}finally{await t.close();}
console.log(`ocr read MCP: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
