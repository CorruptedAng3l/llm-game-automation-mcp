import {mkdtempSync,writeFileSync,readFileSync,existsSync,rmSync} from 'node:fs';
import {join,resolve,dirname,basename} from 'node:path';
import {tmpdir} from 'node:os';
import {SequenceCaptures,prepareCapturePlan,sequenceFollowAction} from '../build/sequencecaptures.js';
import {encodePng,decodePng} from '../build/png.js';
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),dir=mkdtempSync(join(tmpdir(),'vi-sequence-record-')),pid=123;
const ctx={pid,owner:'test',prefix:[],epoch:0,currentEpoch:()=>0},scope={identity:'one'},scopeKey=v=>v?.identity==='one'?'one':undefined,before={job_id:'123:456:1',job_state:'running',job_complete:false,requested_ms:1000,sequence_step:2};
const plan=prepareCapturePlan({capture_every_ms:250,capture_max_frames:1,capture_out_dir:dir});
let calls=[];
const mock=(patch={},mutate)=>async argv=>{const request=JSON.parse(argv[1]);calls.push(request);writeFileSync(request.path,patch.oversize?Buffer.alloc(4*1024*1024+1):encodePng({width:1,height:1,data:Buffer.from([50,80,120,255])}));mutate?.();return {ok:patch.ok!==false,json:{ok:true,path:request.path,w:1,h:1,region_client_w:2,region_client_h:3,origin_x:4,origin_y:5,capture_scope:scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',qpc_frequency:'1000',capture_qpc_ticks:'1010',frame_qpc_ticks:'1009',method:'swapchain',captured_present:17,...patch},argv,stderr:'',timedOut:false,exitCode:0};};
try{
 check(prepareCapturePlan({})===undefined,'omitted recording leaves sequence behavior unchanged');
 for(const args of [{capture_max_frames:1},{capture_every_ms:249},{capture_every_ms:30001},{capture_every_ms:250,capture_max_frames:33},{capture_every_ms:250,capture_max_width:961},{capture_every_ms:250,capture_out_dir:''}]){let rejected=false;try{prepareCapturePlan(args);}catch{rejected=true;}check(rejected,'invalid recording plan refuses before native input: '+JSON.stringify(args));}
 let recorder=new SequenceCaptures(ctx,plan,'one','1000',scopeKey,mock());check(recorder.due(performance.now()),'first observed running job can be recorded immediately');
 let frame=await recorder.capture(before,1000);recorder.confirm(frame,{...before,sequence_step:3,job_state:'completed',job_complete:true});let view=recorder.finish();
 check(view.frames.length===1&&existsSync(frame.path)&&frame.capture_qpc_ticks==='1010'&&frame.frame_qpc_ticks==='1009'&&frame.frame_index===17,'retained PNG carries exact native frame clock/index provenance');
 check(frame.job_id===before.job_id&&frame.step_index_before===2&&frame.step_index_after===3&&frame.bracket_confirmed&&frame.job_complete_after,'different native step witnesses bracket the independent capture without an exact-step claim');
 check(frame.mapping.scale===0.5&&frame.mapping.scale_y===1/3&&frame.mapping.offset_x===4&&frame.mapping.offset_y===5,'saved frame has exact per-axis client mapping');
 check(calls[0].observation_only&&calls[0].preserve_capture_space&&calls[0].timeout_ms<=500&&calls[0].after_frames===0,'recording is bounded observation-only and preserves pointer mapping');
 check(!recorder.due(performance.now()+10000)&&view.capped&&calls.length===1,'finite frame cap stops capture without affecting native sequence input');
 const sheet=decodePng(readFileSync(view.contact_sheet.path));check(sheet.width===320&&sheet.height===240&&view.contact_sheet.coordinate_system==='contact_sheet_not_client_pixels','review sheet comes from retained frames and names its separate coordinates');
 for(const patch of [{ok:false,error:'capture failed'},{capture_scope:{identity:'other'}},{qpc_frequency:'999'},{capture_space_updated:true},{path:'wrong'},{w:2},{oversize:true}]){
  calls=[];recorder=new SequenceCaptures(ctx,plan,'one','1000',scopeKey,mock(patch));frame=await recorder.capture(before,1000);view=recorder.finish();check(!frame&&view.frames.length===0&&view.recording_disabled&&view.warnings[0].includes('do not replay')&&calls.length===1&&!existsSync(calls[0].path),'bad/oversized capture disables recording with an advisory and removes only its owned file: '+JSON.stringify(patch));
 }
 let epoch=0;recorder=new SequenceCaptures({...ctx,currentEpoch:()=>epoch},plan,'one','1000',scopeKey,mock({},()=>epoch=1));frame=await recorder.capture(before,1000);check(!frame&&recorder.finish().recording_disabled,'attachment changes invalidate recording without controlling another target');
 recorder=new SequenceCaptures(ctx,plan,'one','1000',scopeKey,mock(), '1020');frame=await recorder.capture(before,1000);check(!frame&&recorder.finish().recording_disabled,'frame QPC older than pre-admission scoped baseline is unavailable');
 recorder=new SequenceCaptures(ctx,plan,'one','1000',scopeKey,mock());frame=await recorder.capture(before,1000);check(!frame.bracket_confirmed&&frame.step_index_after===null,'missing post-capture witness stays explicitly unconfirmed');
 let mismatch=false;try{recorder.confirm(frame,{...before,job_id:'123:456:2'});}catch{mismatch=true;}check(mismatch&&!frame.bracket_confirmed,'different native generation cannot confirm the saved frame bracket');
 let action=sequenceFollowAction({ok:true,accepted:true,job_complete:false,job_id:before.job_id},pid);check(action.tool==='job_status'&&action.arguments.id===before.job_id&&action.note.includes('future new sequence')&&action.note.includes('do not run it again'),'async hint follows the admitted exact job and names wait:true only for future input');
 for(const patch of [{accepted:false},{job_complete:true},{job_id:'123:18446744073709551616:1'},{job_id:'other'},{ok:false}])check(!sequenceFollowAction({ok:true,accepted:true,job_complete:false,job_id:before.job_id,...patch},pid),'invalid/terminal async result does not suggest a new continuation: '+JSON.stringify(patch));
}finally{const target=resolve(dir);if(dirname(target)!==resolve(tmpdir())||!basename(target).startsWith('vi-sequence-record-'))throw new Error('refused scratch cleanup outside the owned test directory');rmSync(target,{recursive:true,force:true});}
console.log(`sequence captures: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
