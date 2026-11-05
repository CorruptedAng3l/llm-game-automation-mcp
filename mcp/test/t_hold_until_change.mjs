const { holdUntilChange } = await import(process.env.CAMERA_HELPER_MODULE ?? '../build/camerahelpers.js');
import { makeChecker } from './lib.mjs';
const {check,state}=makeChecker(),pid=4242,owner='unit-camera',id=`${pid}:10:1`;
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600};
function fixture(options={}) {
  const calls=[];let reads=0,epoch=0,finishOptions,finishEpoch;
  const duration=options.duration??500;
  const base={job_id:id,owner,requested_ms:duration,job_state:'running',job_complete:false,hold_ready:true,capture_scope_checked:true};
  const ctx={owner,at:p=>['--pid',String(p),'--owner',owner],epoch:()=>epoch,captureError:()=>undefined,
    finish:async(p,out,a,e)=>{finishOptions=a;finishEpoch=e;return out;},run:async(argv,budget)=>{
      const a=argv.slice(4),cmd=a[0];calls.push({argv,budget});let json;
      if(cmd==='status')json={ok:true,pid,capture_scope_version:1,key_sequence_version:options.old?undefined:1,input_scope_version:options.oldScope?undefined:1,owner_active:options.ownerActive===true,cursor_guard:{owner_active:options.nestedOwner===true}};
      else if(cmd==='jobs')json={ok:true,jobs:[]};
      else if(cmd==='json'){
        const request=JSON.parse(a[1]);
        if(request.cmd==='key'){
          if(options.admissionThrows)throw new Error('lost admission acknowledgement');
          json=options.explicitRefusal?{ok:false,input_injected:false,error:'capture_scope_changed'}:options.unknownFailure?{ok:false,error:'transport failed'}:{ok:true,accepted:true,...base,job_id:options.badAck?'999:10:1':id,capture_scope_checked:options.uncheckedAck?false:true};
        }else {
        reads++;if(options.hashThrows&&reads>1)throw new Error('capture transport fault');
        if(options.epochChange&&reads>1)epoch++;
        json={ok:true,hash:options.stable||reads===1?'1111111111111111':'2222222222222222',capture_scope:options.scopeChange&&reads>1?{...scope,geometry_epoch:'1'}:scope};
        }
      }else if(cmd==='key'){
        if(options.admissionThrows)throw new Error('lost admission acknowledgement');
        json=options.explicitRefusal?{ok:false,input_injected:false,error:'capture_scope_changed'}:options.unknownFailure?{ok:false,error:'transport failed'}:{ok:true,accepted:true,...base,job_id:options.badAck?'999:10:1':id,capture_scope_checked:options.uncheckedAck?false:true};
      }
      else if(cmd==='job-status'){if(options.statusDelay)await new Promise(r=>setTimeout(r,options.statusDelay));json={ok:true,job:{ok:false,accepted:false,...base,...(options.completed?{job_complete:true,job_state:'completed',hold_ready:false}:{}),...(options.badStatus?{owner:'someone-else'}:{})}};}
      else if(cmd==='job-cancel'){if(options.epochOnCancel)epoch++;json={ok:true,job:{ok:false,accepted:false,...base,job_state:'cancelled',job_complete:true,hold_ready:false,job_id:options.badCancel?'4242:10:99':id}};}
      else throw new Error(`unexpected ${cmd}`);
      return {ok:json.ok===true,json,exitCode:0,stderr:'',timedOut:false,argv};
    }};
  return {ctx,calls,duration,get finishOptions(){return finishOptions;},get finishEpoch(){return finishEpoch;}};
}
const read=r=>JSON.parse(r.content[0].text), args=f=>({pid,key:'space',timeout_ms:f.duration,poll_ms:50});
const keyCalls=f=>f.calls.filter(c=>c.argv[4]==='json'&&JSON.parse(c.argv[5]).cmd==='key');
const hashCalls=f=>f.calls.filter(c=>c.argv[4]==='json'&&JSON.parse(c.argv[5]).cmd==='frame-hash');
let f=fixture(),r=read(await holdUntilChange(f.ctx,args(f)));
check(r.ok&&r.changed&&r.release_confirmed&&r.job_id===id,'changed pixels cancel the exact admitted native job');
check(keyCalls(f).length===1&&JSON.parse(keyCalls(f)[0].argv[5]).ms===500&&JSON.parse(keyCalls(f)[0].argv[5]).async===true&&f.calls.filter(c=>c.argv[4]==='job-cancel').length===1,'one bounded async hold and one exact cancellation');
check(keyCalls(f).length===1&&JSON.parse(keyCalls(f)[0].argv[5]).expected_capture_scope===JSON.stringify(scope),'hold admission carries the exact canonical baseline scope token');
check(!f.calls.some(c=>['down','up','release'].includes(c.argv[4])),'no indefinite down/up or global release fallback');
check(hashCalls(f).every(c=>JSON.parse(c.argv[5]).preserve_map===true),'hash observations preserve the saved image-coordinate mapping');
f=fixture({badAck:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&r.hold_may_still_be_active&&!f.calls.some(c=>c.argv[4]==='job-cancel'),'mismatched acknowledgement never authorizes cancellation of an arbitrary job');
f=fixture({scopeChange:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&!r.changed&&r.release_confirmed&&r.error.includes('geometry'),'scope changes abort observation and cancel only the admitted job');
f=fixture({epochChange:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&!r.release_confirmed&&!f.calls.some(c=>c.argv[4]==='job-cancel'),'attachment change never sends cleanup into the new attachment');
f=fixture({hashThrows:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&r.release_confirmed&&r.error.includes('capture transport fault'),'capture exceptions still cancel the exact owned job');
f=fixture({badCancel:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&r.changed&&!r.release_confirmed&&r.hold_may_still_be_active,'wrong cancellation generation cannot become a release confirmation');
f=fixture({completed:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&!r.changed&&r.release_confirmed&&r.reason==='native_completed'&&!f.calls.some(c=>c.argv[4]==='job-cancel'),'native completion is terminal without inventing a screen change or extra cancellation');
f=fixture({duration:100,stable:true});const start=performance.now();r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&!r.changed&&r.release_confirmed&&performance.now()-start<500,'unchanged region stops at the bounded observation deadline');
f=fixture({old:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&!keyCalls(f).length,'unsupported target refuses before any hold');
for(const option of ['oldScope','ownerActive','nestedOwner']){f=fixture({[option]:true});r=read(await holdUntilChange(f.ctx,args(f)));check(!r.ok&&!keyCalls(f).length&&!r.hold_may_still_be_active,`${option} refuses before admission`);}
for(const option of ['admissionThrows','unknownFailure','uncheckedAck']){f=fixture({[option]:true});r=read(await holdUntilChange(f.ctx,args(f)));check(!r.ok&&r.admission_attempted&&r.hold_may_still_be_active&&!f.calls.some(c=>c.argv[4]==='job-cancel'),`${option} leaves admission uncertain and never authorizes arbitrary cleanup`);}
f=fixture({explicitRefusal:true});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&r.admission_attempted&&r.admission_explicitly_refused&&!r.hold_may_still_be_active&&!f.calls.some(c=>c.argv[4]==='job-cancel'),'explicit typed no-input refusal proves no hold was admitted');
f=fixture({duration:100,statusDelay:150});r=read(await holdUntilChange(f.ctx,args(f)));
check(!r.ok&&!r.changed&&r.samples===0&&hashCalls(f).length===1,'a status reply after the deadline never admits a late screen-change success');
f=fixture({epochOnCancel:true});r=read(await holdUntilChange(f.ctx,{...args(f),capture_after:0}));
check(f.finishEpoch===0,'post-action capture retains the original attachment epoch');
f=fixture();const capture={...args(f),capture_after:0,max_width:160,format:'jpeg',quality:75};r=read(await holdUntilChange(f.ctx,capture));
check(r.ok&&f.finishOptions.max_width===160&&f.finishOptions.format==='jpeg'&&f.finishOptions.capture_after===0,'post-action capture options survive the helper unchanged');
console.log(`CAMERA_RESULT ${JSON.stringify({completed:true,passed:state.passes,failed:state.failures,scope:'finite dependency-injected native-protocol model; no actual input or rendering'})}`);
process.exit(state.failures?1:0);
