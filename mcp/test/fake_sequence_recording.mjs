// Native-shaped scoped recording RPC only; existing fixture owns sequence ACKs.
import {appendFileSync,readFileSync,writeFileSync} from 'node:fs';
import {encodePng} from '../build/png.js';
const argv=process.argv.slice(2),i=argv.indexOf('json');let request;try{request=JSON.parse(argv[i+1]);}catch{}
if(request?.cmd!=='capture')await import('./fake_sequence_conditions.mjs');
else{
 const scenario=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8')),log=process.env.VINPUT_FAKE_LOG,before=readFileSync(log,'utf8').split('\n').filter(Boolean).map(JSON.parse),n=before.filter(x=>{try{return JSON.parse(x.argv[x.argv.indexOf('json')+1]).cmd==='capture';}catch{return false;}}).length;
 appendFileSync(log,JSON.stringify({argv,t:Date.now()})+'\n');
 let out=scenario.recordings?.[Math.min(n,scenario.recordings.length-1)]??{ok:true};
 if(out._delay_ms)await new Promise(r=>setTimeout(r,out._delay_ms));
 if(out.ok!==false){
  const baseline=scenario.observations[0];
  writeFileSync(request.path,out.oversize?Buffer.alloc(4*1024*1024+1):encodePng({width:1,height:1,data:Buffer.from([40+n,80,120,255])}));
  out={ok:true,path:request.path,w:1,h:1,source_w:1,source_h:1,region_client_w:1,region_client_h:1,origin_x:0,origin_y:0,capture_scope:baseline.capture_scope,capture_space_updated:false,timing_clock:'QueryPerformanceCounter',qpc_frequency:baseline.qpc_frequency,capture_qpc_ticks:String(BigInt(baseline.capture_qpc_ticks)+BigInt(10+n)),frame_qpc_ticks:String(BigInt(baseline.capture_qpc_ticks)+BigInt(9+n)),method:'swapchain',captured_present:10+n,...out};
 }
 const {_delay_ms,_exit,oversize,...json}=out;process.stdout.write(JSON.stringify(json)+'\n');process.exit(_exit??(json.ok===false?1:0));
}
