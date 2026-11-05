import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {makeChecker,startFake} from './lib.mjs';
const {check,state}=makeChecker();
const refusal={ok:false,launched:false,injected:false,already_running:true,automatic_attach_attempted:false,
  duplicate_check:{scope:'direct_exe_exact_canonical_image',inventory_complete:true,ownership:'not_verified'},
  existing_instances:[{pid:process.pid,process_created:'12345',same_image_verified:true}],error:'exact executable already running'};
const t=await startFake({replies:{launch:refusal}});
try{
 let r=await t.call('launch',{exe:'C:/fixture/game.exe',claim:true,arm_pad:true});
 check(r.isError&&r.json?.launched===false&&r.json?.already_running===true,'default duplicate refusal keeps native launch failure and existing-instance evidence');
 check(r.json?.pid===undefined&&r.json?.existing_instances?.[0]?.pid===process.pid,'existing receipt has no selected top-level target PID');
 check(t.callsOf('launch').length===1&&!t.callsOf('launch')[0].includes('--new-instance'),'default leaves explicit duplicate bypass absent');
 await new Promise(resolve=>setTimeout(resolve,50));
 const raw=readFileSync(join(t.dir,'calls.jsonl'),'utf8').split('\n').filter(Boolean).map(line=>JSON.parse(line));
 check(raw.length===1&&t.callsOf('claim').length===0&&t.callsOf('pad').length===0&&t.callsOf('attach').length===0,'duplicate refusal sends no claim, pad, attach or exit-watch action');
 for(const flag of [false,true]){
  t.clearCalls();r=await t.call('launch',{exe:'C:/fixture/game.exe',new_instance:flag});const args=t.callsOf('launch')[0]??[];const at=args.indexOf('--new-instance');
  check(at>=0&&args[at+1]===String(flag),'explicit boolean new_instance is forwarded exactly once');
 }
 for(const input of [{aumid:'Fixture!Game',new_instance:true},{steam_appid:'1',wait_exe:'game.exe',new_instance:false},{follow_pid:123,follow_created:'456',new_instance:true}]){
  t.clearCalls();r=await t.call('launch',input);check(r.isError&&t.calls().length===0,'non-direct new_instance refuses before native launch');
 }
 t.clearCalls();r=await t.call('launch',{exe:'C:/fixture/game.exe',new_instance:'true'});check(r.isError&&t.calls().length===0,'nonboolean bypass refuses before native launch');
}finally{await t.close();}
console.log(`launch instances MCP: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
