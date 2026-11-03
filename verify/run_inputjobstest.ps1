param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\input_jobs\after",[switch]$Baseline,[switch]$SkipRender,[switch]$KeepGoing,[switch]$NativeOnly)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";New-Item -ItemType Directory -Force "$tools\temp"|Out-Null
if(-not('ViJobNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViJobNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'}
Add-Type -AssemblyName System.Drawing
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[Diagnostics.Process]]::new();$case=$null;$owner='';$callNo=0;New-Item -ItemType Directory -Force "$Out\calls"|Out-Null
function Check($Good,$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});"$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good-and-not$KeepGoing){throw $Name}}
function Call($T,[string[]]$Arguments){$watch=[Diagnostics.Stopwatch]::StartNew();$base=@('--json','--pid',[string]$T.pid);if($script:owner){$base+=@('--owner',$script:owner)};$raw=@(& $V @base @Arguments);$code=$LASTEXITCODE;$watch.Stop();$script:callNo++;$checkpoint=@{args=$Arguments;owner=$script:owner;exit=$code;elapsed_ms=$watch.ElapsedMilliseconds;raw=$raw};$checkpoint|ConvertTo-Json -Depth 5|Set-Content ("$Out\calls\"+$script:callNo+".json") -Encoding utf8NoBOM;$r=$checkpoint;$r.reply=($raw|ConvertFrom-Json);$script:case.calls.Add($r);return $r}
function Raw($T,$Json){Call $T @('json',$Json)}
function LogText($T){Get-Content $T.log -Raw -ErrorAction SilentlyContinue}
function Obs($T){$lines=@((LogText $T)-split'\r?\n'|Where-Object{$_-match' OBS '});if(-not$lines.Count){return @{w=0;d=0;ctrl=0;mouse=0;lx=0;ly=0;lt=0;buttons=0;frame=0}};$line=$lines[-1];$values=@{};foreach($field in @('w','d','ctrl','mouse','lx','ly','lt','buttons','frame')){if($line-match"(?: |^)$field=(-?\d+)"){$values[$field]=[int]$Matches[1]}};return $values}
function AwaitObs($T,[scriptblock]$Condition,$Timeout=2000){$until=[Environment]::TickCount64+$Timeout;do{$o=Obs $T;if((& $Condition $o)){return $o};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "actual owned consumer state not observed: $((Obs $T)|ConvertTo-Json -Compress)"}
function StartTarget($Suffix,$Name,[switch]$Render){$script:case=@{arch=$Suffix;name=$Name;calls=[Collections.Generic.List[object]]::new()};$records.Add($script:case);$log="$Out\$Name$Suffix.log";$extra=if($Render){' --render'}else{''};$p=Start-Process "$tools\vi_jobconsumer$Suffix.exe" -ArgumentList "`"$log`"$extra" -WindowStyle Hidden -PassThru;$owned.Add($p);$injection=& $V --json inject --pid $p.Id|ConvertFrom-Json;if(-not$injection.ok){throw 'injection failed'};$t=@{pid=$p.Id;log=$log;process=$p};$until=[Environment]::TickCount64+3000;do{$s=(Call $t @('status')).reply;if($s.hwnd-ne'0x0'-and$s.counters.gaks-gt0-and(-not$Render-or$s.frames-gt0)){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until);if($s.hwnd-eq'0x0'-or$s.really_foreground-or($Render-and$s.frames-le0)){throw 'owned background target not ready'};$t.h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16));return $t}
function WaitJob($T,$Id,$Timeout=6000){$until=[Environment]::TickCount64+$Timeout;do{$r=Call $T @('job-status',$Id);if($r.reply.job.job_complete){return $r.reply.job};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until);throw 'job did not reach bounded terminal state'}
function PauseRender($T,$Value){[ViJobNative]::PostMessage($T.h,0x8001,[IntPtr]$Value,[IntPtr]::Zero)|Out-Null;$until=[Environment]::TickCount64+2000;do{if((LogText $T)-match"PAUSE $Value") {return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'owned pause command unavailable'}
function PaintMode($T,$Value){$old=[regex]::Matches((LogText $T),"PAINT_ENABLED $Value").Count;[ViJobNative]::PostMessage($T.h,0x8003,[IntPtr]$Value,[IntPtr]::Zero)|Out-Null;$until=[Environment]::TickCount64+2000;do{if([regex]::Matches((LogText $T),"PAINT_ENABLED $Value").Count-gt$old){return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'owned paint mode acknowledgement unavailable'}
function PollCoverage($T){[ViJobNative]::PostMessage($T.h,0x8004,[IntPtr]1,[IntPtr]::Zero)|Out-Null;$until=[Environment]::TickCount64+2000;do{if((LogText $T)-match'POLL_COVERAGE token=1 max_gap=(\d+) samples=(\d+)'){$script:case.poll_coverage=@{max_gap_ms=[long]$Matches[1];samples=[long]$Matches[2]};return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'owned polling coverage acknowledgement unavailable'}
function StartLegacyHold($T,$Ms,$Tag){$path="$Out\$Tag$($T.pid).json";$p=Start-Process $V -ArgumentList "--json --pid $($T.pid) hold w $Ms" -RedirectStandardOutput $path -WindowStyle Hidden -PassThru;$owned.Add($p);return @{process=$p;path=$path}}
try{
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_jobconsumer.cpp" /Fo:"$tools\consumer$suffix.obj" /Fd:"$tools\consumer$suffix.pdb" /Fe:"$tools\vi_jobconsumer$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_jobretentiontest.cpp" /Fo:"$tools\retention$suffix.obj" /Fd:"$tools\retention$suffix.pdb" /Fe:"$tools\vi_jobretention$suffix.exe" || exit /b 2
"$tools\vi_jobretention$suffix.exe" || exit /b 3
exit /b 0
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture /W4 /WX"}
 foreach($suffix in @('','32')){
  if(-not$NativeOnly){
  $t=StartTarget $suffix 'gdi';$hold=StartLegacyHold $t 2000 'legacy';AwaitObs $t {$args[0].w-eq1}|Out-Null
  $capturePath="$Out\held$suffix.png";$capture=Call $t @('capture',$capturePath,'--source','printwindow');$stillWaiting=-not$hold.process.HasExited
  $pixel=$null;if(Test-Path $capturePath){$bitmap=[Drawing.Bitmap]::new($capturePath);try{$pixel=$bitmap.GetPixel(10,10)}finally{$bitmap.Dispose()}}
  Check ($capture.reply.ok-and$capture.elapsed_ms-lt1000-and$stillWaiting-and$pixel.R-gt200-and$pixel.G-lt30) "$suffix capture returns actual held-W red frame before synchronous hold completion"
  $hold.process.WaitForExit(5000)|Out-Null;$original=Get-Content $hold.path -Raw|ConvertFrom-Json;Check ($hold.process.HasExited-and$original.ok) "$suffix legacy call still replies after completion"
  AwaitObs $t {$args[0].w-eq0}|Out-Null
  $second=StartLegacyHold $t 1600 'concurrent';AwaitObs $t {$args[0].w-eq1}|Out-Null;$offset=(LogText $t).Length;$key=Call $t @('key','d','250');$overlap=(LogText $t).Substring($offset)-match'OBS w=1 d=1'
  Check ($key.reply.ok-and$key.elapsed_ms-lt900-and$overlap-and-not$second.process.HasExited) "$suffix independent D is actually read during synchronous W hold"
  $second.process.WaitForExit(5000)|Out-Null;AwaitObs $t {$args[0].w-eq0-and$args[0].d-eq0}|Out-Null
  if($Baseline){Stop-Process -Id $t.pid -Force;continue}
  $async=Call $t @('hold','w','800','--async');AwaitObs $t {$args[0].w-eq1}|Out-Null;$before=(Call $t @('trace','key','w')).reply
  $busy=Call $t @('hold','w','100','--async');$after=(Call $t @('trace','key','w')).reply
  Check ($async.reply.accepted-and$async.reply.job_id-and-not$busy.reply.ok-and$after.injected_down-eq$before.injected_down) "$suffix async overlap fails before an additional press"
  $queued=StartLegacyHold $t 120 'fifo';Start-Sleep -Milliseconds 70;$list=(Call $t @('jobs')).reply.jobs
  Check (@($list|Where-Object job_state -eq queued).Count-eq1-and-not$queued.process.HasExited) "$suffix legacy overlap queues FIFO while unrelated commands stay available"
  $first=WaitJob $t $async.reply.job_id;$queued.process.WaitForExit(3000)|Out-Null;$next=Get-Content $queued.path -Raw|ConvertFrom-Json
  Check ($first.job_state-eq'completed'-and$next.ok-and$next.job_state-eq'completed') "$suffix queued legacy work completes without automatic retry"
  $active=Call $t @('hold','w','600','--async');AwaitObs $t {$args[0].w-eq1}|Out-Null;$pending=StartLegacyHold $t 500 'pending_cancel';Start-Sleep -Milliseconds 70;$queuedList=(Call $t @('jobs')).reply.jobs;Call $t @('release')|Out-Null;$pending.process.WaitForExit(2000)|Out-Null;$pendingResult=Get-Content $pending.path -Raw|ConvertFrom-Json;Start-Sleep -Milliseconds 650
  Check (@($queuedList|Where-Object job_state -eq queued).Count-eq1-and-not$pendingResult.ok-and$pendingResult.job_state-eq'cancelled'-and(Obs $t).w-eq0) "$suffix release cancels queued-before-start work without later reassertion"
  $chord=Call $t @('hold','ctrl+w','700','--async');AwaitObs $t {$args[0].w-eq1-and$args[0].ctrl-eq1}|Out-Null
  $up=Call $t @('up','ctrl');AwaitObs $t {$args[0].w-eq0-and$args[0].ctrl-eq0}|Out-Null;$down=Call $t @('down','w');Start-Sleep -Milliseconds 850;$current=Obs $t;$old=WaitJob $t $chord.reply.job_id
  Check ($up.reply.ok-and$down.reply.ok-and$current.w-eq1-and$old.job_state-eq'cancelled') "$suffix manual up cancels the whole chord and its old deadline cannot release newer W"
  Call $t @('release')|Out-Null;AwaitObs $t {$args[0].w-eq0}|Out-Null
  $mouseJob=Call $t @('hold','lbutton','600','--async');AwaitObs $t {$args[0].mouse-eq1}|Out-Null;$mouseDown=Call $t @('mousedown','left','80','70');Start-Sleep -Milliseconds 750;$mouseOld=WaitJob $t $mouseJob.reply.job_id
  Check ($mouseDown.reply.ok-and(Obs $t).mouse-eq1-and$mouseOld.job_state-eq'cancelled') "$suffix coordinate mousedown invalidates generic mouse hold and survives its stale deadline"
  Call $t @('mouseup','left')|Out-Null;AwaitObs $t {$args[0].mouse-eq0}|Out-Null
  $cancel=Call $t @('hold','w','1000','--async');AwaitObs $t {$args[0].w-eq1}|Out-Null;$cancelled=Call $t @('job-cancel',$cancel.reply.job_id);AwaitObs $t {$args[0].w-eq0}|Out-Null
  Check ($cancelled.reply.ok-and$cancelled.reply.job.job_state-eq'cancelled') "$suffix exact job cancellation releases actual held input"
  $replaceOld=Call $t @('hold','w','900','--async');AwaitObs $t {$args[0].w-eq1}|Out-Null;$replace=Call $t @('hold','w','150','--async','--replace');$old=WaitJob $t $replaceOld.reply.job_id;$new=WaitJob $t $replace.reply.job_id
  Check ($old.job_state-eq'cancelled'-and$new.job_state-eq'completed') "$suffix explicit replacement cancels old generation and completes new job"
  Call $t @('down','w')|Out-Null;AwaitObs $t {$args[0].w-eq1}|Out-Null;$before=(Call $t @('trace','key','w')).reply;$manualBusy=Call $t @('hold','w','120','--async');$after=(Call $t @('trace','key','w')).reply
  Check (-not$manualBusy.reply.ok-and$before.injected_down-eq$after.injected_down-and(Obs $t).w-eq1) "$suffix async does not silently take over earlier manual key state"
  $manualReplace=Call $t @('hold','w','120','--async','--replace');$replacedManual=WaitJob $t $manualReplace.reply.job_id;AwaitObs $t {$args[0].w-eq0}|Out-Null
  Check ($manualReplace.reply.accepted-and$replacedManual.job_state-eq'completed') "$suffix explicit async replacement may supersede manual key state"
  $bad=Raw $t '{"cmd":"hold","key":"w","ms":200,"async":1}';$trace=(Call $t @('trace','key','w')).reply
  Check (-not$bad.reply.ok-and-not$trace.virtual_down) "$suffix numeric async option rejected before input"
  $owner='alpha';Call $t @('claim','--ttl','5')|Out-Null;$protected=Call $t @('hold','w','1200','--async');AwaitObs $t {$args[0].w-eq1}|Out-Null;$owner='beta';$foreignRelease=Call $t @('release');$foreignUp=Call $t @('up','w');$foreignCancel=Call $t @('job-cancel',$protected.reply.job_id)
  Check ($foreignRelease.reply.claim_refused-and$foreignUp.reply.claim_refused-and$foreignCancel.reply.claim_refused-and(Obs $t).w-eq1) "$suffix mutex-bypassing cleanup remains claim checked and cannot release foreign input"
  $owner='alpha';Call $t @('release')|Out-Null;Call $t @('unclaim')|Out-Null
  $owner='alpha';Call $t @('claim','--ttl','1')|Out-Null;$ttl=Call $t @('hold','w','3000','--async');AwaitObs $t {$args[0].w-eq1}|Out-Null;$expired=WaitJob $t $ttl.reply.job_id 2500;AwaitObs $t {$args[0].w-eq0}|Out-Null
  Check ($expired.job_state-eq'cancelled'-and$expired.job_reason-eq'claim_expired_or_changed') "$suffix read-only job polling does not renew TTL and expiry releases input"
  Call $t @('claim','--ttl','5')|Out-Null;$prior=Call $t @('hold','w','500','--async');AwaitObs $t {$args[0].w-eq1}|Out-Null;$owner='beta';Call $t @('claim','--ttl','5','--force-claim')|Out-Null;Call $t @('down','w')|Out-Null;Start-Sleep -Milliseconds 650;$priorDone=WaitJob $t $prior.reply.job_id
  Check ($priorDone.job_state-eq'cancelled'-and(Obs $t).w-eq1) "$suffix forced claim cancels prior work without stale release of newer owner's W"
  Call $t @('release')|Out-Null;Call $t @('unclaim')|Out-Null;$owner=''
  $pad=Call $t @('pad','--lx','7000','600','--async');AwaitObs $t {$args[0].lx-eq7000}|Out-Null;$padNew=Call $t @('pad','--lx','20000','--hold');Start-Sleep -Milliseconds 750;$padOld=WaitJob $t $pad.reply.job_id
  Check ($padNew.reply.ok-and(Obs $t).lx-eq20000-and$padOld.job_state-eq'cancelled') "$suffix manual pad override survives stale timed-pad deadline"
  $manualPadBusy=Call $t @('pad','--lx','7000','120','--async');$manualPadReplace=Call $t @('pad','--lx','7000','120','--async','--replace');$manualPadDone=WaitJob $t $manualPadReplace.reply.job_id
  Check (-not$manualPadBusy.reply.ok-and$manualPadReplace.reply.accepted-and$manualPadDone.job_state-eq'completed') "$suffix async pad takeover requires explicit replacement of earlier manual pad state"
  Call $t @('pad','off')|Out-Null;AwaitObs $t {$args[0].lx-eq0}|Out-Null
  PaintMode $t 0 # Polling oracle runs without blocking bitmap publication during PWM/axis-only controls.
  $offset=(LogText $t).Length;$pwm=Call $t @('pulse','w','650','--period-ms','100','--duty','25');AwaitObs $t {$args[0].w-eq0}|Out-Null;PollCoverage $t;$transitions=@([regex]::Matches((LogText $t).Substring($offset),'(?m)^(\d+) OBS w=(\d)'))
  $on=@();$off=@();for($i=1;$i-lt$transitions.Count;$i++){$delta=[long]$transitions[$i].Groups[1].Value-[long]$transitions[$i-1].Groups[1].Value;if($transitions[$i-1].Groups[2].Value-eq'1'){$on+=$delta}else{$off+=$delta}}
  $onAvg=if($on.Count){($on|Measure-Object -Average).Average}else{0};$offAvg=if($off.Count){($off|Measure-Object -Average).Average}else{0}
  Check ($pwm.reply.ok-and$on.Count-ge4-and$off.Count-ge3-and$onAvg-ge10-and$onAvg-lt90-and$offAvg-gt$onAvg-and(Obs $t).w-eq0) "$suffix actual key reader observes bounded25percent PWM and final release"
  $none=Call $t @('pad-ramp','lx','0','12000','200');$trace=(Call $t @('status')).reply
  Check (-not$none.reply.ok-and(Obs $t).lx-eq0) "$suffix frame steering refuses no-Present target before input"
  $offset=(LogText $t).Length;$ramp=Call $t @('pad-ramp','lx','0','16000','240','--ly','1234','--pacing','time','--end-hold-ms','80');AwaitObs $t {$args[0].lx-eq0}|Out-Null;$samples=(LogText $t).Substring($offset)
  Check ($ramp.reply.ok-and$ramp.reply.pad_updates-ge4-and$samples-match'lx=16000 ly=1234'-and(Obs $t).lx-eq0) "$suffix explicit time ramp reaches actual coherent XInput endpoint and releases"
  $timelinePath="$Out\timeline$suffix.json";'[{"at_ms":0,"lx":4000,"ly":8000},{"at_ms":100,"lx":9000,"ly":18000},{"at_ms":200,"lx":12000,"ly":24000}]'|Set-Content $timelinePath -Encoding utf8NoBOM
  $offset=(LogText $t).Length;$timeline=Call $t @('pad-timeline',$timelinePath,'--pacing','time','--end-hold-ms','70');$samples=(LogText $t).Substring($offset)
  Check ($timeline.reply.ok-and$timeline.reply.pad_updates-eq3-and$samples-match'lx=4000 ly=8000'-and$samples-match'lx=9000 ly=18000'-and$samples-match'lx=12000 ly=24000') "$suffix strict parsed timeline publishes actual coherent tuples in order"
  $invalids=@('[]','[{"at_ms":1,"lx":1}]','[{"at_ms":0,"lx":1,"lx":2}]','[{"at_ms":0,"lx":40000}]','[{"at_ms":0},{"at_ms":0}]','[{"at_ms":0,"bad":1}]','[{"at_ms":0,"lx":+1}]','[{"at_ms":0}] garbage','[{"at_ms":0,"buttons":"\q"}]')
  foreach($invalid in $invalids){$json=@{cmd='pad-timeline';states_json=$invalid;pacing='time'}|ConvertTo-Json -Compress;$bad=Raw $t $json;Check (-not$bad.reply.ok-and(Obs $t).lx-eq0) "$suffix malformed timeline rejected before input: $invalid"}
  PaintMode $t 1
  $holdWhile=Call $t @('hold-while','w','--until-text','HELD W','--timeout-ms','3000');AwaitObs $t {$args[0].w-eq0}|Out-Null
  Check ($holdWhile.reply.ok-and$holdWhile.reply.expectation_met-and$holdWhile.reply.cleanup_confirmed-and$holdWhile.reply.hold_ready) "$suffix hold-while observes fresh owned UI and cancels its exact bounded job"
  $miss=Call $t @('hold-while','w','--until-text','THIS TEXT IS ABSENT','--timeout-ms','300');AwaitObs $t {$args[0].w-eq0}|Out-Null
  Check (-not$miss.reply.ok-and-not$miss.reply.expectation_met-and$miss.reply.cleanup_confirmed) "$suffix missed hold-while observation still releases input"
  $before=(Call $t @('trace','key','w')).reply;$badExpect=Call $t @('key','w','--async','--expect-text','HELD W');$after=(Call $t @('trace','key','w')).reply
  Check (-not$badExpect.reply.ok-and$after.injected_down-eq$before.injected_down) "$suffix async plus synchronous text postcondition rejected before original press"
  $badDown=Call $t @('down','w','--async');$trace=(Call $t @('trace','key','w')).reply
  Check (-not$badDown.reply.ok-and-not$trace.virtual_down) "$suffix unsupported async manual down rejected before input"
  foreach($key in @('a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','0','1','2','3','4','5')){$q=Call $t @('hold',$key,'5000','--async');if(-not$q.reply.ok){throw 'bounded queue setup rejected unexpectedly'}}
  $overflow=Call $t @('hold','6','5000','--async');$live=@((Call $t @('jobs')).reply.jobs|Where-Object{-not$_.job_complete});Call $t @('release')|Out-Null
  Check (-not$overflow.reply.ok-and$live.Count-eq32-and@((Call $t @('status')).reply.virtual_down).Count-eq0) "$suffix capacity32 refuses33rd resource and release cancels all jobs"
  Stop-Process -Id $t.pid -Force
  }
  if($SkipRender){continue}
  $t=StartTarget $suffix 'native' -Render;PaintMode $t 0
  # Native GPU presentation and XInput are the witnesses in this phase. Avoid
  # synchronous GDI UpdateWindow work after the observer logs a state transition.
  $offset=(LogText $t).Length;$r=Call $t @('pad-ramp','lx','0','18000','240','--ly','4321','--end-hold-ms','80');$samples=(LogText $t).Substring($offset)
  Check ($r.reply.ok-and$r.reply.frame_paced-and$r.reply.pad_updates-ge4-and$r.reply.last_frame-gt$r.reply.first_frame-and$samples-match'lx=18000 ly=4321') "$suffix native frame checkpoint drives actual XInput ramp endpoint"
  [ViJobNative]::PostMessage($t.h,0x8002,[IntPtr]400,[IntPtr]::Zero)|Out-Null
  $until=[Environment]::TickCount64+2000;do{if((LogText $t)-match'FRAME_MS 400'){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
  if((LogText $t)-notmatch'FRAME_MS 400'){throw 'owned slow native-frame acknowledgement unavailable'}
  $timelinePath="$Out\frame_timeline$suffix.json";'[{"at_ms":0,"lx":4000,"ly":8000},{"at_ms":100,"lx":9000,"ly":18000},{"at_ms":200,"lx":12000,"ly":24000}]'|Set-Content $timelinePath -Encoding utf8NoBOM
  $strict=Call $t @('pad-timeline',$timelinePath,'--strict-frames');AwaitObs $t {$args[0].lx-eq0}|Out-Null
  Check (-not$strict.reply.ok-and$strict.reply.job_reason-eq'timeline_states_missed'-and$strict.reply.skipped_states-ge1) "$suffix strict native timeline fails and releases when actual frames miss discrete states"
  $offset=(LogText $t).Length;$coalesced=Call $t @('pad-timeline',$timelinePath,'--ms','1500');$samples=(LogText $t).Substring($offset)
  Check ($coalesced.reply.ok-and$coalesced.reply.skipped_states-ge1-and$coalesced.reply.pad_updates-eq2-and$coalesced.reply.max_lateness_ms-ge150-and$samples-match'lx=12000 ly=24000') "$suffix native timeline coalesces late states explicitly without a catch-up burst"
  [ViJobNative]::PostMessage($t.h,0x8002,[IntPtr]16,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 70
  $stall=Call $t @('pad-ramp','lx','5000','18000','400','--async');AwaitObs $t {$args[0].lx-ge5000}|Out-Null;PauseRender $t 1;$s1=(Call $t @('status')).reply;Start-Sleep -Milliseconds 100;$s2=(Call $t @('status')).reply;$stalled=WaitJob $t $stall.reply.job_id 3500;AwaitObs $t {$args[0].lx-eq0}|Out-Null
  Check ($s1.frames-eq$s2.frames-and$stalled.job_state-eq'failed'-and$stalled.job_reason-eq'native_present_stalled') "$suffix actual stopped native rendering still gets real-clock pad cleanup"
  Stop-Process -Id $t.pid -Force
 }
}finally{foreach($p in $owned){if(-not$p.HasExited){Stop-Process -Id $p.Id -Force}};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 17|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$fail=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$fail) PASS / $fail FAIL";if($fail){exit 1};$global:LASTEXITCODE=0
