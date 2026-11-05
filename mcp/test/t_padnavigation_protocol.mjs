import {resolve,join} from 'node:path';
import {writeFileSync,rmSync} from 'node:fs';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
const {check,state}=makeChecker(),pid=process.pid,t=await startFake({padnavigation:{}},{VINPUT_EXE:resolve('test/fake_padnavigation.mjs')});
const args={pid,direction:'up',region:[10,20,100,80],timeout_ms:2200,grace_ms:0,still_ms:100,poll_ms:20};
const holds=()=>t.callsOf('json').filter(a=>JSON.parse(cmdOf(a)[1]).cmd==='pad');
const captures=()=>t.callsOf('json').filter(a=>JSON.parse(cmdOf(a)[1]).cmd==='frame-hash');
function scenario(options){t.setScenario({padnavigation:options});rmSync(join(t.dir,'scenario.json.state'),{force:true});t.clearCalls();}
try{
 const list=await t.client.listTools(),tool=list.tools.find(x=>x.name==='pad_move_until_stuck');
 check(tool?.inputSchema.required.includes('region')&&tool.inputSchema.properties.compact?.type==='boolean','actual server exposes required scoped region and global compact option');
 let r=await t.call('pad_move_until_stuck',args);
 check(!r.isError&&r.json.ok&&r.json.stopped==='no_motion'&&r.json.release_confirmed&&r.json.hold_started,'actual MCP protocol stops stable pixels with one confirmed exact-job release');
 check(holds().length===1&&t.callsOf('job-cancel').length===1&&!t.callsOf('release').length&&!t.callsOf('down').length,'protocol sends one finite native hold, one cancellation and no global release/replay');
 check(JSON.parse(cmdOf(holds()[0])[1]).expected_capture_scope.includes('geometry_witness')&&captures().every(a=>JSON.parse(cmdOf(a)[1]).region==='10,20,100,80'),'tuple region normalizes and exact scope token fences the native job');
 scenario({noise:true});r=await t.call('pad_move_until_stuck',{...args,compact:true,capture_after:0,max_width:160});
 check(!r.isError&&r.json.compact&&r.json.stopped==='no_motion'&&r.json.motion_score.measured&&r.json.displacement===null,'compact MCP reply retains successful no-motion and unknown displacement');
 check(!!r.image&&r.json.capture_after.path&&t.callsOf('capture').length===1&&holds().length===1,'post-action image keeps shared capture path without repeating input');
 scenario({queued:true});r=await t.call('pad_move_until_stuck',{...args,timeout_ms:500});
 check(!r.isError&&r.json.stopped==='timeout'&&!r.json.hold_started&&r.json.samples===0&&captures().length===1&&t.callsOf('job-cancel').length===1,'queued protocol job times out without admitting pixel verdicts',r.text);
 scenario({completed:true});r=await t.call('pad_move_until_stuck',args);
 check(!r.isError&&r.json.stopped==='native_completed'&&r.json.release_confirmed&&!t.callsOf('job-cancel').length,'natural terminal protocol receipt needs no cancellation');
 for(const option of ['captureFailure','scopeChange']){
  scenario({[option]:true});r=await t.call('pad_move_until_stuck',args);check(r.isError&&r.json.stopped==='observation_failed'&&r.json.release_confirmed&&t.callsOf('job-cancel').length===1&&holds().length===1,option+' preserves the admission and confirms one exact cleanup without replay');
 }
 scenario({moving:true});r=await t.call('pad_move_until_stuck',{...args,timeout_ms:600});
 check(!r.isError&&r.json.stopped!=='no_motion'&&r.json.release_confirmed,'varying frames never become an MCP no-motion verdict',r.text);
 scenario({manual:true});r=await t.call('pad_move_until_stuck',args);check(r.isError&&!holds().length&&!r.json.admission_attempted,'current human ownership refuses before input');
 scenario({busy:true});r=await t.call('pad_move_until_stuck',args);check(r.isError&&!holds().length&&!r.json.admission_attempted&&!t.callsOf('job-cancel').length,'existing native pad job refuses before input or takeover');
 scenario({old:true});r=await t.call('pad_move_until_stuck',args);check(r.isError&&!holds().length&&!r.json.admission_attempted,'old native pad capability refuses before admission');
 scenario({});r=await t.call('pad_move_until_stuck',{...args,region:undefined});check(r.isError&&t.calls().length===0,'missing region fails schema validation before native calls');
 r=await t.call('pad_move_until_stuck',{...args,still_ms:0});check(r.isError&&t.calls().length===0,'invalid stability timing fails schema validation before native calls');
}finally{await t.close();}
console.log(`pad navigation protocol: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
