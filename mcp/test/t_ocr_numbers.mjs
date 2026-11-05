import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const number={version:1,status:'numeric_observed',value:'005',box_client:[100,200,8,14],agreement_count:2,analysis_complete:true,reason:'same_frame_pass_agreement',observations:[
 {scale:1,preprocess:'none',ok:true,text:'UBS',eligible_value:null,box_client:null,reason:'non_numeric'},
 {scale:3,preprocess:'contrast',ok:true,text:'005',eligible_value:'005',box_client:[100,200,8,14],reason:'eligible_numeric'},
 {scale:2,preprocess:'white_glyph',ok:true,text:'005',eligible_value:'005',box_client:[100,200,8,14],reason:'eligible_numeric'},
]};
const native={ok:true,text:'005',line_count:1,ocr_empty_retry_version:1,ocr_compact_version:1,digits_only:true,number_read:number,ocr_confidence:null,capture_scope:{id:'same-crop',pid},capture_qpc_ticks:'18446744073709551614',frame_qpc_ticks:'18446744073709551600',qpc_frequency:'10000000'};
const t=await startFake({replies:{ocr:native}});
try{
 let r=await t.call('ocr',{pid,digits_only:true,region:[90,190,24,24]});let calls=t.callsOf('ocr'),args=calls[0]??[];
 check(!r.isError&&r.json?.number_read?.value==='005','numeric OCR keeps exact observed string including leading zeros');
 check(calls.length===1&&args.includes('--digits-only=true')&&args.includes('90,190,24,24')&&!args.includes('--tiles'),'digits mode forwards one region in one invocation and disables default tiles');
 check(JSON.stringify(r.json?.number_read)===JSON.stringify(number),'numeric OCR retains all nonnumeric readings, agreement and box evidence');
 t.clearCalls();r=await t.call('ocr',{pid,digits_only:true,region:{x:90,y:190,w:24,h:24},text_only:true});
 check(!r.isError&&r.json.digits_only===true&&JSON.stringify(r.json.number_read)===JSON.stringify(number),'text_only retains the entire number-read contract');
 check(r.json.ocr_confidence===null&&r.json.capture_qpc_ticks===native.capture_qpc_ticks&&r.json.frame_qpc_ticks===native.frame_qpc_ticks&&r.json.capture_scope.id==='same-crop','digits compact mode preserves null confidence and exact same-frame provenance');
 const ambiguous={...number,status:'ambiguous',value:null,box_client:null,reason:'numeric_disagreement',observations:[...number.observations,{scale:4,preprocess:'white_glyph',ok:true,text:'3',eligible_value:'3',box_client:[100,200,8,14],reason:'eligible_numeric'}]};
 t.setScenario({replies:{ocr:{...native,number_read:ambiguous}}});r=await t.call('ocr',{pid,digits_only:true,region:[90,190,24,24],text_only:true});
 check(!r.isError&&JSON.stringify(r.json.number_read)===JSON.stringify(ambiguous),'conflicting numeric observations remain ambiguous/null with the minority evidence');
 const unknown={...number,status:'unknown',value:null,box_client:null,analysis_complete:false,reason:'incomplete_engine_analysis',observations:[...number.observations,{scale:4,preprocess:'white_glyph',ok:false,text:'',eligible_value:null,box_client:null,reason:'engine_error',error:'controlled failure'}]};
 t.setScenario({replies:{ocr:{...native,number_read:unknown}}});r=await t.call('ocr',{pid,digits_only:true,region:[90,190,24,24],text_only:true});
 check(!r.isError&&JSON.stringify(r.json.number_read)===JSON.stringify(unknown),'incomplete numeric analysis remains unknown/null and retains actual engine failure');
 t.clearCalls();await t.call('ocr',{pid,digits_only:true,region:[90,190,24,24],retry_empty:false,scale:3,preprocess:'threshold'});args=t.callsOf('ocr')[0];
 check(args.includes('--retry-empty=false')&&args.includes('threshold')&&args.includes('3'),'numeric mode respects explicit retry, scale and initial preprocessing options');
 for(const invalid of [{digits_only:true},{digits_only:true,region:[0,0,513,24]},{digits_only:true,region:[0,0,24,24],tiles:true}]){
  t.clearCalls();r=await t.call('ocr',{pid,...invalid});check(r.isError&&t.callsOf('ocr').length===0,'invalid numeric region/tiles refuses before CLI capture');
 }
 t.setScenario({replies:{ocr:{ok:true,text:'UBS',line_count:1}}});r=await t.call('ocr',{pid,digits_only:true,region:[0,0,24,24]});
 check(r.isError&&/did not confirm digits-only/.test(r.json?.error??'')&&!r.json?.number_read,'old CLI ignored option cannot become a numeric success');
 t.setScenario({replies:{ocr:{ok:false,error:'OCR language unavailable'}}});r=await t.call('ocr',{pid,digits_only:true,region:[0,0,24,24]});
 check(r.isError&&r.json.error==='OCR language unavailable','numeric mode preserves initial engine failure');
 t.setScenario({replies:{ocr:{ok:true,text:'ordinary text',line_count:1}}});t.clearCalls();r=await t.call('ocr',{pid,digits_only:false,tiles:false});
 check(!r.isError&&r.json.text==='ordinary text'&&t.callsOf('ocr')[0].includes('--digits-only=false'),'explicit false preserves ordinary OCR and reaches native boolean');
}finally{await t.close();}
console.log(`ocr numbers MCP: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
