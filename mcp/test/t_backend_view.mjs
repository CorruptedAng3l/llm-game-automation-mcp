import {compactRenderBackends} from '../build/backendview.js';
import {compactStatus} from '../build/status.js';
import {makeChecker,startFake} from './lib.mjs';
const {state,check}=makeChecker(),idle={hooked:true,present_hooked:true,presented:0,present_attempts:0,last_present_hresult:'0x00000000',render_warning:''};
let v=compactRenderBackends({frame_api:'d3d11',d3d9:idle,ddraw:{hooked:true},inject:{d3d9:idle},anticheat_refused:true});
check(!v.d3d9&&!v.ddraw&&!v.inject.d3d9&&v.anticheat_refused,'known other renderer omits idle legacy blocks recursively and preserves refusal');
check(compactRenderBackends({d3d9:idle}).d3d9,'unknown renderer retains diagnostics');
check(compactRenderBackends({frame_api:'d3d9',d3d9:idle}).d3d9,'active legacy renderer retains diagnostics');
for(const patch of [{render_warning:'failed'},{last_present_hresult:'0x88760868'},{capture_failures:1},{presented:2},{blocked_before_first_present:true}])check(compactRenderBackends({frame_api:'d3d11',d3d9:{...idle,...patch}}).d3d9,'failure/activity block stays visible: '+JSON.stringify(patch));
check(!compactStatus({ok:true,frame_api:'d3d11',d3d9:idle,counters:{}}).d3d9,'default compact status removes unused d3d9 probe block');
const pid=process.pid,t=await startFake({replies:{launch:{ok:true,pid,frame_api:'d3d11',d3d9:idle,ddraw:{hooked:true},inject:{ok:true,pid,d3d9:idle}},status:{ok:true,pid,frame_api:'d3d11',d3d9:idle,counters:{}}}});
try{let r=await t.call('launch',{exe:'owned.exe',settle_s:0});check(!r.isError&&!r.json.d3d9&&!r.json.inject.d3d9,'MCP default launch removes unused backend diagnostics');r=await t.call('launch',{exe:'owned.exe',settle_s:0,verbose:true});check(!r.isError&&r.json.d3d9&&r.json.inject.d3d9,'MCP verbose launch retains raw backend diagnostics');}finally{await t.close();}
console.log(`backend view: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
