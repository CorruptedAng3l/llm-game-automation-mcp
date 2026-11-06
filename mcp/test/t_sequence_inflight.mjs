import {mkdtempSync,writeFileSync,readFileSync,rmSync} from 'node:fs';
import {join,resolve,dirname,basename} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import {guardSequence,canonicalSequenceScope} from '../build/sequenceconditions.js';
import {makeChecker} from './lib.mjs';
const {state,check}=makeChecker(),dir=mkdtempSync(join(tmpdir(),'vi-sequence-inflight-'));
const variables=['VINPUT_EXE','VINPUT_FAKE_LOG','VINPUT_FAKE_SCENARIO'],old=Object.fromEntries(variables.map(k=>[k,process.env[k]]));
const scope={hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:800,client_h:600,geometry_witness:'owned fixture'};
const job={ok:true,accepted:true,job_id:'123:456:1',job_kind:'sequence',job_state:'queued',job_complete:false,owner:'test',requested_ms:5000,input_injected:null,capture_scope_checked:true,sequence_guard_version:1,observer_guarded:true,observer_guard_ms:4000,guard_observation_seq:0};
const initial={ok:true,json:job,stderr:'',timedOut:false,exitCode:0,argv:[]};
const logfile=join(dir,'calls.jsonl');writeFileSync(logfile,'');const scenario=join(dir,'scenario.json');writeFileSync(scenario,JSON.stringify({replies:{seq:job},guard_reply_patch:{_delay_ms:400,returned_native_tag:'retain cancelled ACK'}}));
process.env.VINPUT_EXE=fileURLToPath(new URL('./fake_sequence_conditions.mjs',import.meta.url));process.env.VINPUT_FAKE_LOG=logfile;process.env.VINPUT_FAKE_SCENARIO=scenario;
try{
 const controller=new AbortController(),ctx={pid:123,owner:'test',prefix:['--pid','123','--owner','test'],epoch:0,currentEpoch:()=>0,signal:controller.signal};
 const baseline={scope:canonicalSequenceScope(scope),hash:'a'.repeat(16),qpc:'1000',frequency:'1000',frame_qpc_ticks:null,frame_index:null,receipt:{}};
 const pending=guardSequence(initial,ctx,{scope:baseline.scope,abort:baseline,observations:1,options:{abort_on:{kind:'frame_change'}}},3000),until=Date.now()+2000;
 while(!readFileSync(logfile,'utf8').trim()&&Date.now()<until)await new Promise(resolve=>setTimeout(resolve,5));controller.abort();const result=await pending,d=result.json.sequence_guard.observer_diagnostics;
 const good=!result.ok&&d?.entries[0]?.predicates.request_live===false&&d.entries[0].receipt.returned_native_tag==='retain cancelled ACK'&&d.entries[0].reconciliation.attempted===false;
 if(!good)console.error('INFLIGHT_DIAGNOSTIC '+JSON.stringify({result,calls:readFileSync(logfile,'utf8')}));check(good,'in-flight request cancellation preserves the returned native ACK while refusing follow-up authority');
 const calls=readFileSync(logfile,'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
 check(calls.length===1&&JSON.parse(calls[0].argv.at(-1)).cmd==='sequence-observe'&&JSON.stringify(result.json.sequence_input)===JSON.stringify(job),'cancelled observer sends one original control, no reconciliation/abort retry, and retains admitted input');
}finally{for(const key of variables){if(old[key]===undefined)delete process.env[key];else process.env[key]=old[key];}const target=resolve(dir);if(dirname(target)!==resolve(tmpdir())||!basename(target).startsWith('vi-sequence-inflight-'))throw new Error('refused cleanup outside owned inflight test directory');rmSync(target,{recursive:true,force:true});}
console.log(`sequence inflight: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
