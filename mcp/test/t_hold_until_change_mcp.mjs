import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StdioClientTransport} from '@modelcontextprotocol/sdk/client/stdio.js';
import {mkdtempSync,writeFileSync,readFileSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {makeChecker} from './lib.mjs';
const {check,state}=makeChecker(),dir=mkdtempSync(join(tmpdir(),'camera-helper-'));
const log=join(dir,'calls.jsonl'),scenario=join(dir,'scenario.json');writeFileSync(log,'');writeFileSync(scenario,JSON.stringify({}));
const transport=new StdioClientTransport({command:process.execPath,args:[process.env.CAMERA_HELPER_SERVER??resolve('build/index.js')],stderr:'pipe',env:{...process.env,VINPUT_EXE:resolve('test/fake_camera.mjs'),VINPUT_FAKE_LOG:log,VINPUT_FAKE_SCENARIO:scenario,VINPUT_OWNER:'mcp-test',VINPUT_CAPTURE_DIR:dir}});
const client=new Client({name:'camera-fixture',version:'1'});await client.connect(transport);
const calls=()=>readFileSync(log,'utf8').split('\n').filter(Boolean).map(x=>JSON.parse(x).argv);
const call=async(args)=>{const r=await client.callTool({name:'hold_until_change',arguments:{pid:process.pid,...args}});let j;try{j=JSON.parse(r.content.find(c=>c.type==='text').text)}catch{}return {r,j}};
try {
 const list=await client.listTools(),registered=list.tools.some(t=>t.name==='hold_until_change');
 check(registered,'hold_until_change is exposed by the actual MCP server');
 if(registered){
  let {r,j}=await call({key:'space',timeout_ms:5000,poll_ms:50,region:[10,20,100,80]});
  check(!r.isError&&j.changed&&j.release_confirmed,'actual MCP observes region change and confirms exact-job release');
  check(calls().some(a=>a.includes('job-cancel')&&a.includes(j.job_id))&&!calls().some(a=>a.includes('release')||a.includes('down')),'MCP integration uses one bounded job and exact cancellation only');
  check(calls().some(a=>{const i=a.indexOf('json');if(i<0)return false;const q=JSON.parse(a[i+1]);return q.cmd==='key'&&q.async===true&&typeof q.expected_capture_scope==='string';}),'actual MCP fences the bounded key admission with its captured scope');
  check(calls().some(a=>{const i=a.indexOf('json');return i>=0&&JSON.parse(a[i+1]).region==='10,20,100,80';}),'tuple region normalizes before native hash observation');
  const before=calls().length;({r}=await call({key:'space',timeout_ms:0}));
  check(r.isError&&calls().length===before,'invalid timeout refuses before CLI work');
  writeFileSync(scenario+'.state',JSON.stringify({hashReads:0,duration:5000,target:[300,250]}));
  ({r,j}=await call({key:'space',timeout_ms:5000,capture_after:0,max_width:160}));
  check(!r.isError&&r.content.some(c=>c.type==='image')&&calls().some(a=>a.includes('capture')&&a.includes('--max-width')&&a.includes('160')),'standard post-action image options reach the shared capture path');
  writeFileSync(scenario,JSON.stringify({camera:{manualOwner:true}}));const beforeOwned=calls().length;({r,j}=await call({key:'space',timeout_ms:5000}));
  check(r.isError&&!j.admission_attempted&&!j.hold_may_still_be_active&&calls().slice(beforeOwned).every(a=>a.includes('status')),'current nested native user ownership refuses before hold admission');
 }
}finally{await client.close();}
console.log(`CAMERA_RESULT ${JSON.stringify({completed:true,passed:state.passes,failed:state.failures,scope:'actual MCP server/CLI orchestration with a stateful fake target; no native input/rendering'})}`);
process.exit(state.failures?1:0);
