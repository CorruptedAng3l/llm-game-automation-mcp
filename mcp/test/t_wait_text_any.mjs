import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const t=await startFake({replies:{'wait-text':{ok:true,found:true,match:{query:'DEFEAT',text:'Defeat'},polls:2,ocr_passes:1}}});
try{
 let r=await t.call('wait_text',{pid,text:['VICTORY','DEFEAT'],region:{x:1,y:2,w:100,h:50},timeout_ms:1000});let args=cmdOf(t.callsOf('wait-text')[0]??[]);
 check(!r.isError&&r.json.matched_text==='DEFEAT'&&r.json.match.text==='Defeat'&&r.json.polls===2,'any-of wait preserves original match and identifies native matching query',r.text);
 check(t.callsOf('wait-text').length===1&&args.includes('--any')&&args[args.indexOf('--any')+1]==='VICTORY|DEFEAT'&&args.includes('--region')&&!t.callsOf('ocr').length&&!t.callsOf('capture').length,'all alternatives use one native shared-capture/OCR polling call');
 t.clearCalls();r=await t.call('wait_text',{pid,text:'VICTORY'});args=cmdOf(t.callsOf('wait-text')[0]??[]);check(args[1]==='VICTORY'&&!args.includes('--any')&&r.json.matched_text===undefined,'single string retains existing native path and reply');
 for(const text of [[],[''],['   '],['a|b'],Array.from({length:17},()=> 'x')]){t.clearCalls();r=await t.call('wait_text',{pid,text});check(r.isError&&!t.callsOf('wait-text').length,'invalid alternatives refuse before native I/O: '+JSON.stringify(text));}
 t.setScenario({replies:{'wait-text':{ok:true,found:false,gone:true,match:null}}});t.clearCalls();r=await t.call('wait_text',{pid,text:['VICTORY','DEFEAT'],gone:true});check(!r.isError&&r.json.gone===true&&r.json.matched_text===null&&cmdOf(t.callsOf('wait-text')[0]).includes('--gone'),'gone list waits for all alternatives absent without inventing match');
 t.setScenario({replies:{'wait-text':{ok:false,timed_out:true,ocr_saw:'Loading'}}});t.clearCalls();r=await t.call('wait_text',{pid,text:['VICTORY','DEFEAT'],timeout_ms:100});check(r.isError&&r.json.timed_out===true&&r.json.ocr_saw==='Loading','timeout retains native OCR evidence',r.text);
 t.setScenario({replies:{'wait-text':{ok:true,found:true,match:{query:'wrong',text:'Other'}}}});t.clearCalls();r=await t.call('wait_text',{pid,text:['VICTORY','DEFEAT']});check(r.isError&&r.json.wait_text_input?.match.query==='wrong'&&r.json.matched_text===null,'unknown native matching query cannot fabricate success',r.text);
 const schema=(await t.client.listTools()).tools.find(t=>t.name==='wait_text');check(schema.description.includes('VICTORY')&&schema.description.includes('ANY')&&JSON.stringify(schema.inputSchema.properties.text).includes('array'),'list discovery advertises any-of waits and shared OCR');
}finally{await t.close();}
console.log(`wait text any: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
