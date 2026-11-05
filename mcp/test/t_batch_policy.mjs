import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {state,check}=makeChecker(),pid=process.pid;
const points=Array.from({length:5},(_,i)=>({x:100+i*20,y:200})),region={x:0,y:0,w:100,h:100};
const t=await startFake({}, {VINPUT_EXE:fileURLToPath(new URL('./fake_agent_loops.mjs',import.meta.url)),VINPUT_MCP_COMPACT:'false'});
const requests=()=>t.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1])),actions=()=>requests().filter(a=>['click','mouse','key'].includes(a.cmd));
const clicks=()=>actions().filter(a=>a.cmd==='click'),set=s=>{t.setScenario(s);t.clearCalls();};
const base={pid,points,timeout_ms:30000,hold_ms:1,hover_ms:0,verify_change:true,verify_region:region};
const tooltip={pid,points:points.slice(0,2),timeout_ms:30000,hold_ms:1,hover_ms:0,verify:'tooltip',reject_texts:['Cannot build on water'],tooltip_hover_ms:0};
try{
 set({motion_samples:['000000']});let r=await t.call('click_many',{...base,compact:true});
 check(r.isError&&r.json.compact&&r.json.error==='point_region_unchanged_after_delivered_click'&&r.json.stop_reason==='unchanged_limit'&&r.json.next_index===1&&r.json.completed_injected_points===1&&r.json.points[0].changed===false&&clicks().length===1,'default stop/1 preserves its error and first delivered point while compact reports explicit reason and next index');
 set({motion_samples:['000000','000000','000000','000000','ffffff','ffffff','ffffff','ffffff','ffffff']});r=await t.call('click_many',{...base,unchanged_limit:2,compact:true});
 check(r.isError&&r.json.next_index===4&&r.json.unchanged_streak===2&&r.json.completed_injected_points===4&&JSON.stringify(r.json.points.map(p=>p.changed))==='[false,true,false,false]'&&clicks().length===4,'changed pixels reset the streak and two later unchanged clicks stop before point five');
 set({motion_samples:['000000']});r=await t.call('click_many',{...base,verify_policy:'report_only',compact:true});
 check(!r.isError&&r.json.next_index===5&&r.json.stop_reason==='all_points_acknowledged'&&r.json.unchanged_limit===null&&r.json.points.every(p=>p.changed===false&&p.placement_confirmed===null)&&r.json.game_accepted_points===null&&clicks().length===5,'report-only finishes all original clicks without turning unchanged pixels into game acceptance');
 for(const options of [{verify_policy:'report_only'},{unchanged_limit:2},{verify_change:true,verify_region:region,verify_policy:'report_only',unchanged_limit:2},{verify_change:true,verify_region:region,unchanged_limit:0},{verify:'tooltip'},{reject_texts:['no']},{verify:'tooltip',reject_texts:['!!!']}]){
  set({});r=await t.call('click_many',{pid,points:points.slice(0,1),...options});check(r.isError&&!t.calls().length,'invalid/detached batch policy refuses before native I/O: '+JSON.stringify(options));
 }
 set({click_patches:{2:{ok:true,button_released:false,input_injected:null,delivery_unknown:true,reply_raw:'unmodified ACK',custom_unknown:{token:'keep-exact'}}}});r=await t.call('click_many',{pid,points,timeout_ms:30000,compact:true});
 check(r.isError&&r.json.compact&&r.json.next_index===2&&r.json.uncertain_index===1&&r.json.completed_injected_points===1&&r.json.points[1].receipt.reply_raw==='unmodified ACK'&&r.json.points[1].receipt.custom_unknown.token==='keep-exact'&&r.json.do_not_repeat_delivered_points&&clicks().length===2,'uncertain attempted point is never the resume index and its exact native unknown fields survive compact mode');
 set({hover_texts:['CANNOT BUILD ON WATER','Cost 100']});r=await t.call('click_many',{...tooltip,compact:true});
 check(!r.isError&&r.json.skipped_points===1&&r.json.completed_injected_points===1&&r.json.next_index===2&&r.json.stop_reason==='all_points_processed'&&r.json.points[0].reason==='reject_text_observed'&&r.json.points[0].click_attempted===false&&r.json.points[0].placement_confirmed===null&&clicks().length===1&&actions().filter(a=>a.cmd==='mouse').length===2,'one literal reject-text probe skips that click while the other original point is clicked once');
 const ocrArgs=t.callsOf('ocr').map(cmdOf);check(ocrArgs.length===2&&ocrArgs.every(a=>a.includes('--lines-only')&&a.includes('--observation-only=true')&&a.includes('--region')),'tooltip OCR is scoped/lines-only and uses no input fallback');
 set({hover_texts:['','Cost 100']});r=await t.call('click_many',tooltip);
 check(!r.isError&&r.json.points[0].skipped&&r.json.points[0].reason==='tooltip_empty'&&r.json.points[0].delivery_confirmed===null&&r.json.skipped_points===1&&clicks().length===1,'empty OCR is a skip, not proof that placement is valid');
 set({ocr_patch:{capture_scope:{hwnd:'0xBAD'},custom_unknown:'original-wrong-scope'}});r=await t.call('click_many',{...tooltip,compact:true});
 check(r.isError&&r.json.compact&&r.json.next_index===0&&r.json.completed_injected_points===0&&r.json.failure_receipt.custom_unknown==='original-wrong-scope'&&r.json.failure_receipt.capture_scope.hwnd==='0xBAD'&&clicks().length===0&&actions().length===1,'wrong-scope OCR stops after its one hover with the exact failed observation retained');
 set({ocr_patch:{ok:false,anticheat_refused:true,error:'known protected target'}});r=await t.call('click_many',{...tooltip,compact:true});
 check(r.isError&&r.json.anticheat_refused&&r.json.failure_receipt.error==='known protected target'&&!clicks().length,'a later OCR refusal stays explicit and never triggers a click or fallback');
 set({status_patches:{2:{owner_active:true,owner_input_count:1}}});r=await t.call('click_many',tooltip);
 check(r.isError&&r.json.stop_reason==='user_controls_target'&&!clicks().length&&actions().length===1,'activity after tooltip OCR is rechecked before any click');
 set({});r=await t.call('hover_many',{pid,points:[{x:0,y:0},{x:799,y:599}],ocr:true,hover_ms:0,compact:true,timeout_ms:30000});
 const hoverSummaryOk=!r.isError&&r.json.points[0].x===0&&r.json.points[1].x===799&&r.json.points[1].text==='connection ready'&&r.json.points[0].probe_region.x===0&&r.json.points[1].probe_region.x===400&&r.json.points[1].probe_region.y===400&&r.json.points[1].ocr_capture_qpc_ticks&&actions().every(a=>a.cmd==='mouse');
 const hoverRegionsOk=t.callsOf('ocr').map(cmdOf).map(a=>a[a.indexOf('--region')+1]).join('|')==='0,0,400,200|400,400,400,200';
 if(!hoverSummaryOk||!hoverRegionsOk)console.error('HOVER_DIAGNOSTIC '+JSON.stringify({isError:r.isError,json:r.json,raw_result:r.r,calls:t.timedCalls(),ocr_args:t.callsOf('ocr').map(cmdOf)}));
 check(hoverSummaryOk,'compact hover uses clamped per-point local regions and readable x/y/text with separate OCR QPC');
 check(hoverRegionsOk,'each OCR receives the exact local client region, not the full frame');
 set({});r=await t.call('hover_many',{pid,points:points.slice(0,1),probe_region:{x:1,y:2,w:30,h:40},ocr:true,hover_ms:0,timeout_ms:30000});
 check(!r.isError&&r.json.points[0].probe_region?.x===1&&cmdOf(t.callsOf('ocr')[0]).includes('1,2,30,40'),'explicit shared probe_region keeps its existing absolute-client meaning');
 set({});const controller=new AbortController(),pending=t.client.callTool({name:'click_many',arguments:{...tooltip,tooltip_hover_ms:300}},undefined,{signal:controller.signal,timeout:10000}).catch(e=>e),until=Date.now()+3000;
 while(!actions().length&&Date.now()<until)await new Promise(resolve=>setTimeout(resolve,10));controller.abort();await pending;await new Promise(resolve=>setTimeout(resolve,500));
 check(actions().filter(a=>a.cmd==='mouse').length===1&&!clicks().length&&!t.callsOf('release').length,'MCP cancellation during the tooltip dwell prevents all clicks without broad cleanup');
}finally{await t.close();}
console.log(`batch policy: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
