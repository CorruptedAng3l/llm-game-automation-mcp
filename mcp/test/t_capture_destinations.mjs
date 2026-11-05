import {mkdtempSync,existsSync,rmSync,readdirSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {makeChecker,startFake,cmdOf} from './lib.mjs';
import {decodePng} from '../build/png.js';
const {state,check}=makeChecker(),pid=process.pid,dir=mkdtempSync(join(tmpdir(),'vi-dest-'));
const t=await startFake({png:{w:640,h:360,core_capture:true}});
try{
 let r=await t.call('capture',{pid,region:{x:100,y:20,w:400,h:100},max_width:700,upscale:true,out_dir:join(dir,'crops')});
 let im=decodePng(Buffer.from(r.image.data,'base64'));
 check(!r.isError&&im.width===700&&im.height===175&&r.json.image.scale===1.75&&r.json.image.offset_x===100&&r.json.image.offset_y===20,'explicit crop enlargement returns exact per-axis client mapping');
 check(r.json.requested_region.w===400&&r.json.region_coordinate_system==='client_pixels'&&r.json.path.startsWith(resolve(dir))&&existsSync(r.json.path),'caller output directory is created and region units are echoed');
 const saved=join(dir,'exact.png');r=await t.call('capture',{pid,save_path:saved});check(!r.isError&&r.json.path===saved&&existsSync(saved),'save_path selects exact saved artifact');
 t.clearCalls();r=await t.call('capture',{pid,path:saved,out_dir:dir});check(r.isError&&/only one/.test(r.text)&&!t.callsOf('capture').length,'conflicting destinations refuse before capture');
 r=await t.call('capture',{pid,region:{x:1,y:2,w:10,h:10},zoom:16});im=decodePng(Buffer.from(r.image.data,'base64'));check(!r.isError&&im.width===160&&im.height===160,'bounded PNG zoom above eight is accepted');
 r=await t.call('capture',{pid,region:{x:1,y:2,w:400,h:100},max_width:700});check(decodePng(Buffer.from(r.image.data,'base64')).width===400,'default width cap preserves existing no-upscale behavior');
 t.clearCalls();r=await t.call('scroll',{pid,amount:-2,capture_after:0,out_dir:join(dir,'actions')});check(!r.isError&&cmdOf(t.callsOf('scroll')[0])[1]==='-2'&&r.json.capture_after.path.startsWith(join(dir,'actions')),'scroll alias and action output directory are honored with one wheel request');
 for(const alias of ['delta','clicks']){t.clearCalls();r=await t.call('scroll',{pid,[alias]:3});check(!r.isError&&t.callsOf('scroll').length===1&&cmdOf(t.callsOf('scroll')[0])[1]==='3',alias+' forwards one wheel request');}
 for(const args of [{},{notches:1,delta:1}]){t.clearCalls();r=await t.call('scroll',{pid,...args});check(r.isError&&/notches/.test(r.text)&&!t.callsOf('scroll').length,'missing/ambiguous wheel amount returns expected shape before input');}
 t.clearCalls();r=await t.call('type',{pid,text:'hello',as_keys:true});check(!r.isError&&t.callsOf('json').length===1&&JSON.parse(cmdOf(t.callsOf('json')[0])[1]).text==='hello','type alias uses the same text handler without replay');
 t.clearCalls();r=await t.call('click',{pid,x:10,y:10,out_dir:dir});check(r.isError&&!t.callsOf('click').length,'action destination requires an enabled capture before input');
 r=await t.call('capture_burst',{pid,frames:1,out_dir:join(dir,'bursts')});check(!r.isError&&existsSync(cmdOf(t.callsOf('capture-burst').at(-1))[1]),'burst uses caller output directory');
 const defaultsBefore=readdirSync(t.dir).filter(name=>/^pid.*\.(png|jpg)$/i.test(name)).sort();
 t.clearCalls();const exactNoImage=join(dir,'without-image.png');r=await t.call('capture',{pid,save_path:exactNoImage,include_image:false});
 check(!r.isError&&!r.image&&r.json.path===exactNoImage&&t.callsOf('capture').length===1&&cmdOf(t.callsOf('capture')[0])[1]===exactNoImage,'explicit path with no attachment is saved once and remains the reply path');
 await t.call('capture',{pid,out_dir:join(dir,'only-here')});
 await t.call('scroll',{pid,notches:1,capture_after:0,out_dir:join(dir,'only-here')});
 await t.call('capture_burst',{pid,frames:1,out_dir:join(dir,'only-here')});
 check(JSON.stringify(readdirSync(t.dir).filter(name=>/^pid.*\.(png|jpg)$/i.test(name)).sort())===JSON.stringify(defaultsBefore),'explicit capture, action and burst destinations add no default capture copies');
 t.clearCalls();r=await t.call('capture',{pid});
 check(!r.isError&&r.json.path.startsWith(t.dir)&&t.callsOf('capture').length===1&&readdirSync(t.dir).filter(name=>/^pid.*\.(png|jpg)$/i.test(name)).length===defaultsBefore.length+1,'omitted destination positive control creates exactly one additional default artifact');
}finally{await t.close();rmSync(dir,{recursive:true,force:true});}
console.log(`capture destinations: ${state.passes} PASS / ${state.failures} FAIL`);if(state.failures)process.exitCode=1;
