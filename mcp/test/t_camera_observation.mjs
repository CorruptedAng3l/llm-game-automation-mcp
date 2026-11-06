import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
import {makeChecker} from './lib.mjs';
const moduleUrl=process.env.CAMERA_OBSERVATION_MODULE?pathToFileURL(resolve(process.env.CAMERA_OBSERVATION_MODULE)).href:new URL('../build/cameraobservation.js',import.meta.url).href;
const {CameraObservation,parseCameraFrame,canonicalCameraScope}=await import(moduleUrl);
const {check,state}=makeChecker();
const scope={hwnd:'0x1234',thread:17,adoption_generation:'2',geometry_epoch:'0',lifetime_cookie:'3',client_w:800,client_h:600,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch'};
const colours=(value,cells=4)=>value.toString(16).padStart(2,'0').repeat(3*cells);
function frame(ms,patch={}){
 return {ok:true,method:'swapchain',capture_path:'d3d11',capture_scope:{...scope},capture_space_updated:false,
  timing_clock:'QueryPerformanceCounter',qpc_frequency:'1000',capture_qpc_ticks:String(100000+ms),frame_qpc_ticks:String(99999+ms),captured_present:1000+ms,
  origin_x:10,origin_y:20,region_client_w:200,region_client_h:120,hash:'A'.repeat(16),
  motion_sample_version:1,motion_sample_w:2,motion_sample_h:2,motion_sample_rgb:colours(0),...patch};
}
function noSource(ms,patch={}){const r=frame(ms,patch);delete r.frame_qpc_ticks;delete r.captured_present;return r;}
function run(policy,rows){let r=policy.snapshot();for(const [qpc,wall,patch] of rows)r=policy.observe(frame(qpc,patch),wall);return r;}
const errorContains=(r,reason)=>!r.ok&&r.reason===reason;
check(canonicalCameraScope({...scope})===canonicalCameraScope({...scope,thread:17})&&JSON.parse(canonicalCameraScope(scope)).geometry_witness===scope.geometry_witness,'canonical native scope retains every lifetime/geometry witness in a stable key');
for(const altered of [{...scope,hwnd:'0x10000000000000000'},{...scope,lifetime_cookie:'0'},{...scope,geometry_epoch:'00'},{...scope,geometry_witness:undefined},{...scope,extra:'unrecognized scope witness'}])check(canonicalCameraScope(altered)===undefined,'invalid or unknown scope witnesses cannot silently join an observation');
let parsed=parseCameraFrame(frame(0));
check(parsed.ok&&parsed.sample.qpc===100000n&&parsed.sample.frequency===1000n&&parsed.sample.frameIndex===1000n&&parsed.sample.hash==='A'.repeat(16),'native capture parser preserves exact clocks/source witness and normalizes only the pixel hash');
for(const [patch,reason] of [
 [{ok:1},'capture_unavailable'],[{capture_scope:null},'capture_scope_invalid'],[{capture_space_updated:true},'capture_map_not_preserved'],[{capture_space_updated:undefined},'capture_map_not_preserved'],
 [{method:'auto'},'capture_method_invalid'],[{timing_clock:'wall'},'capture_clock_invalid'],[{capture_qpc_ticks:100000},'capture_clock_invalid'],[{capture_qpc_ticks:'0100000'},'capture_clock_invalid'],
 [{capture_qpc_ticks:'18446744073709551616'},'capture_clock_invalid'],[{qpc_frequency:'0'},'capture_clock_invalid'],[{hash:'A'.repeat(15)},'capture_hash_invalid'],
 [{capture_path:''},'capture_path_invalid'],[{region_client_w:1000},'capture_region_invalid'],[{motion_sample_rgb:'bad'},'motion_sample_invalid'],
 [{frame_qpc_ticks:'100001'},'source_frame_clock_future'],[{frame_qpc_ticks:100000},'source_frame_clock_invalid'],[{captured_present:1.5},'source_frame_index_invalid']
])check(errorContains(parseCameraFrame(frame(0,patch)),reason),`malformed capture metadata refuses with ${reason}`);
const absentGrid=frame(0);for(const k of ['motion_sample_version','motion_sample_w','motion_sample_h','motion_sample_rgb'])delete absentGrid[k];
check(parseCameraFrame(absentGrid).ok&&!parseCameraFrame(absentGrid,{require_motion:true}).ok,'exact hashes do not require an RGB grid; RGB mode never invents a missing sample');

let p=new CameraObservation(frame(0));let r=p.observe(frame(250),250);
check(!r.settled&&r.quiet_observed_ms===250&&r.quiet_fresh_samples===1,'passive observation requires both native quiet duration and fresh sample count');
r=p.observe(frame(500),500);
check(r.outcome==='settled'&&r.passive_quiet&&!r.post_input_pixels_settled&&r.quiet_observed_ms===500&&r.quiet_fresh_samples===2,'passive quiet is separate from a post-input onset verdict');
check(r.native_source_frame_progress_observed===true&&r.frame_freshness==='native_hook_witness_advanced'&&!r.rendered_frame_freshness_measured,'advancing native hook witnesses do not become display/rendered-content freshness proof');

p=new CameraObservation(frame(0),{mode:'post_input'});r=run(p,[[100,100,{}],[250,250,{}],[500,500,{}],[750,750,{}]]);
check(!r.settled&&!r.onset_observed&&r.quiet_fresh_samples===0&&r.quiet_observed_ms===0,'quiet baseline frames before delayed motion cannot report post-input settled');
r=p.observe(frame(800,{hash:'B'.repeat(16)}),800);
check(r.onset_observed&&!r.settled&&r.quiet_observed_ms===0&&r.onset_capture_qpc_ticks==='100800','first changed sample arms a new quiet interval at its actual native QPC');
r=run(p,[[1200,1200,{hash:'C'.repeat(16)}],[2000,2000,{hash:'D'.repeat(16)}],[2800,2800,{hash:'E'.repeat(16)}],[3050,3050,{hash:'E'.repeat(16)}]]);
check(!r.settled&&r.quiet_observed_ms===250,'continued camera-like smoothing resets quiet until its last observed change');
r=p.observe(frame(3300,{hash:'E'.repeat(16)}),3300);
check(r.settled&&r.post_input_pixels_settled&&r.quiet_observed_ms===500&&r.pixel_change_observed,'post-input pixels settle only after onset plus two fresh samples spanning the native quiet interval');

p=new CameraObservation(frame(0),{mode:'post_input'});r=run(p,[[100,100,{hash:'B'.repeat(16)}],[250,1000,{hash:'B'.repeat(16)}],[400,2000,{hash:'B'.repeat(16)}]]);
check(!r.settled&&r.quiet_observed_ms===300&&r.elapsed_ms===2000,'slow host observation does not stretch300ms of native quiet into a500ms verdict');
r=p.tick(4000);check(r.outcome==='timeout'&&r.onset_observed&&!r.settled,'wall deadline ends a post-onset observation even when native quiet was insufficient');

p=new CameraObservation(frame(0),{timeout_ms:500});r=p.observe(frame(500),500);
check(r.outcome==='unavailable'&&r.samples===1&&r.last_capture_qpc_ticks==='100000','sample arriving at the wall deadline is never admitted as fresh evidence');
p=new CameraObservation(frame(0),{timeout_ms:1000});p.observe(frame(100),100);r=p.observe(frame(600),1001);
check(r.outcome==='timeout'&&r.samples===2&&!r.settled&&r.last_capture_qpc_ticks==='100100','late sample that could satisfy quiet is ignored before any state mutation');

for(const [patch,reason] of [
 [{capture_qpc_ticks:'100000',frame_qpc_ticks:'99999',captured_present:1000},'capture_clock_not_advanced'],
 [{capture_qpc_ticks:'99999',frame_qpc_ticks:'99998',captured_present:1001},'capture_clock_not_advanced'],
 [{qpc_frequency:'2000'},'capture_frequency_changed'],[{capture_scope:{...scope,geometry_epoch:'1'}},'capture_scope_changed'],
 [{method:'printwindow',capture_path:'printwindow'},'capture_method_changed'],[{capture_path:'d3d12'},'capture_method_changed'],
 [{origin_x:11},'capture_region_changed'],[{captured_present:1000},'native_source_frame_not_advanced'],[{frame_qpc_ticks:'99999'},'native_source_frame_not_advanced'],
 [{frame_qpc_ticks:null},'native_source_frame_not_advanced'],[{captured_present:null},'native_source_frame_not_advanced']
]){p=new CameraObservation(frame(0));r=p.observe(frame(100,patch),100);check(r.outcome==='unavailable'&&r.stop_reason===reason&&r.samples===1&&r.rejected_samples===1,`changed/stale evidence refuses honestly with ${reason}`);}
p=new CameraObservation(frame(0));r=p.observe(frame(100,{captured_present:1000}),100);
check(r.native_source_frame_progress_observed===null&&r.last_capture_qpc_ticks==='100000','a rejected sample cannot contribute even a partial native-source progress claim');

p=new CameraObservation(noSource(0));p.observe(noSource(250),250);r=p.observe(noSource(500),500);
check(r.settled&&r.frame_freshness==='unmeasured_native_source'&&r.native_source_frame_progress_observed===null,'absent optional native-source identifiers stay explicitly unmeasured while pixel quiet remains qualified');
p=new CameraObservation(noSource(0));r=p.observe(frame(250),250);check(r.native_source_frame_progress_observed===null,'newly available source identity is not retroactive advancement proof');
r=p.observe(frame(500),500);check(r.native_source_frame_progress_observed===true,'later comparable advancing source witnesses can add qualified progress evidence');

const pw=ms=>frame(ms,{method:'printwindow',capture_path:'printwindow',frame_qpc_ticks:'irrelevant native aggregate',captured_present:1});
p=new CameraObservation(pw(0),{mode:'post_input'});p.observe({...pw(800),hash:'B'.repeat(16)},800);p.observe({...pw(1050),hash:'B'.repeat(16)},1050);r=p.observe({...pw(1300),hash:'B'.repeat(16)},1300);
check(r.settled&&r.post_input_pixels_settled&&r.frame_freshness==='unmeasured_printwindow'&&r.native_source_frame_progress_observed===null&&r.last_frame_qpc_ticks===null&&r.last_frame_index===null,'PrintWindow can report changed-then-quiet pixels but never borrows native aggregate frame freshness');

p=new CameraObservation(frame(0),{mode:'post_input'});p.observe(frame(500),500);r=p.tick(4000);
check(r.outcome==='no_change'&&r.pixel_change_observed===false&&!r.settled&&r.actual_angle===null&&r.do_not_repeat,'dark/static scene gives bounded no-change guidance without claiming rejected input, stuck motion or camera angle');
p=new CameraObservation(frame(0),{mode:'post_input'});r=p.tick(4000);
check(r.outcome==='unavailable'&&r.pixel_change_observed===null,'no comparable post-input samples is unavailable rather than measured no-change');

const options={mode:'post_input',metric:'rgb_grid',max_change_pct:1,pixel_tolerance:8};p=new CameraObservation(frame(0),options);
for(let ms=100;ms<=900;ms+=100)r=p.observe(frame(ms,{motion_sample_rgb:colours(ms/25),hash:(ms%200?'A':'B').repeat(16)}),ms);
check(r.onset_observed&&!r.settled&&r.quiet_observed_ms<500,'anchored RGB quiet comparison detects accumulated drift hidden by small pairwise deltas');
const quiet=new CameraObservation(frame(0),{metric:'rgb_grid',max_change_pct:1}),noisy=new CameraObservation(frame(0,{origin_x:300}),{metric:'rgb_grid',max_change_pct:1});
quiet.observe(frame(250,{hash:'B'.repeat(16),outside_region_noise:'first animation'}),250);r=quiet.observe(frame(500,{hash:'C'.repeat(16),outside_region_noise:'other animation'}),500);
noisy.observe(frame(250,{origin_x:300,motion_sample_rgb:colours(255)}),250);const noise=noisy.observe(frame(500,{origin_x:300,motion_sample_rgb:colours(0)}),500);
check(r.settled&&!noise.settled&&r.pixel_change_observed===false,'independent noisy region/irrelevant exact hash cannot contaminate the selected RGB-region observation');
p=new CameraObservation(frame(0),{metric:'rgb_grid'});r=p.observe(frame(250,{motion_sample_w:1,motion_sample_h:4}),250);
check(r.outcome==='unavailable'&&r.stop_reason==='motion_layout_changed','RGB-grid shape changes cannot masquerade as comparable quiet');

p=new CameraObservation(frame(0),{mode:'post_input',max_samples:60,timeout_ms:60000});for(let n=1;n<60;n++)r=p.observe(frame(n),n);
check(r.terminal&&r.outcome==='no_change'&&r.stop_reason==='sample_limit'&&r.samples===60,'finite60-sample cap ends pre-onset observation without another input or fabricated settle');
const terminal=JSON.stringify(r);r=p.observe(null,1000);check(JSON.stringify(r)===terminal,'terminal policy does not parse another sample or mutate frozen evidence');
p=new CameraObservation(frame(0),{max_samples:3});p.observe(frame(250),250);r=p.observe(frame(500),500);check(r.settled&&r.samples===3,'exact quiet success at the final admitted sample wins over the cap');

const preciseBase=frame(0,{qpc_frequency:'10000',frame_qpc_ticks:'99999',captured_present:1});p=new CameraObservation(preciseBase);
r=p.observe(frame(1,{qpc_frequency:'10000',capture_qpc_ticks:'104999',frame_qpc_ticks:'104998',captured_present:2}),1);
check(!r.settled&&r.quiet_observed_ms===499.9,'fractional499.9ms native interval does not round into500ms readiness');
r=p.observe(frame(2,{qpc_frequency:'10000',capture_qpc_ticks:'105000',frame_qpc_ticks:'104999',captured_present:3}),2);
check(r.settled&&r.quiet_observed_ms===500,'exact native quiet threshold uses integer cross-multiplication');

for(const opts of [{quiet_ms:99},{quiet_ms:2001},{min_fresh_samples:1},{min_fresh_samples:11},{timeout_ms:0},{timeout_ms:60001},{max_samples:61},{max_samples:3,min_fresh_samples:3},{metric:'exact_hash',max_change_pct:1},{pixel_tolerance:256}]){
 let refused=false;try{new CameraObservation(frame(0),opts);}catch(e){refused=e instanceof RangeError;}check(refused,'invalid observation limits refuse before sample processing');
}
p=new CameraObservation(frame(0));p.tick(10);r=p.observe(frame(100),9);check(r.outcome==='unavailable'&&r.stop_reason==='wall_clock_regressed','regressed host clock cannot reopen or extend a bounded observation');
p=new CameraObservation(frame(0),{mode:'post_input'});r=p.unavailable('modeled capture transport failure',100);
check(r.outcome==='unavailable'&&r.error.includes('transport failure')&&r.do_not_repeat&&r.samples===1,'observer transport failure is advisory and contains no input, retry or inverse action');
const original=frame(0);p=new CameraObservation(original);original.capture_scope.geometry_epoch='999';original.motion_sample_rgb=colours(255);const publicView=p.snapshot();publicView.capture_scope.geometry_epoch='888';p.observe(frame(250),250);r=p.observe(frame(500),500);
check(r.settled&&r.capture_scope.geometry_epoch==='0','caller receipt/report mutation cannot rewrite the retained baseline scope or pixels');
check(JSON.parse(JSON.stringify(r)).actual_angle===null&&r.input_consumption_measured===false&&r.queue_drain_measured===false&&r.camera_angle_measured===false,'reports are JSON-safe and never convert pixels into angular or input-consumption measurement');
console.log(`CAMERA_OBSERVATION_RESULT ${JSON.stringify({completed:true,passed:state.passes,failed:state.failures,scope:'finite pure metadata/QPC/pixel policy; no captures, input, inverse movement or game-angle measurement'})}`);
console.log(`camera observation: ${state.passes} PASS / ${state.failures} FAIL`);
if(state.failures)process.exitCode=1;
