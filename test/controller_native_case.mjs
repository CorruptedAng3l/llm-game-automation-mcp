// Real SDK/real private MCP, one owned controller case. Outer PS owns fixture and
// this Node process via EA65/8F. SDK/production CLI children use audited retained
// ChildProcess close/timeout, not separate EA65 receipts or PID-only termination.
import { readFileSync,writeFileSync,realpathSync,statSync } from "node:fs";
import { join,resolve,sep } from "node:path";
import { pathToFileURL } from "node:url";
import { createRequire } from "node:module";
import { performance } from "node:perf_hooks";

const spec=JSON.parse(readFileSync(process.argv[2],"utf8"));
const root=realpathSync(spec.repo_root),mcp=realpathSync(spec.mcp_root),exe=realpathSync(spec.cli_exe);
if(!Number.isSafeInteger(spec.pid)||spec.pid<1||!Number.isSafeInteger(spec.case_token)||spec.case_token<1)throw Error("invalid owned identity token");
const under=p=>resolve(p).toLowerCase().startsWith((root+sep).toLowerCase());
for(const p of [mcp,exe,spec.target_log,spec.png_path])if(!under(p))throw Error("case path escaped private repository");
if(!["pad","pad_move","pad_look","pad_rotate","pad_drive"].includes(spec.tool))throw Error("controller case tool outside narrow allowlist");
const req=createRequire(pathToFileURL(join(mcp,"package.json")));
const {Client}=await import(pathToFileURL(req.resolve("@modelcontextprotocol/sdk/client/index.js")).href);
const {StdioClientTransport}=await import(pathToFileURL(req.resolve("@modelcontextprotocol/sdk/client/stdio.js")).href);
const transport=new StdioClientTransport({command:process.execPath,args:[join(mcp,"build/index.js")],cwd:mcp,
 env:{...process.env,VINPUT_EXE:exe},stderr:"pipe"});
const client=new Client({name:"owned-controller-native-case",version:"1"});
const record={schema:1,completed:false,pid:spec.pid,case_token:spec.case_token,tool:spec.tool,
 arguments:spec.arguments,internal_lifecycle:"audited SDK/production retained ChildProcess; not individually EA65 inventoried",
 server_pid_diagnostic:null,calls:[],image_count:0,error:null,cleanup:null};
let stderr="",stderrTruncated=false;
transport.stderr?.on("data",b=>{if(stderr.length+b.length<=65536)stderr+=b.toString();else stderrTruncated=true;});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
function rows(){if(statSync(spec.target_log).size>4*1024*1024)throw Error("fixture log exceeded bounded reader budget");return readFileSync(spec.target_log,"utf8").split(/\r?\n/).filter(x=>x.startsWith("{")&&x.endsWith("}")).map(x=>JSON.parse(x));}
async function observedActive(){const end=performance.now()+2500;do{const states=rows().filter(x=>x.event==="state"&&x.case_token===spec.case_token&&x.rc===0&&[x.buttons,x.lt,x.rt,x.lx,x.ly,x.rx,x.ry].some(v=>v!==0));if(["std","ex"].every(s=>states.some(x=>x.source===s)))return states;await sleep(5);}while(performance.now()<end);return null;}
async function observedNeutral(){const end=performance.now()+1000;do{const states=rows().filter(x=>x.event==="state"&&x.case_token===spec.case_token);const last=["std","ex"].map(source=>states.filter(x=>x.source===source).at(-1));if(last.every(x=>x?.rc===0&&[x.buttons,x.lt,x.rt,x.lx,x.ly,x.rx,x.ry].every(v=>v===0))&&["std","ex"].every(source=>states.some(x=>x.source===source&&x.rc===0&&[x.buttons,x.lt,x.rt,x.lx,x.ly,x.rx,x.ry].some(v=>v!==0))))return last;await sleep(5);}while(performance.now()<end);return null;}
async function call(name,args){
 const begin=performance.now();const result=await client.callTool({name,arguments:{...args,pid:spec.pid}},undefined,{timeout:60000});
 const texts=result.content.filter(c=>c.type==="text").map(c=>c.text);let json=null;try{json=JSON.parse(texts[0]??"");}catch{}
 const images=result.content.filter(c=>c.type==="image");const row={name,arguments:args,elapsed_ms:performance.now()-begin,is_error:!!result.isError,json,texts,image_count:images.length};record.calls.push(row);return {result,row,images};
}
try{
 await client.connect(transport);record.server_pid_diagnostic=transport.pid??null;
 const action=await call(spec.tool,spec.arguments);record.action=action.row;
 record.image_count=action.images.length;
 if(action.images.length===1){if(action.images[0].mimeType!=="image/png")throw Error("case requires lossless PNG observation");writeFileSync(spec.png_path,Buffer.from(action.images[0].data,"base64"));record.png_path=spec.png_path;}
 if(spec.cancel_after_active&&!action.row.is_error&&action.row.json?.ok){
  record.observed_before_cancel=await observedActive();record.active_observation_missing=record.observed_before_cancel===null;const id=action.row.json.job_id??action.row.json.job?.job_id;
  if(typeof id!=="string"||!id)throw Error("async action returned no actual job id");record.cancel=(await call("job_cancel",{id})).row;
 }
 if(!action.row.is_error&&action.row.json?.ok&&(!spec.arguments.async&&!spec.arguments.hold||spec.cancel_after_active)){record.observed_neutral_before_cleanup=await observedNeutral();record.neutral_observation_missing=record.observed_neutral_before_cleanup===null;}
 record.completed=true;
}catch(error){record.error=String(error?.stack??error);}
finally{
 try{record.cleanup=(await call("pad",{buttons:"none",ms:40,lx:0,ly:0,rx:0,ry:0,lt:0,rt:0,replace:true,capture_after:false})).row;}catch(error){record.cleanup={error:String(error)};}
 try{await client.close();}catch(error){record.close_error=String(error);}try{await transport.close();}catch(error){record.transport_close_error=String(error);}
 record.server_stderr=stderr;record.server_stderr_truncated=stderrTruncated;
 if(record.cleanup?.error||record.cleanup?.is_error||record.cleanup?.json?.ok!==true||record.close_error||record.transport_close_error||stderrTruncated)record.completed=false;
}
process.stdout.write(JSON.stringify(record)+"\n");
process.exitCode=record.completed&&!record.error&&!record.close_error&&!record.transport_close_error&&!stderrTruncated?0:1;
