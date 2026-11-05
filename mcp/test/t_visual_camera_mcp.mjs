import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StdioClientTransport} from '@modelcontextprotocol/sdk/client/stdio.js';
import {mkdtempSync,writeFileSync,readFileSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),dir=mkdtempSync(join(tmpdir(),'visual-camera-'));
const log=join(dir,'calls.jsonl'),scenario=join(dir,'scenario.json');
function reset(options={}){writeFileSync(log,'');writeFileSync(scenario,JSON.stringify({camera:{look:true,...options}}));writeFileSync(scenario+'.state',JSON.stringify({hashReads:0,duration:5000,target:[300,250]}));}
reset();
const transport=new StdioClientTransport({command:process.execPath,args:[process.env.CAMERA_HELPER_SERVER??resolve('build/index.js')],stderr:'pipe',env:{...process.env,VINPUT_EXE:resolve('test/fake_camera.mjs'),VINPUT_FAKE_LOG:log,VINPUT_FAKE_SCENARIO:scenario,VINPUT_OWNER:'mcp-test',VINPUT_CAPTURE_DIR:dir}});
const client=new Client({name:'visual-camera-fixture',version:'1'});await client.connect(transport);
const calls=()=>readFileSync(log,'utf8').split('\n').filter(Boolean).map(x=>JSON.parse(x).argv);
const moves=()=>calls().filter(a=>{const i=a.indexOf('json');return i>=0&&JSON.parse(a[i+1]).cmd==='move'});
const call=async(name,args)=>{const r=await client.callTool({name,arguments:{pid:process.pid,poll_ms:50,stable_samples:2,timeout_ms:10000,...args}});let j;try{j=JSON.parse(r.content.find(c=>c.type==='text').text)}catch{}return {r,j}};
try {
 const list=await client.listTools(),registered=['observe_settle','look_calibrate','look_at','look_turn'].every(name=>list.tools.some(t=>t.name===name));
 check(registered,'actual MCP exposes observation, calibration and reidentified look helpers');
 if(registered){
  let {r,j}=await call('observe_settle',{region:[10,20,100,80]});
  check(!r.isError&&j.settled&&j.observation.queue_drain_measured===false&&!moves().length,'read-only observation keeps raw drain unknown and injects no motion');
  ({r,j}=await call('look_calibrate',{template:'landmark.png',region:[10,20,700,500],counts:80}));
  const calibration=j.calibration;
  check(!r.isError&&j.calibration.matrix.xx===-.5&&j.calibration.matrix.yy===-.5&&moves().length===2,'actual MCP calibration submits exactly two probes and computes signed local response');
  check(calls().filter(a=>a.includes('find-image')).every(a=>a.includes('--preserve-map'))&&j.observations.every(o=>o.sample.capture_scope),'map preservation and scoped observations reach the CLI protocol');
  reset();({r,j}=await call('look_at',{template:'landmark.png',calibration,max_counts_per_step:150,capture_after:0,max_width:160}));
  check(!r.isError&&j.centered&&moves().length===2&&r.content.some(c=>c.type==='image'),'actual MCP correction reidentifies its target and returns standard post-action image');
  check(moves().every(a=>{const i=a.indexOf('json'),q=JSON.parse(a[i+1]);return q.raw===true&&q.steps===1&&typeof q.expected_capture_scope==='string'&&Math.abs(q.dx)<=150&&Math.abs(q.dy)<=150}),'all actual MCP corrections retain native scope and count bounds');
  reset({ambiguous:true});({r,j}=await call('look_calibrate',{template:'landmark.png'}));
  check(r.isError&&!j.ok&&!moves().length,'ambiguous visual template refuses before any actual MCP movement');
  reset({oldCli:true});({r,j}=await call('look_calibrate',{template:'landmark.png'}));
  check(r.isError&&!moves().length&&!calls().some(a=>a.includes('find-image')),'old CLI is rejected before a map-changing template capture');
  reset({oldDll:true});({r,j}=await call('look_calibrate',{template:'landmark.png'}));
  check(r.isError&&!moves().length&&!calls().some(a=>a.includes('find-image')),'old native motion capability refuses before target capture or movement');
  reset({manualOwner:true});({r,j}=await call('look_calibrate',{template:'landmark.png'}));
  check(r.isError&&!moves().length&&!calls().some(a=>a.includes('find-image')),'current native nested user ownership refuses before movement');
  reset({manualOwner:true});({r,j}=await call('observe_settle',{}));
  check(!r.isError&&j.settled&&!moves().length,'read-only helper still observes while the user owns the native window');
  reset({noProgress:true});({r,j}=await call('look_at',{template:'landmark.png',calibration}));
  check(r.isError&&moves().length===1&&j.error.includes('progress'),'actual MCP stops a nonresponsive landmark after one correction');
  reset();({r}=await call('look_at',{template:'landmark.png',calibration,max_steps:0}));
  check(r.isError&&calls().length===0,'invalid correction bounds reject before any CLI work');
  reset();const turn=await call('look_turn',{yaw_deg:90,counts_per_degree:12.5});
  check(!turn.r.isError&&turn.j.submitted_dx===1125&&turn.j.actual_yaw_deg===null&&moves().length===1&&!calls().some(a=>a.includes('find-image')),'actual MCP plain yaw submits caller counts once without landmark observations');
 }
}finally{await client.close()}
console.log(`VISUAL_CAMERA_RESULT ${JSON.stringify({completed:true,passed:state.passes,failed:state.failures,scope:'actual MCP server with stateful simulated CLI target; no real rendering, camera angles or raw-input-consumption measurement'})}`);
process.exit(state.failures?1:0);
