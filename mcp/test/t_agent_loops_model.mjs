import {clickMany,clickToMove,gridPoints,noteOtherInput} from '../build/agentloops.js';
import {makeChecker} from './lib.mjs';
const {state,check}=makeChecker(),pid=42,owner='unit',scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const parsed=r=>JSON.parse(r.content[0].text),result=json=>({ok:json.ok!==false,json,argv:[],stderr:'',timedOut:false,exitCode:json.ok===false?1:0});
function model(options={}){
 const calls=[];let epoch=0,n=0,clicks=0,position={x:300,y:200},hold;
 const ctx={owner,at:p=>['--owner',owner,'--pid',String(p)],epoch:()=>epoch,map:(_p,a,pts)=>({pts:options.outside?[[800,1]]:pts,mapped:false}),captureError:()=>undefined,finish:async(_p,r)=>{if(options.finishError)throw Error('capture failed');return r;},
  qualifyInput:r=>options.qualifyFailure?{...r,ok:false,json:{...r.json,ok:false,input_receipt:r.json,input_read_observation:{version:1}}}:r,
  run:async args=>{const c=args[4];calls.push(args.slice(4));if(c==='status')return result({ok:true,pid,capture_scope_version:1,pointer_scope_version:1,input_scope_version:1,input_admission_version:1,owner_input_count:0,virtual_down:[],vcur_x:position.x,vcur_y:position.y});if(c==='jobs')return result({ok:true,jobs:[]});
   if(c==='job-status')return result({ok:true,job:{...hold,ok:false,accepted:false,job_state:'running',hold_ready:true}});if(c==='job-cancel')return result({ok:true,job:{...hold,ok:false,accepted:false,job_state:'cancelled',job_complete:true,hold_ready:false,job_reason:'explicit_cancel'}});
   const r=JSON.parse(args[5]),fields={capture_scope_checked:true,pointer_scope_version:1,input_admission_version:1,admission_deadline_qpc_ticks:r.admit_before_qpc_ticks};
   if(r.cmd==='frame-hash')return result({ok:true,hash:n++?'BBBBBBBBBBBBBBBB':'AAAAAAAAAAAAAAAA',capture_scope:scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',capture_qpc_ticks:'100000',qpc_frequency:'1000'});
   if(r.cmd==='click'){clicks++;if(options.abortAfterClick)options.controller.abort();if(options.attachAfterClick)epoch++;if(options.otherInputAfterClick)noteOtherInput(pid);return result({ok:true,...fields,button_down_count:1,button_released:true,last_press_x:r.x,last_press_y:r.y});}
   if(r.cmd==='mouse'){position={x:r.x,y:r.y};return result({ok:true,...position,...fields});}
   if(r.cmd==='key'){hold={ok:true,accepted:true,job_id:'42:444:1',job_kind:'keys',owner,job_state:'queued',job_complete:false,hold_ready:false,requested_ms:r.ms,...fields};if(options.attachAfterHold)epoch++;return result(hold);}
   throw Error('unexpected model input');}};
 return {ctx,calls,get clicks(){return clicks;}};
}
const base={pid,points:[{x:10,y:20},{x:30,y:40}],timeout_ms:10000};
let m=model({attachAfterClick:true}),r=parsed(await clickMany(m.ctx,base));check(!r.ok&&r.points.length===1&&m.clicks===1&&r.error==='attachment_changed','local attachment change stops future points and preserves admitted receipt');
m=model({otherInputAfterClick:true});r=parsed(await clickMany(m.ctx,base));check(!r.ok&&r.points.length===1&&m.clicks===1&&r.error==='other_input_command_started','other ordinary input command interrupts helper without discarding the in-flight point');
const controller=new AbortController();m=model({abortAfterClick:true,controller});r=parsed(await clickMany(m.ctx,base,{signal:controller.signal}));check(!r.ok&&m.clicks===1&&r.points[0].confirmed&&r.error==='request_cancelled','cancel after native acknowledgement preserves that point and stops subsequent input');
m=model({qualifyFailure:true});r=parsed(await clickMany(m.ctx,base));check(!r.ok&&m.clicks===1&&r.points[0].qualified_receipt.input_receipt.ok===true,'input-read qualification failure stops batch and preserves original native receipt');
m=model({finishError:true});r=parsed(await clickMany(m.ctx,base));check(!r.ok&&r.points.length===2&&r.post_capture_error.includes('capture failed'),'post-action capture exception cannot discard point receipts');
m=model({outside:true});r=parsed(await clickMany(m.ctx,{...base,points:[{x:1,y:1}]}));check(!r.ok&&m.clicks===0&&r.input_injected===false,'invalid mapped point refuses before input');
m=model();r=parsed(await clickMany(m.ctx,{...base,timeout_ms:500}));check(!r.ok&&m.clicks===0&&r.input_session.stop_reason==='insufficient_budget_for_next_bounded_click','insufficient whole-point budget refuses bounded click admission');
check(JSON.stringify(gridPoints({x0:10,y0:20,dx:-2,dy:3,cols:2,rows:2}))==='[{"x":10,"y":20},{"x":8,"y":20},{"x":10,"y":23},{"x":8,"y":23}]','grid retains signed spacing and exact row-major points');
m=model({attachAfterHold:true});r=parsed(await clickToMove(m.ctx,{pid,x:300,y:200,region:{x:10,y:10,w:100,h:100},timeout_ms:5000,hold_ms:1000,poll_ms:50,stable_samples:2}));check(!r.ok&&r.hold_may_still_be_active&&!m.calls.some(x=>x[0]==='job-cancel')&&m.calls.filter(x=>x[0]==='json'&&JSON.parse(x[1]).cmd==='key').length===1,'attachment change after hold ACK avoids controlling old target and leaves native timeout bound');
console.log(`agent loop model: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
