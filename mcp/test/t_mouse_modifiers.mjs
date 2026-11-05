import { withMouseModifiers } from '../build/mousemodifiers.js';
import { makeChecker, startFake, fullStatus } from './lib.mjs';
const {check,state}=makeChecker(),pid=42,owner='owner-test',id='42:123:1';
const prefix=p=>['--owner',owner,'--pid',String(p)];
const result=(json,ok=true)=>({ok,json,exitCode:ok?0:1,stderr:'',timedOut:false,argv:[]});
const actionReply=()=>({content:[{type:'text',text:'{"ok":true,"action":"mouse"}'},{type:'image',data:'AA==',mimeType:'image/png'}]});
const parsed=r=>JSON.parse(r.content.find(x=>x.type==='text').text);
async function scenario(options={}){
  const calls=[];let after=false,polled=0;const duration=60000+30*((options.modifiers??['ctrl']).filter(k=>!(options.preheld??[]).includes(({ctrl:17,shift:16,alt:18})[k])).length-1);
  const active={job_id:options.jobId??id,owner,requested_ms:duration,job_state:'running',job_complete:false,hold_ready:true};
  const run=async a=>{const cmd=a[4];calls.push(a.slice(4));
    if(cmd==='status')return result({ok:true,virtual_down:options.preheld??[]});
    if(cmd==='key')return result(options.missingId?{ok:true,accepted:true}:{...active,ok:true,accepted:true,...(options.queued?{job_state:'queued',hold_ready:false}:{})},!options.denied);
    if(cmd==='job-status'){polled++;return result({ok:true,job:after&&options.superseded?{...active,job_state:'cancelled',job_complete:true,hold_ready:false,job_reason:'manual_key_override'}:active});}
    if(cmd==='job-cancel')return result({ok:!options.cancelFails,job:{...active,job_state:'cancelled',job_complete:true,hold_ready:false}},!options.cancelFails);
    throw Error('unexpected native call '+cmd);
  };
  const action=async()=>{calls.push(['ACTION']);after=true;if(options.throwAction)throw Error('modeled action failure');return actionReply();};
  const r=await withMouseModifiers(pid,options.modifiers??['ctrl'],action,run,prefix);return {r,j:parsed(r),calls,polled};
}
let s=await scenario({queued:true});
check(s.calls.some(c=>c[0]==='key'&&c[1]==='ctrl'&&c[2]==='60000'&&c.includes('--async')),'temporary modifiers use one bounded native job');
check(s.calls.findIndex(c=>c[0]==='job-status')<s.calls.findIndex(c=>c[0]==='ACTION')&&s.j.modifiers.before_action.hold_ready,'mouse action waits for exact active native hold');
check(s.calls.at(-1)[0]==='job-cancel'&&s.calls.at(-1)[1]===id&&!s.calls.some(c=>c[0]==='up'||c[0]==='release'),'cleanup cancels only exact native job, with no key_up/global release');
check(s.j.modifiers.release_confirmed===true&&s.r.content.some(c=>c.type==='image'),'cleanup receipt and action image survive attachment');
s=await scenario({preheld:[17]});check(!s.calls.some(c=>c[0]==='key'||c[0]==='job-cancel')&&s.j.modifiers.already_held[0]==='ctrl','pre-held modifier aliases are preserved');
s=await scenario({throwAction:true});check(s.r.isError&&s.calls.at(-1)[0]==='job-cancel','thrown action still cancels its exact bounded job');
s=await scenario({superseded:true});check(s.r.isError&&/superseded/.test(s.j.modifier_scope_error)&&s.calls.at(-1)[1]===id&&!s.calls.some(c=>c[0]==='up'),'manual supersession is reported and newer modifier is never key-upped');
s=await scenario({denied:true});check(s.r.isError&&!s.calls.some(c=>c[0]==='ACTION')&&s.calls.at(-1)[0]==='job-cancel','uncertain admission with a positively owned ID gets exact-ID cleanup and no mouse action');
s=await scenario({missingId:true});check(s.r.isError&&!s.calls.some(c=>c[0]==='ACTION'||c[0]==='job-cancel'),'missing admission identity never triggers guessed cancellation or mouse action');
for(const jobId of ['0:1:1','42:0:1','42:1:0','42:01:1','42:1:1x','4294967296:1:1','42:18446744073709551616:1','42:1:18446744073709551616',`42:${'9'.repeat(10000)}:1`]){
  s=await scenario({jobId});check(s.r.isError&&!s.calls.some(c=>c[0]==='ACTION'||c[0]==='job-cancel'),'malformed/zero/overflow identity refuses action and guessed cleanup: '+jobId.slice(0,64));
}
s=await scenario({jobId:'42:18446744073709551615:18446744073709551615'});check(!s.r.isError&&s.calls.some(c=>c[0]==='ACTION'),'full uint64 generation/serial remain exact without Number truncation');
s=await scenario({cancelFails:true});check(s.r.isError&&s.j.modifiers.release_confirmed===false&&s.j.modifier_scope_error.includes('native deadline'),'unconfirmed cancellation retains native deadline backstop without broad release');
s=await scenario({modifiers:[]});check(s.calls.length===1&&s.calls[0][0]==='ACTION','empty modifier option adds no native calls');
const nativeId=`${process.pid}:123:1`,job={job_id:nativeId,owner:'mcp-test',requested_ms:60000,job_state:'running',job_complete:false,hold_ready:true};
const t=await startFake({replies:{status:fullStatus(),key:{ok:true,accepted:true,...job},'job-status':{ok:true,job},'job-cancel':{ok:true,job:{...job,job_complete:true,hold_ready:false,job_state:'cancelled'}}}});
try{
  let r=await t.call('scroll',{pid:process.pid,notches:1,modifiers:['ctrl']});check(!r.isError&&t.callsOf('scroll').length===1&&t.callsOf('job-cancel')[0]?.includes(nativeId),'MCP Ctrl+wheel uses bounded native scope');
  t.clearCalls();r=await t.call('click',{pid:process.pid,modifiers:['ctrl']});check(!r.isError&&t.callsOf('click').length===1&&t.callsOf('job-cancel').length===1,'MCP modified click uses same scope');
  t.clearCalls();r=await t.call('drag',{pid:process.pid,x1:10,y1:10,x2:20,y2:20,steps:1,ms:1,hold_start_ms:0,hold_end_ms:0,edge_margin:0,modifiers:['ctrl']});check(!r.isError&&t.callsOf('mouseup').length===1&&t.callsOf('job-cancel').length===1,'MCP drag retains modifier job through mouse release');
  t.clearCalls();r=await t.call('scroll',{pid:process.pid,notches:1,modifiers:['ctrl','ctrl']});check(r.isError&&t.callsOf('key').length===0,'duplicate modifiers refuse before input');
  t.clearCalls();r=await t.call('scroll',{pid:process.pid,notches:1,modifier_hold_ms:1000});check(r.isError&&t.calls().length===0,'hold deadline without modifiers refuses before native calls');
}finally{await t.close();}
console.log(`mouse modifiers: ${state.passes} PASS / ${state.failures} FAIL`);process.exit(state.failures?1:0);
