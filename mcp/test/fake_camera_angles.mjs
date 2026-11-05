// Camera profile protocol fixture; all input is modeled, never sent to Windows.
import {appendFileSync,readFileSync} from 'node:fs';
const argv=process.argv.slice(2),rest=[];let pid=0,owner='';
for(let i=0;i<argv.length;i++){if(argv[i]==='--json')continue;if(argv[i]==='--pid'){pid=Number(argv[++i]);continue;}if(argv[i]==='--owner'){owner=argv[++i];continue;}rest.push(argv[i]);}
const scene=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8')),a=scene.angles??{};
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:a.geometry??'0',lifetime_cookie:'3',client_w:800,client_h:600};
let result;
if(rest[0]==='status')result={ok:true,pid,capture_scope_version:1,key_sequence_version:1,input_scope_version:1,relative_scope_version:1,cursor_guard:{owner_active:a.owner===true},
 presentation_window:{version:1,process_creation_filetime:a.noCreation?undefined:a.created??'134300000000000000'}};
if(rest[0]==='json'){
 const q=JSON.parse(rest[1]);
 if(q.cmd==='frame-hash')result={ok:true,hash:'1111111111111111',capture_scope:scope};
 if(q.cmd==='move')result=a.refuseMove?{ok:false,input_injected:false,error:'fixture refusal'}:{ok:true,capture_scope_checked:!a.badAck,raw_dx:q.dx,raw_dy:q.dy,unbounded:true};
 if(q.cmd==='key')result=a.failKey?{ok:false,error:'fixture key acknowledgement lost'}:{ok:true,accepted:false,job_kind:'keys',job_id:`${pid}:100:1`,owner,requested_ms:q.ms,job_state:'completed',job_complete:true,hold_ready:false,capture_scope_checked:true,input_injected:true};
}
if(result){appendFileSync(process.env.VINPUT_FAKE_LOG,JSON.stringify({argv,t:Date.now()})+'\n');console.log(JSON.stringify(result));process.exit(result.ok?0:1);}
await import('./fake_vinput.mjs');
