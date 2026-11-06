import {mkdtempSync,mkdirSync,writeFileSync,existsSync,readdirSync,rmSync} from 'node:fs';
import {join,resolve,dirname,basename} from 'node:path';
import {tmpdir} from 'node:os';
import {SequenceCaptures,prepareCapturePlan} from '../build/sequencecaptures.js';
import {encodePng} from '../build/png.js';
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),dir=mkdtempSync(join(tmpdir(),'vi-sequence-ring-')),scope={id:'one'},key=v=>v?.id==='one'?'one':undefined;
const ctx={pid:123,owner:'test',prefix:[],epoch:0,currentEpoch:()=>0},before={job_id:'123:456:1',job_state:'running',job_complete:false,requested_ms:5000,sequence_step:0};
let requests=[],serial=0,fail=false;
const run=async argv=>{const request=JSON.parse(argv[1]);requests.push(request);serial++;writeFileSync(request.path,encodePng({width:1,height:1,data:Buffer.from([serial,20,30,255])}));return {ok:!fail,json:{ok:!fail,path:request.path,w:1,h:1,region_client_w:2,region_client_h:3,origin_x:serial,origin_y:5,capture_scope:scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',qpc_frequency:'1000',capture_qpc_ticks:String(1000+serial),frame_qpc_ticks:String(999+serial),method:'swapchain',captured_present:serial},argv,stderr:'',timedOut:false,exitCode:fail?1:0};};
try{
 const plan=prepareCapturePlan({capture_every_ms:250,capture_mode:'last',capture_max_frames:2,capture_out_dir:dir});
 const sentinel=join(dir,'caller-owned.png');writeFileSync(sentinel,'do not delete');
 let recorder=new SequenceCaptures(ctx,plan,'one','1000',key,run),all=[];
 for(let n=0;n<4;n++){check(recorder.due(performance.now()+n*1000),'last-N recording continues beyond its retention count');const f=await recorder.capture({...before,sequence_step:n},1000);recorder.confirm(f,{...before,sequence_step:n+1});all.push(f);}
 let view=recorder.finish();
 check(view.mode==='last'&&view.recorded_frames===2&&view.total_recorded_frames===4&&view.evicted_frames===2&&!view.capped,'ring retains last N and reports total produced/evicted without stopping the observer');
 check(view.frames.map(f=>f.index).join()==='2,3'&&view.frame_order==='oldest_to_newest'&&view.frames.map(f=>f.capture_qpc_ticks).join()==='1003,1004','retained frames preserve monotonic original indices, exact QPC and explicit oldest-to-newest order');
 check(view.frames[0].step_index_before===2&&view.frames[1].step_index_after===4&&view.frames.every(f=>f.bracket_confirmed),'last frames keep exact before/after job-step brackets');
 check(view.frames[0].mapping.offset_x===3&&view.frames[1].mapping.offset_x===4,'each retained image keeps its own actual mapping rather than the newest mapping');
 check(!existsSync(all[0].path)&&!existsSync(all[1].path)&&existsSync(all[2].path)&&existsSync(all[3].path)&&existsSync(sentinel),'only oldest generated ring files are evicted; caller files remain untouched');
 check(dirname(all[3].path)!==dir&&dirname(dirname(all[3].path))===dir&&readdirSync(dirname(all[3].path)).length===3,'ring frames and review sheet stay inside one unique owned run subdirectory');
 check(/^r_[0-9a-f]{32}$/.test(basename(dirname(view.frames[0].path)))&&view.frames.every(f=>/^f_[0-9a-f]{32}\.png$/.test(basename(f.path)))&&/^s_[0-9a-f]{32}\.png$/.test(basename(view.contact_sheet.path))&&[...view.frames.map(f=>f.path),view.contact_sheet.path].every(p=>p.length-plan.directory.length===74),'last-mode generated paths add only74 characters to the caller directory while retaining unique owned names');
 check(view.first_evicted_capture_qpc_ticks==='1001'&&view.last_evicted_capture_qpc_ticks==='1002'&&view.saved_bytes<=view.file_budget_bytes,'evicted clock range and finite retained disk budget remain explicit');
 check(!recorder.due(performance.now()+10000)&&await recorder.capture(before,1000)===undefined,'finished ring cannot create extra captures');
 const first=new SequenceCaptures(ctx,{...plan,mode:'first'},'one','1000',key,run);await first.capture(before,1000);const firstView=first.finish();
 check(dirname(firstView.frames[0].path)===dir&&basename(firstView.frames[0].path).startsWith('pid123_sequence_')&&basename(firstView.contact_sheet.path).startsWith('pid123_sequence_sheet_'),'first-mode original caller-directory filenames remain unchanged');
 fail=false;recorder=new SequenceCaptures(ctx,plan,'one','1000',key,run);const held=await recorder.capture(before,1000);fail=true;const rejected=await recorder.capture(before,1000);view=recorder.finish();
 check(!rejected&&view.recording_disabled&&view.failed_frames===1&&view.capture_attempts===2&&view.evicted_frames===0&&existsSync(held.path),'failed replacement preserves earlier frames and disables only recording, not native input');
 fail=false;recorder=new SequenceCaptures(ctx,{...plan,maxFrames:1},'one','1000',key,run);const original=await recorder.capture(before,1000),originalPath=original.path;original.path=sentinel;await recorder.capture(before,1000);view=recorder.finish();
 check(view.recording_disabled&&existsSync(sentinel)&&existsSync(originalPath),'path ownership mismatch refuses eviction of caller-supplied or mutated paths');
 const notAFile=async argv=>{const request=JSON.parse(argv[1]);mkdirSync(request.path);writeFileSync(join(request.path,'caller-content'),'retain');return {ok:false,json:{ok:false,error:'test non-file path'},argv,stderr:'',timedOut:false,exitCode:1};};
 recorder=new SequenceCaptures(ctx,plan,'one','1000',key,notAFile);await recorder.capture(before,1000);view=recorder.finish();
 check(view.recording_disabled&&!view.file_budget_confirmed&&view.saved_bytes===null&&view.orphaned_generated_paths.length===1&&existsSync(join(view.orphaned_generated_paths[0].path,'caller-content'))&&!view.contact_sheet,'failed nonrecursive cleanup records unknown bytes and refuses further writes without deleting directory contents');
 for(const options of [{capture_mode:'last'},{capture_every_ms:250,capture_mode:'all'}]){let rejected=false;try{prepareCapturePlan(options);}catch{rejected=true;}check(rejected,'invalid/detached retention mode refuses before input');}
}finally{const target=resolve(dir);if(dirname(target)!==resolve(tmpdir())||!basename(target).startsWith('vi-sequence-ring-'))throw new Error('refused scratch cleanup outside owned ring test directory');rmSync(target,{recursive:true,force:true});}
console.log(`sequence ring: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
