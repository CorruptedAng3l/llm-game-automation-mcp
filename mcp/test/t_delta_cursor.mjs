import {clickDeltaCursor} from '../build/agentloops.js';
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),pid=4242;
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600,geometry_witness:'owned'};
const read=r=>JSON.parse(r.content[0].text);
function fixture(options={}){
 const calls=[];let point=[50,50],moves=0,clicks=0;
 const ctx={owner:'cursor-test',at:()=>[],epoch:()=>0,captureError:()=>undefined,finish:async(_p,r)=>r,map:async(_p,_a,pts)=>({pts,mapped:false}),run:async(argv)=>{
  calls.push(argv);let json;
  if(argv[0]==='status')json={ok:true,pid,capture_scope_version:1,pointer_scope_version:1,input_scope_version:1,input_admission_version:1,relative_scope_version:1,virtual_down:[],owner_input_count:options.ownerAfter&&moves?1:0};
  else if(argv[0]==='jobs')json={ok:true,jobs:options.busy?[{job_complete:false}]:[]};
  else if(argv[0]==='find-image')json={ok:true,capture_space_preserved:true,capture_scope:options.scopeAfter&&moves?{...scope,geometry_epoch:'1'}:scope,match_count:options.missing?0:options.ambiguous?2:1,matches:options.missing?[]:[{center:point,score:.99}]};
  else if(argv[0]==='json'){
   const r=JSON.parse(argv[1]);
   if(r.cmd==='frame-hash')json={ok:true,capture_space_updated:false,capture_scope:scope,hash:'1234567890abcdef',timing_clock:'QueryPerformanceCounter',capture_qpc_ticks:'1000000',qpc_frequency:'10000000'};
   else if(r.cmd==='move'){moves++;if(!options.stuck)point=[point[0]+r.dx*(options.reverse?-1.5:1.5),point[1]+r.dy*(options.reverse?-1.5:1.5)];json={ok:true,capture_scope_checked:!options.badMove,unbounded:true,raw_dx:r.dx,raw_dy:r.dy};}
   else if(r.cmd==='click'){clicks++;json={ok:!options.badClick,capture_scope_checked:true,input_admission_version:1,admission_deadline_qpc_ticks:r.admit_before_qpc_ticks,button_down_count:1,button_released:true};}
  }
  if(!json)throw new Error('unexpected '+JSON.stringify(argv));return {ok:json.ok,json,argv,stderr:'',timedOut:false,exitCode:0};
 }};
 return {ctx,calls,get moves(){return moves},get clicks(){return clicks},get point(){return point}};
}
const args={pid,x:200,y:125,cursor_template:'cursor.png',counts_per_pixel_x:2/3,counts_per_pixel_y:2/3,settle_ms:0,timeout_ms:5000,max_counts_per_step:50};
let f=fixture(),r=read(await clickDeltaCursor(f.ctx,args));
check(r.ok&&r.cursor_aligned&&f.moves===2&&f.clicks===1,'positive cursor gain converges then clicks exactly once');
check(r.observations.slice(-2).every(o=>o.error_px<=3),'two cursor hotspot observations confirm alignment');
const commands=f.calls.filter(a=>a[0]==='json').map(a=>JSON.parse(a[1]));
check(commands.filter(c=>c.cmd==='move').every(c=>c.raw&&Math.abs(c.dx)<=50&&Math.abs(c.dy)<=50&&c.expected_capture_scope===JSON.stringify(scope)),'every cursor correction uses bounded scoped raw deltas');
check(commands.filter(c=>c.cmd==='click').every(c=>c.x===undefined&&c.y===undefined&&c.count===1),'final click never substitutes virtual absolute coordinates for the rendered cursor');
for(const mode of ['missing','ambiguous','stuck','badMove','ownerAfter','scopeAfter','busy']){
 f=fixture({[mode]:true});r=read(await clickDeltaCursor(f.ctx,args));check(!r.ok&&f.clicks===0,mode+' stops before a click');
}
f=fixture({reverse:true});r=read(await clickDeltaCursor(f.ctx,{...args,counts_per_pixel_x:-2/3,counts_per_pixel_y:-2/3}));check(r.ok&&f.clicks===1,'signed gains support reversed cursor axes');
f=fixture();r=read(await clickDeltaCursor(f.ctx,{...args,counts_per_pixel_x:0}));check(!r.ok&&!f.moves&&!f.clicks,'zero gain refuses before input');
f=fixture({badClick:true});r=read(await clickDeltaCursor(f.ctx,args));check(!r.ok&&f.clicks===1&&r.click_receipt,'failed click retains receipt and is never repeated');
f=fixture();r=read(await clickDeltaCursor(f.ctx,{...args,x:800}));check(!r.ok&&!f.moves&&!f.clicks,'out-of-client target refuses before movement');
console.log(`delta cursor: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
