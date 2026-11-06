// Same actual MCP protocol controls against old/candidate servers; no native input.
import {readdirSync,mkdtempSync,rmSync} from 'node:fs';
import {join,resolve,dirname,basename} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid;
const images=directory=>readdirSync(directory,{withFileTypes:true}).flatMap(e=>e.isDirectory()?images(join(directory,e.name)):/\.(png|jpe?g)$/i.test(e.name)?[join(directory,e.name)]:[]).sort();
const t=await startFake({replies:{capture:{ok:true,w:1,h:1,method:'swapchain',capture_path:'d3d11'}}});
try{
 const before=images(t.dir);t.clearCalls();const r=await t.call('move_relative',{pid,dx:2,dy:-1,capture_after:0});
 check(!r.isError&&r.json.ok&&t.callsOf('move').length===1,'zero capture retains exactly one original relative action');
 check(!r.image&&!r.json.capture_after&&!t.callsOf('capture').length&&JSON.stringify(images(t.dir))===JSON.stringify(before),'capture_after:0 disables image/RPC/file creation on a successful relative action',JSON.stringify({reply:r.json,calls:t.calls(),files:images(t.dir)}));
 const families=[
  ['key',{key:'enter'},'key'],['click',{x:10,y:20},'click'],['click_text',{text:'Play'},'click-text'],
  ['move',{x:5,y:6},'mouse'],['drag',{x1:1,y1:2,x2:3,y2:4,hold_start_ms:0,hold_end_ms:0},'mouseup'],['scroll',{notches:1},'scroll'],
  ['pad',{buttons:'a',ms:1},'pad'],['pad_move',{direction:'right',ms:1},'pad'],
  ['pad_look',{degrees:90,calibration:{rx:20000,degrees_per_second:600}},'pad'],
  ['pad_drive',{ms:100,throttle:170},'json'],['pad_rotate',{stick:'left',start_deg:0,direction:'clockwise',turns:0.25,ms:300},'json'],
 ];
 for(const [name,args,command] of families){
  const before=images(t.dir);t.clearCalls();const r=await t.call(name,{pid,...args,capture_after:0});
  check(!r.isError&&r.json.ok&&t.callsOf(command).length===1,`${name}: explicit zero preserves the original action once`);
  check(!r.image&&!r.json.capture_after&&!t.callsOf('capture').length&&JSON.stringify(images(t.dir))===JSON.stringify(before),`${name}: zero cannot fall through to a tool-specific image default`,JSON.stringify({reply:r.json,calls:t.calls(),files:images(t.dir)}));
 }
 for(const [name,args,command,kind] of [['key',{key:'w',hold_ms:150,async:true},'key','keys'],['pad',{buttons:'a',ms:150,async:true},'pad','pad'],['key_sequence',{steps:[{action:'key',key:'w',ms:150}]},'seq','sequence']]){
  const queued={ok:true,accepted:true,job_id:`${pid}:100:1`,owner:'mcp-test',job_kind:kind,job_state:'queued',job_complete:false,requested_ms:150,input_injected:null};
  t.setScenario({replies:{[command]:queued}});t.clearCalls();const before=images(t.dir),r=await t.call(name,{pid,...args,capture_after:0});
  check(!r.isError&&r.json.job_id===queued.job_id&&r.json.job_complete===false&&t.callsOf(command).length===1,`${name}: zero retains the exact queued job without re-admission`);
  check(!r.image&&!r.json.capture_after&&!t.callsOf('capture').length&&JSON.stringify(images(t.dir))===JSON.stringify(before),`${name}: queued zero creates no capture RPC/image file`);
 }
 for(const value of ['0','false','instant',-1,0.5,3601,null]){
  t.clearCalls();const before=images(t.dir),r=await t.call('move_relative',{pid,dx:1,dy:0,capture_after:value});
  check(r.isError&&!t.calls().length&&JSON.stringify(images(t.dir))===JSON.stringify(before),`invalid capture_after ${JSON.stringify(value)} refuses before action, capture or image file`);
 }
}finally{await t.close();}
const initial={ok:true,accepted:true,job_id:`${pid}:101:1`,owner:'mcp-test',job_kind:'sequence',job_state:'queued',job_complete:false,requested_ms:150,input_injected:null};
const completed={...initial,accepted:false,job_state:'completed',job_complete:true,input_injected:true,transitions:2};
const wait=await startFake({replies:{status:{ok:true,pid,key_sequence_version:1},jobs:{ok:true,jobs:[]},seq:initial},sequence_observations:[{ok:true,job:completed}]},{VINPUT_EXE:fileURLToPath(new URL('./fake_sequence_wait.mjs',import.meta.url))});
try{
 const before=images(wait.dir),r=await wait.call('key_sequence',{pid,steps:[{action:'key',key:'w',ms:150}],wait:true,wait_timeout_ms:2000,capture_after:0});
 check(!r.isError&&r.json.job_id===initial.job_id&&r.json.sequence_wait.completed&&JSON.stringify(r.json.sequence_input)===JSON.stringify(initial)&&wait.callsOf('seq').length===1&&wait.callsOf('job-status').length===1,'waited sequence zero keeps exact completed and original admitted receipts');
 check(!r.image&&!r.json.capture_after&&!wait.callsOf('capture').length&&JSON.stringify(images(wait.dir))===JSON.stringify(before),'completed sequence zero never captures after terminal polling');
}finally{await wait.close();}
const cameraRoot=mkdtempSync(join(tmpdir(),'vi-capture-zero-camera-'));
const camera=await startFake({},{VINPUT_EXE:fileURLToPath(new URL('./fake_camera_angles.mjs',import.meta.url)),VINPUT_CAMERA_PROFILE_DIR:cameraRoot});
try{
 const calibration=await camera.call('look_calibrate',{pid,mode:'counts',counts_per_degree:2});
 check(!calibration.isError&&calibration.json.calibration.path.startsWith(cameraRoot),'zero camera controls use one exact-target owned calibration profile');
 for(const [name,args,keyCount] of [['turn',{degrees:90},0],['look',{yaw:90},0],['unstick',{degrees:60},3]]){
  const before=images(camera.dir);camera.clearCalls();const r=await camera.call(name,{pid,...args,capture_after:0});
  const native=camera.callsOf('json').map(a=>JSON.parse(cmdOf(a)[1]));
  check(!r.isError&&native.filter(q=>q.cmd==='move').length===1&&native.filter(q=>q.cmd==='key').length===keyCount,`${name}: zero retains each original scoped action exactly once`);
  check(!r.image&&!r.json.capture_after&&!camera.callsOf('capture').length&&JSON.stringify(images(camera.dir))===JSON.stringify(before),`${name}: camera finish treats zero as disabled without creating an image`);
 }
}finally{await camera.close();const target=resolve(cameraRoot);if(dirname(target)!==resolve(tmpdir())||!basename(target).startsWith('vi-capture-zero-camera-'))throw new Error('camera profile cleanup escaped owned fixture');rmSync(target,{recursive:true,force:true});}
console.log(`capture zero: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
