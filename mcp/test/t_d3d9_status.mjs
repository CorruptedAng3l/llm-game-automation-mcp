import { diagnoseFrames, compactStatus } from '../build/status.js';
import { fullStatus, makeChecker, startFake } from './lib.mjs';
const {check,state}=makeChecker();
const stalled=fullStatus({frames:0,frame_api:'none',d3d9:{hooked:true,present_hooked:true,present_attempts:8,presented:0,device_lost_returns:8,last_present_hresult:0x88760868,render_warning:'D3D9 device lost: no successful target Present after 5 s; try args -windowed'},directinput_mouse:{poll_channel:'none',moves_since_poll:3,warning:'mouse has not polled DirectInput since at least three moves'}},{present:8});
check(!diagnoseFrames(fullStatus({frames:9,frame_api:'d3d9'},{present:0})).nothingPresented,'actual counted frames override legacy all-API present counter');
check(diagnoseFrames(stalled).nothingPresented&&diagnoseFrames(stalled).warning.includes('device lost'),'failed Present attempts never become rendered frames');
check(diagnoseFrames({counters:{present:0}}).nothingPresented,'older status without frames retains legacy fallback');
const view=compactStatus(stalled);
check(view.d3d9.device_lost_returns===8&&view.directinput_mouse.poll_channel==='none'&&view.warnings.some(x=>x.includes('mouse has not polled')),'compact status exposes D3D9 and unconsumed DI mouse diagnostics');
const t=await startFake({replies:{status:stalled,launch:{ok:true,pid:process.pid,injected:true}}});
try {
  let r=await t.call('status',{pid:process.pid});
  check(!r.isError&&r.json?.d3d9?.presented===0&&r.json?.warnings?.some(x=>x.includes('device lost')),'actual MCP compact status surfaces stalled rendering');
  r=await t.call('launch',{exe:'C:/fixture/game.exe',args:'-windowed'});
  check(!r.isError&&r.json?.nothing_presented===true&&r.json?.warnings?.some(x=>x.includes('-windowed')),'launch keeps running PID and warns on stalled rendering');
} finally {await t.close();}
console.log(`D3D9 status: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
