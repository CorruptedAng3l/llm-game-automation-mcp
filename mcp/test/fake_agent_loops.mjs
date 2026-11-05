// Finite CLI protocol model only: never accesses a target/input/GUI API.
import {readFileSync,appendFileSync} from 'node:fs';
const argv=process.argv.slice(2),log=process.env.VINPUT_FAKE_LOG,scene=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8'));
const rest=[];let pid=0;for(let i=0;i<argv.length;i++){if(argv[i]==='--pid'){pid=Number(argv[++i]);continue;}if(argv[i]==='--owner'){i++;continue;}if(argv[i]==='--json')continue;rest.push(argv[i]);}
const cmd=rest[0],before=readFileSync(log,'utf8').split('\n').filter(Boolean).map(JSON.parse);
const parsed=a=>{try{return JSON.parse(a[a.indexOf('json')+1]);}catch{return {};}};
const requests=before.map(x=>parsed(x.argv)),lastMove=requests.filter(x=>x.cmd==='mouse').at(-1),lastKey=requests.filter(x=>x.cmd==='key').at(-1);
if(['status','jobs','job-status','job-cancel','json'].includes(cmd))appendFileSync(log,JSON.stringify({argv,t:Date.now()})+'\n');
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
let out;
const fields=r=>({capture_scope_checked:true,pointer_scope_version:1,input_admission_version:1,admission_deadline_qpc_ticks:r.admit_before_qpc_ticks});
const job=(r,state='running',accepted=false)=>({ok:accepted||state==='completed',accepted,job_id:`${pid}:444:1`,owner:'mcp-test',job_kind:'keys',job_state:state,job_complete:['completed','cancelled','failed'].includes(state),hold_ready:state==='running',requested_ms:r.ms,...fields(r)});
if(cmd==='status'){const n=before.filter(x=>x.argv.includes('status')).length;out={ok:true,pid,capture_scope_version:1,pointer_scope_version:1,input_scope_version:1,input_admission_version:1,owner_active:false,owner_input_count:0,virtual_down:[],vcur_x:lastMove?.x??400,vcur_y:lastMove?.y??300,...(scene.status_patch??{}),...(scene.status_patches?.[n]??{})};}
else if(cmd==='jobs')out={ok:true,jobs:scene.jobs??[]};
else if(cmd==='job-status'){const n=before.filter(x=>x.argv.includes('job-status')).length,state=scene.job_states?.[Math.min(n,scene.job_states.length-1)]??'running';out={ok:true,job:{...job(lastKey,state),...(scene.job_patch??{})}};}
else if(cmd==='job-cancel')out=scene.cancel_error?{ok:false,error:'modeled lost cancel ACK'}:{ok:true,job:{...job(lastKey,'cancelled'),job_reason:'explicit_cancel'}};
else if(cmd==='json'){
 const r=JSON.parse(rest[1]);
 if(r.cmd==='frame-hash'){const n=requests.filter(x=>x.cmd==='frame-hash').length,hashes=scene.hashes??['AAAAAAAAAAAAAAAA','BBBBBBBBBBBBBBBB','BBBBBBBBBBBBBBBB','BBBBBBBBBBBBBBBB'];out={ok:true,hash:hashes[Math.min(n,hashes.length-1)],capture_space_updated:false,capture_scope:scope,timing_clock:'QueryPerformanceCounter',qpc_frequency:'1000',capture_qpc_ticks:String(100000+n),...(scene.capture_patches?.[n]??{})};}
 else if(r.cmd==='click'){const n=requests.filter(x=>x.cmd==='click').length+1;out={ok:true,button_down_count:1,button_released:true,last_press_x:r.x,last_press_y:r.y,...fields(r),...(scene.click_patches?.[n]??{})};if(scene.delay_click_ms)await new Promise(done=>setTimeout(done,scene.delay_click_ms));}
 else if(r.cmd==='mouse')out={ok:true,x:r.x,y:r.y,...fields(r),...(scene.move_patch??{})};
 else if(r.cmd==='key')out={...job(r,'queued',true),...(scene.initial_patch??{})};
 else out={ok:false,error:'unexpected modeled native command'};
}else await import('./fake_vinput.mjs');
if(out){process.stdout.write(JSON.stringify(out)+'\n');process.exit(out.ok===false?1:0);}
