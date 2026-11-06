// Camera readback protocol fixture. No DLL, Windows input, captures or camera.
import {appendFileSync,existsSync,readFileSync,writeFileSync} from 'node:fs';
const argv=process.argv.slice(2),rest=[];let pid=0;
for(let n=0;n<argv.length;n++){if(argv[n]==='--json')continue;if(argv[n]==='--pid'){pid=Number(argv[++n]);continue;}if(argv[n]==='--owner'){n++;continue;}rest.push(argv[n]);}
const scene=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8')),a=scene.readback??{},file=process.env.VINPUT_FAKE_SCENARIO+'.readback';
let state=existsSync(file)?JSON.parse(readFileSync(file,'utf8')):{};
if(state.case_id!==a.case_id)state={case_id:a.case_id,frames:0,post_frames:0,moves:0};
const ticks=()=>process.hrtime.bigint()/1000n; // one shared monotonic clock, microseconds; never a callback-ordinal clock
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
let result,proof={},noJson=false;
if(rest[0]==='status')result={ok:true,pid,capture_scope_version:1,relative_scope_version:1,key_sequence_version:1,input_scope_version:1,cursor_guard:{owner_active:false},presentation_window:{version:1,process_creation_filetime:'134300000000000000'}};
if(rest[0]==='json'){
 const q=JSON.parse(rest[1]);
 if(q.cmd==='move'){
  state.moves++;state.move_qpc=ticks().toString();
  result={ok:true,capture_scope_checked:!a.bad_ack,raw_dx:q.dx,raw_dy:q.dy,unbounded:true,relative_accounting_version:1,device_dx:q.dx,device_dy:q.dy,relative_delivery:{last_raw_backend:'buffer',raw_submission_exact:true,consumption_confirmed:false}};
  proof={event:'modeled_move',qpc_frequency:'1000000',move_qpc:state.move_qpc};
 }else if(q.cmd==='frame-hash'){
  const now=ticks(),post=state.moves>0;state.frames++;if(post)state.post_frames++;
  const elapsed=post?Number(now-BigInt(state.move_qpc))/1000:null;
  const onset=a.onset_ms??300,smooth=a.smoothing_ms??150;
  const phase=!post||a.static||elapsed<onset?'A':elapsed<onset+smooth/2?'B':elapsed<onset+smooth?'C':'D';
  let qpc=now,index=Number(now/1000n),frameQpc=now-1n;
  if(post&&a.fault==='repeat_clock')qpc=BigInt(state.last_qpc);
  if(post&&a.fault==='regress_clock')qpc=BigInt(state.first_qpc)-1n;
  if(post&&a.fault==='repeat_frame')index=state.pre_move_index;
  frameQpc=qpc-1n;
  const region=q.region?q.region.split(',').map(Number):[0,0,800,600];
  const print=a.printwindow===true;
  result={ok:true,method:print?'printwindow':'swapchain',capture_path:print?'printwindow':'d3d11',capture_scope:post&&a.fault==='scope'?{...scope,geometry_epoch:'1'}:scope,capture_space_updated:false,
   timing_clock:'QueryPerformanceCounter',qpc_frequency:post&&a.fault==='frequency'?'2000000':'1000000',capture_qpc_ticks:qpc.toString(),frame_qpc_ticks:print?null:frameQpc.toString(),captured_present:print?7:index,
   origin_x:region[0],origin_y:region[1],region_client_w:region[2],region_client_h:region[3],hash:phase.repeat(16),...(q.motion_sample?{motion_sample_version:1,motion_sample_w:2,motion_sample_h:2,motion_sample_rgb:(phase==='A'?'000000':phase==='B'?'303030':phase==='C'?'606060':'909090').repeat(4)}:{})};
  proof={event:'modeled_frame',post_move:post,modeled_elapsed_ms:elapsed,phase,qpc_frequency:result.qpc_frequency,capture_qpc_ticks:result.capture_qpc_ticks,frame_qpc_ticks:result.frame_qpc_ticks,frame_index:result.captured_present};
  state.first_qpc??=qpc.toString();state.last_qpc=qpc.toString();if(!post)state.pre_move_index=index;
  if(post&&a.fault==='capture')result={ok:false,error:'modeled post-ACK capture failure'};
  if(post&&a.fault==='transport')noJson=true;
 }
}
if(result){
 writeFileSync(file,JSON.stringify(state));appendFileSync(process.env.VINPUT_FAKE_LOG,JSON.stringify({argv,t:Date.now(),...proof})+'\n');
 if(noJson){console.log('modeled transport returned no JSON');process.exit(1);}
 console.log(JSON.stringify(result));process.exit(result.ok?0:1);
}
await import('./fake_vinput.mjs');
