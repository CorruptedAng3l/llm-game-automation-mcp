import {fileURLToPath} from 'node:url';
import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const t=await startFake({lines:['Click Next to continue']},{VINPUT_EXE:fileURLToPath(new URL('./fake_text_line.mjs',import.meta.url))});
const set=(lines,replies={})=>{t.setScenario({lines,replies});t.clearCalls();};
try{
 let r=await t.call('click_text',{pid,text:'Next',timeout_ms:0});
 check(r.isError&&r.json?.clicked===false&&r.json?.exact_line===true,'default Next never clicks an embedded paragraph word');
 check(t.callsOf('help').length===1&&t.callsOf('click-text')[0]?.includes('--exact-line=true'),'one typed CLI capability preflight precedes line flag');
 set(['Click Next to continue','Next']);r=await t.call('click_text',{pid,text:'Next'});
 check(!r.isError&&r.json?.match?.line==='Next'&&r.json?.candidates?.length===1,'standalone label wins over paragraph context');
 set(['Click Next to continue','Next','next']);r=await t.call('click_text',{pid,text:'Next'});
 check(r.isError&&r.json?.candidates?.length===2&&r.json.candidates.every(x=>x.line.toLowerCase()==='next'),'ambiguity reports only standalone alternatives');
 r=await t.call('click_text',{pid,text:'Next',nth:2});check(!r.isError&&r.json?.match?.line==='next','nth retains standalone reading order');
 set(['Player','PLAY']);r=await t.call('click_text',{pid,text:'PLAY'});check(!r.isError&&r.json?.match?.line==='PLAY','line mode preserves PLAY versus Player boundary');
 set(['Read Nexf carefully','Nexf']);r=await t.call('click_text',{pid,text:'Next',fuzzy:1});check(!r.isError&&r.json?.match?.line==='Nexf'&&r.json?.match?.match_score<1&&r.json?.ocr_confidence===null,'fuzzy covers entire label and never invents OCR confidence');
 set(['Click Next to continue']);r=await t.call('click_text',{pid,text:'Next',exact_line:false});check(!r.isError&&r.json?.match?.line.includes('Click Next')&&t.callsOf('click-text')[0]?.includes('--exact-line=false'),'explicit line opt-out permits intentional paragraph text');
 set(['Click Next to continue']);r=await t.call('find_text',{pid,text:'Next'});check(!r.isError&&r.json?.clicked===false&&t.callsOf('help').length===0,'read-only find keeps legacy paragraph default');
 r=await t.call('find_text',{pid,text:'Next',exact_line:true});check(r.isError&&r.json?.clicked===false,'read-only exact line search applies same selection');
 set(['Next'],{help:{ok:true,text_word_boundary_version:1}});r=await t.call('click_text',{pid,text:'Next'});check(r.isError&&r.json?.input_injected===false&&t.callsOf('click-text').length===0,'old CLI line capability refused with zero input');
 check(/exact_line:false/.test(r.text),'capability error explains explicit legacy opt-out');
 set(['Next'],{help:{ok:true,text_word_boundary_version:1,text_line_match_version:'1'}});r=await t.call('click_text',{pid,text:'Next',optional:true});check(r.isError&&t.callsOf('click-text').length===0,'malformed capability and optional mode never authorize input');
 set(['Next']);r=await t.call('click_text',{pid,text:'Next',exact_line:'true'});check(r.isError&&t.calls().length===0,'invalid line option refuses before capability work');
 set(['Click Next to continue']);r=await t.call('click_text',{pid,text:'Next',optional:true});check(!r.isError&&r.json?.skipped&&r.json?.clicked===false,'optional paragraph-only miss skips with no click');
}finally{await t.close();}
console.log(`text line MCP: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
