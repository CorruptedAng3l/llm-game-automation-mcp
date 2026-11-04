# Background owned XInput/Present receiver. Actual reader samples are retained before assertions.
param([string]$Out="$PSScriptRoot\..\build\test\pad_phases",[string]$Vinput="$PSScriptRoot\..\dist\vinput.exe",[switch]$KeepGoing,[switch]$BaselineOnly)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path $Vinput).Path
$Out=[IO.Path]::GetFullPath($Out);if(-not$Out.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture output must stay inside this checkout'}
New-Item -ItemType Directory -Force $Out|Out-Null;$run=[guid]::NewGuid().ToString('N');$tools="$Out\tools_$run";New-Item -ItemType Directory -Force "$tools\temp","$Out\calls"|Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$failures=0;$completed=$false;$owned=$null;$serial=0
function Check($Good,$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good){$script:failures++;if(-not$KeepGoing){throw $Name}}}
if(-not('ViPhaseNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViPhaseNative{[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);[DllImport("user32.dll",SetLastError=true)]public static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,UIntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);}' }
function Call([int]$TargetPid,[string[]]$Arguments,[int]$Budget=10000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$V;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in @('--json','--pid',[string]$TargetPid)+$Arguments){$info.ArgumentList.Add([string]$arg)}
 $proc=[Diagnostics.Process]::new();$proc.StartInfo=$info
 try{if(-not$proc.Start()){throw 'owned CLI failed to start'};$stdout=$proc.StandardOutput.ReadToEndAsync();$stderr=$proc.StandardError.ReadToEndAsync();if(-not$proc.WaitForExit($Budget)){$proc.Kill();$proc.WaitForExit();throw 'owned CLI deadline exceeded'}
  $text=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult();$lines=@($text-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
  $script:serial++;$record=@{arguments=$Arguments;exit=$proc.ExitCode;stdout=$text;stderr=$err;reply=$reply};$record|ConvertTo-Json -Depth 20|Set-Content "$Out\calls\$serial.json" -Encoding utf8NoBOM
  Check ($lines.Count-eq1-and$null-ne$reply) "one JSON reply: $($Arguments[0])";return $record
 }finally{$proc.Dispose()}
}
function Rows($Target,[int]$Token){
 @(foreach($line in (Get-Content -LiteralPath $Target.log -ErrorAction SilentlyContinue)){if($line-match'^OBS token=(\d+) tick=(\d+) frame=(\d+) rc=(\d+) b=(\d+) lt=(\d+) rt=(\d+) lx=(-?\d+) ly=(-?\d+) rx=(-?\d+) ry=(-?\d+) axis_frame=(\d+) violations=(\d+) stage=(\d+) selections=(\d+)'){
  if([int]$Matches[1]-eq$Token){[pscustomobject]@{token=[int]$Matches[1];tick=[long]$Matches[2];frame=[long]$Matches[3];rc=[long]$Matches[4];buttons=[int]$Matches[5];lt=[int]$Matches[6];rt=[int]$Matches[7];lx=[int]$Matches[8];ly=[int]$Matches[9];rx=[int]$Matches[10];ry=[int]$Matches[11];axis_frame=[long]$Matches[12];violations=[int]$Matches[13];stage=[int]$Matches[14];selections=[int]$Matches[15];line=$line}}
 }})
}
function Control($Target,[uint32]$Message,[uint32]$Value){
 [uint32]$windowPid=0;$thread=[ViPhaseNative]::GetWindowThreadProcessId([IntPtr]$Target.hwnd,[ref]$windowPid)
 if(-not$thread-or$windowPid-ne$Target.pid){throw 'owned fixture HWND/PID validation failed before control message'}
 [UIntPtr]$result=[UIntPtr]::Zero;$sent=[ViPhaseNative]::SendMessageTimeoutW([IntPtr]$Target.hwnd,$Message,[UIntPtr]$Value,[IntPtr]::Zero,2,500,[ref]$result)
 if($sent-eq[IntPtr]::Zero-or$result.ToUInt64()-ne1){throw 'owned fixture control failed or exceeded500ms'}
}
function Reset-Case($Target,[int]$Token){Control $Target 0x8001 ([uint32]$Token);Start-Sleep -Milliseconds 30}
function Wire($States,[int]$Duration,[int]$Stagger=100,[string]$Pacing='frames',[bool]$Async=$false){@{cmd='pad-timeline';states_json=$States;ms=$Duration;stagger_ms=$Stagger;pacing=$Pacing;async=$Async;strict_frames=($Pacing-eq'frames')}|ConvertTo-Json -Compress}
function Wait-Rows($Target,[int]$Token,[scriptblock]$Predicate,[int]$Budget=2000){
 $until=[Environment]::TickCount64+$Budget;do{$rows=@(Rows $Target $Token);$found=@($rows|Where-Object $Predicate);if($found.Count){return $found};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "actual receiver witness missing for token $Token"
}
function Wait-Job($Target,[string]$Id,[int]$Budget=6000){
 $until=[Environment]::TickCount64+$Budget;do{$r=Call $Target.pid @('job-status',$Id);if($r.reply.job.job_complete){return $r.reply.job};Start-Sleep -Milliseconds 30}while([Environment]::TickCount64-lt$until);throw 'owned job failed to become terminal before deadline'
}
function Positive-PhaseTiming($Target,[int]$Token){
 $rows=@(Rows $Target $Token);$segments=[Collections.Generic.List[object]]::new();$maximumMean=0.0
 for($index=1;$index-lt$rows.Count;$index++){
  $previous=$rows[$index-1];$current=$rows[$index];$frames=$current.frame-$previous.frame;$elapsed=$current.tick-$previous.tick
  if($frames-gt0-and$elapsed-gt0){$mean=[double]$elapsed/$frames;$maximumMean=[math]::Max($maximumMean,$mean);$segments.Add(@{first=$previous;last=$current;frames=$frames;elapsed_ms=$elapsed;segment_mean_ms_per_target_frame=$mean})}
 }
 # State-change samples give segment means, not the worst per-frame gap. Keep a
 # 1s jitter floor and32 average target-frame periods for each positive phase.
 $dwell=[int][math]::Ceiling([math]::Max(1000,32*$maximumMean))
 $timing=@{name='named-positive timing plan';source_token=$Token;segment_count=$segments.Count;segments=$segments;maximum_segment_mean_ms_per_target_frame=$maximumMean;frame_budget=32;minimum_dwell_ms=1000;maximum_dwell_ms=4000;phase_dwell_ms=$dwell;evidence_kind='actual reader state-sample segment means; not a worst-frame-gap guarantee'}
 $Target.evidence.Add($timing)
 Check ($segments.Count-gt0-and$dwell-le4000) "$($Target.arch) positive phase timing has actual target-frame evidence within bounded4000ms dwell"
 if(-not$segments.Count-or$dwell-gt4000){throw 'positive fixture cannot schedule without measured cadence inside its bounded dwell budget'}
 return $timing
}
function Verify-Radial($Target,$Record,[int]$Token,$Name,[bool]$Native=$true){
 Start-Sleep -Milliseconds 80;$rows=@(Rows $Target $Token);$Target.evidence.Add(@{name=$Name;reply=$Record.reply;samples=$rows})
 Check ($Record.exit-eq0-and$Record.reply.job_state-eq'completed') "$Name completed"
 $pressed=@($rows|Where-Object{$_.buttons-eq0x1000-or$_.rt-eq255});$aim=@($rows|Where-Object{$_.lx-eq5000-and$_.buttons-eq0-and$_.rt-eq0});$released=@($rows|Where-Object{$_.stage-ge3});$selected=@($rows|Where-Object selections -ge 1)
 Check ($pressed.Count-gt0-and$aim.Count-gt0-and$released.Count-gt0-and$selected.Count-gt0) "$Name actual reader saw aim, confirm, confirm-up while stick held, then neutral"
 if($Native){Check ($pressed.Count-gt0-and@($pressed|Where-Object{$_.violations-ne0-or$_.frame-le$_.axis_frame}).Count-eq0) "$Name new press follows a separately observed target Present frame"}
}
try{
 foreach($arch in @('x64','x86')){
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'}
  $batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_pad_phaseconsumer.cpp" /Fo:"$tools\reader$suffix.obj" /Fe:"$tools\reader$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@
  [IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch phase receiver compiles /W4 /WX"
  $target=@{arch=$arch;pid=0;hwnd=0;log="$Out\$arch.$run.target.log";evidence=[Collections.Generic.List[object]]::new()};$cases.Add($target)
  $owned=Start-Process "$tools\reader$suffix.exe" -ArgumentList "`"$($target.log)`"" -WindowStyle Hidden -PassThru;$target.pid=$owned.Id
  try{
   $until=[Environment]::TickCount64+5000;do{$ready=Get-Content $target.log -ErrorAction SilentlyContinue|Where-Object{$_-match'^READY '};if($ready-and$ready-match'hwnd=(\d+)'){$target.hwnd=[long]$Matches[1];break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until);Check ($target.hwnd-ne0) "$arch owns responsive offscreen receiver"
   $injected=Call $target.pid @('inject') 30000;Check ($injected.exit-eq0-and$injected.reply.ok) "$arch inject readiness"
   $until=[Environment]::TickCount64+5000;do{$status=Call $target.pid @('status');if($status.reply.present_ours-gt0-or$status.reply.frames-gt0-or$status.reply.present-gt0){break};Start-Sleep -Milliseconds 50}while([Environment]::TickCount64-lt$until)
   Check ($status.reply.ok-and-not$status.reply.really_foreground) "$arch background target stays unfocused"
   Reset-Case $target 1
   $states='[{"at_ms":0,"buttons":"a","lx":5000},{"at_ms":300,"buttons":"none","lx":5000},{"at_ms":450,"buttons":"none","lx":0}]'
   $firstStates=$states;$firstDuration=600
   if(-not$BaselineOnly){$firstDwell=1000;$firstStates=@(@{at_ms=0;buttons='a';lx=5000},@{at_ms=$firstDwell;buttons='none';lx=5000},@{at_ms=2*$firstDwell;buttons='none';lx=0})|ConvertTo-Json -Compress;$firstDuration=3*$firstDwell;$target.evidence.Add(@{name='same-point positive dwell floor';phase_dwell_ms=$firstDwell;source='bounded positive fixture floor; historical BaselineOnly request stays600ms'})}
   $record=Call $target.pid @('json',(Wire $firstStates $firstDuration)) ($firstDuration+6000);Verify-Radial $target $record 1 "$arch same-point axes+A stagger"
   if(-not$BaselineOnly){
    Check ($record.reply.stagger_enabled-and$record.reply.stagger_ms-eq100-and$record.reply.phase_publications.Count-eq3) "$arch explicit stagger and actual phase publications reported"
    $timing=Positive-PhaseTiming $target 1;$phaseDwell=[int]$timing.phase_dwell_ms
    Reset-Case $target 2
    $named=@(@{at_ms=0;phase='aim';buttons='none';lx=5000},@{at_ms=$phaseDwell;phase='confirm';buttons='a';lx=5000},@{at_ms=2*$phaseDwell;phase='confirm_released';buttons='none';lx=5000},@{at_ms=3*$phaseDwell;phase='neutral';buttons='none';lx=0})|ConvertTo-Json -Compress
    $namedDuration=4*$phaseDwell;$record=Call $target.pid @('json',(Wire $named $namedDuration 60)) ($namedDuration+6000);Verify-Radial $target $record 2 "$arch one-job named radial phases"
    Check ($record.reply.phase_publications.Count-eq4-and$record.reply.phase_publications[2].phase-eq'confirm_released'-and$record.reply.phase_publications[2].published) "$arch named button release has an actual publication receipt"
    $phases=@($record.reply.phase_publications);$ordered=$phases.Count-eq4-and$record.reply.skipped_states-eq0-and$record.reply.frame_paced
    for($index=0;$index-lt$phases.Count;$index++){$ordered=$ordered-and$phases[$index].published;if($index){$ordered=$ordered-and$phases[$index].published_present-gt$phases[$index-1].published_present-and$phases[$index].published_at_ms-gt$phases[$index-1].published_at_ms}}
    Check $ordered "$arch strict positive publishes every named phase at an advancing actual Present with no skipped state"
    $released=@(Rows $target 2|Where-Object{$_.stage-eq3-and$_.lx-eq5000-and$_.buttons-eq0});$neutral=@(Rows $target 2|Where-Object{$_.stage-eq4-and$_.lx-eq0-and$_.buttons-eq0});$target.evidence.Add(@{name='named release-before-neutral frame evidence';release=$released;neutral=$neutral;requested_phase_dwell_ms=$phaseDwell})
    Check ($released.Count-gt0-and$neutral.Count-gt0-and$neutral[0].frame-gt$released[0].frame-and$neutral[0].tick-gt$released[0].tick) "$arch actual reader sees button-up with stick held before a later-frame neutral"
    $positiveStates=@(@{at_ms=0;buttons='a';lx=5000},@{at_ms=$phaseDwell;buttons='none';lx=5000},@{at_ms=2*$phaseDwell;buttons='none';lx=0})|ConvertTo-Json -Compress
    $positiveDuration=3*$phaseDwell
    Reset-Case $target 3;$triggers=@(@{at_ms=0;rt=255;lx=5000},@{at_ms=$phaseDwell;rt=0;lx=5000},@{at_ms=2*$phaseDwell;rt=0;lx=0})|ConvertTo-Json -Compress
    $record=Call $target.pid @('json',(Wire $triggers $positiveDuration)) ($positiveDuration+6000);Verify-Radial $target $record 3 "$arch trigger press is staggered after movement axes"
    # A real stopped renderer must withhold the pending press, even after its wall deadline.
    $guarded='[{"at_ms":0,"phase":"confirm","buttons":"a","lx":5000},{"at_ms":1200,"phase":"up","buttons":"none","lx":5000},{"at_ms":1500,"phase":"neutral","buttons":"none","lx":0}]'
    $guardRelease=[Math]::Max(1200,$phaseDwell);$guardNeutral=$guardRelease+$phaseDwell;$guardDuration=$guardNeutral+[Math]::Max(200,[int]($phaseDwell/2))
    $guardedPositive=@(@{at_ms=0;phase='confirm';buttons='a';lx=5000},@{at_ms=$guardRelease;phase='up';buttons='none';lx=5000},@{at_ms=$guardNeutral;phase='neutral';buttons='none';lx=0})|ConvertTo-Json -Compress
    Reset-Case $target 4;$ack=Call $target.pid @('json',(Wire $guardedPositive $guardDuration 800 'frames' $true));Check ($ack.reply.accepted-and$ack.reply.job_id-and$null-eq$ack.reply.input_injected) "$arch async admission does not claim a delivered press"
    Wait-Rows $target 4 {$_.lx-eq5000-and$_.buttons-eq0}|Out-Null;Control $target 0x8002 0
    $pauseBefore=Call $target.pid @('status');Start-Sleep -Milliseconds 900;$pauseAfter=Call $target.pid @('status');$pausedRows=@(Rows $target 4);$target.evidence.Add(@{name='paused pending guard';rows=$pausedRows;before=$pauseBefore.reply;after=$pauseAfter.reply})
    Check ($pauseBefore.reply.frames-eq$pauseAfter.reply.frames-and@($pausedRows|Where-Object buttons -ne 0).Count-eq0) "$arch paused native Present never publishes pending A after time-only expiry"
    Control $target 0x8002 1;$job=Wait-Job $target $ack.reply.job_id
    Verify-Radial $target @{exit=0;reply=$job} 4 "$arch resumed renderer preserves later phase dwell"
    Check ($job.timeline_shift_ms-ge800-and$job.phase_publications[0].press_after_axis_present-eq$true) "$arch delayed publication reports shift and observed later Present"
    $phases=$job.phase_publications;Check (($phases[1].published_at_ms-$phases[0].published_at_ms)-ge($guardRelease-100)) "$arch guard delay retains requested bounded confirm phase"
    Reset-Case $target 5;$ack=Call $target.pid @('json',(Wire $guarded 1700 800 'frames' $true));Wait-Rows $target 5 {$_.lx-eq5000-and$_.buttons-eq0}|Out-Null;Control $target 0x8002 0
    $watch=[Diagnostics.Stopwatch]::StartNew();$job=Wait-Job $target $ack.reply.job_id 4000;$watch.Stop();$rows=@(Rows $target 5);$target.evidence.Add(@{name='native stall';job=$job;rows=$rows;watch_ms=$watch.ElapsedMilliseconds})
    Check ($job.job_state-eq'failed'-and$job.job_reason-eq'stagger_present_stalled'-and$watch.ElapsedMilliseconds-lt3500-and@($rows|Where-Object buttons -ne 0).Count-eq0) "$arch stalled native renderer fails boundedly without a pending press"
    Check ($job.cleanup_neutral_published-and$job.last_published_pad.buttons-eq0-and$job.last_published_pad.lx-eq0) "$arch stalled job reports its actual owned neutral cleanup"
    Control $target 0x8002 1
    Reset-Case $target 6;$ack=Call $target.pid @('json',(Wire $guarded 1700 1000 'frames' $true));Wait-Rows $target 6 {$_.lx-eq5000-and$_.buttons-eq0}|Out-Null
    $cancel=Call $target.pid @('job-cancel',$ack.reply.job_id);$job=Wait-Job $target $ack.reply.job_id;Start-Sleep -Milliseconds 1050;$rows=@(Rows $target 6);$target.evidence.Add(@{name='cancel pending press';job=$job;rows=$rows})
    Check ($cancel.reply.ok-and$job.job_state-eq'cancelled'-and@($rows|Where-Object buttons -ne 0).Count-eq0) "$arch canceling prepared axes cannot revive a pending A"
    Reset-Case $target 7;$ack=Call $target.pid @('json',(Wire $guarded 1700 1000 'frames' $true));Wait-Rows $target 7 {$_.lx-eq5000-and$_.buttons-eq0}|Out-Null
    Call $target.pid @('pad','b','--lx','777','--hold')|Out-Null;$job=Wait-Job $target $ack.reply.job_id;Start-Sleep -Milliseconds 1050;$rows=@(Rows $target 7);$last=$rows|Select-Object -Last 1;$target.evidence.Add(@{name='manual generation takeover';job=$job;rows=$rows})
    Check ($job.job_state-eq'cancelled'-and$job.job_reason-eq'manual_pad_override'-and$last.buttons-eq0x2000-and$last.lx-eq777-and@($rows|Where-Object buttons -eq 0x1000).Count-eq0) "$arch stale pending job leaves newer manual pad generation intact"
    Call $target.pid @('release')|Out-Null
    Reset-Case $target 8;Call $target.pid @('pad','b','--lx','777','--hold')|Out-Null
    $invalids=@(
     @('pad','a','200','--stagger-ms','60'),@('hold','W','200','--stagger-ms','60'),@('scroll','down','1','--stagger-ms','60'),@('inject','--stagger-ms','60'),
     @('json','{"cmd":"pad","buttons":"a","ms":200,"stagger_ms":60}'),
     @('json',(@{cmd='pad-timeline';states_json=$states;pacing='frames';stagger_ms=-1}|ConvertTo-Json -Compress)),
     @('json',(@{cmd='pad-timeline';states_json=$states;pacing='frames';stagger_ms=1.5}|ConvertTo-Json -Compress)),
     @('json',(@{cmd='pad-timeline';states_json=$states;pacing='frames';stagger_ms=1001}|ConvertTo-Json -Compress)),
     @('json',(@{cmd='pad-timeline';states_json=$states;pacing='frames';stagger_ms=$true}|ConvertTo-Json -Compress)),
     @('json',(@{cmd='pad-timeline';states_json=$states;pacing='frames';stagger_ms='100'}|ConvertTo-Json -Compress))
    )
    foreach($label in @('','bad space',('x'*33),"bad`nline")){$badStates=@(@{at_ms=0;phase=$label;buttons='a';lx=5000})|ConvertTo-Json -Compress -AsArray;$invalids+=,@('json',(@{cmd='pad-timeline';states_json=$badStates;pacing='frames';stagger_ms=60}|ConvertTo-Json -Compress))}
    foreach($invalid in $invalids){$r=Call $target.pid $invalid;Check ($r.exit-ne0-and-not$r.reply.ok-and-not$r.reply.accepted) "$arch invalid stagger or phase rejected before admission"}
    $rows=@(Rows $target 8);$last=$rows|Select-Object -Last 1;$target.evidence.Add(@{name='invalid request sentinel';rows=$rows});Check ($last.buttons-eq0x2000-and$last.lx-eq777-and@($rows|Where-Object buttons -eq 0x1000).Count-eq0) "$arch invalid requests preserve actual held sentinel state"
    Call $target.pid @('release')|Out-Null
    Reset-Case $target 9
    $largeStates=@(for($index=0;$index-lt256;$index++){@{at_ms=$index;phase=('phase_'+$index.ToString('D3')+('_'*23));buttons='none';lx=0}})|ConvertTo-Json -Compress
    $ack=Call $target.pid @('json',(Wire $largeStates 600 0 'time' $true));Check ($ack.reply.accepted-and-not$ack.reply.phase_publications) "$arch large timeline admission omits the detailed phase journal"
    $job=Wait-Job $target $ack.reply.job_id;$journalJson=$job|ConvertTo-Json -Depth 15 -Compress
    Check ($job.job_state-eq'completed'-and$job.phase_publications.Count-eq256-and$journalJson.Length-gt65536) "$arch generic job-status receives and parses all256 phase records beyond64KiB"
   }
  }finally{if($owned-and-not$owned.HasExited){Stop-Process -Id $owned.Id -Force};$owned=$null}
  if(-not$BaselineOnly){
   $target=@{arch="$arch-time";pid=0;hwnd=0;log="$Out\$arch.time.$run.target.log";evidence=[Collections.Generic.List[object]]::new()};$cases.Add($target)
   $owned=Start-Process "$tools\reader$suffix.exe" -ArgumentList "`"$($target.log)`" --no-render" -WindowStyle Hidden -PassThru;$target.pid=$owned.Id
   try{
    $until=[Environment]::TickCount64+5000;do{$ready=Get-Content $target.log -ErrorAction SilentlyContinue|Where-Object{$_-match'^READY '};if($ready-and$ready-match'hwnd=(\d+)'){$target.hwnd=[long]$Matches[1];break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
    if(-not('ViPadPhasePeer'-as[type])){Add-Type -Path "$PSScriptRoot\vi_padphase_peer.cs"}
    foreach($escaped in @('{"cmd":"pad\u002dtimeline","states_json":"[{\"at_ms\":0,\"buttons\":\"a\"}]","stagger_ms":60}', '{"cmd":"pad-timeline","states_json":"[{\"at_ms\":0,\"buttons\":\"a\"}]","stagger\u005fms":60}')){
     $peer=[ViPadPhasePeer]::Start($target.pid,'{"ok":true}');$r=Call $target.pid @('json',$escaped);$wire=$peer.GetAwaiter().GetResult();$target.evidence.Add(@{name='old capability peer';wire=$wire;reply=$r.reply})
     Check ($r.exit-ne0-and$r.reply.input_injected-eq$false-and$r.reply.pad_timeline_support_checked-eq$true-and($wire|ConvertFrom-Json).cmd-eq'ping') "$arch escaped JSON cannot bypass old-target capability refusal before original input"
    }
    $r=Call $target.pid @('inject') 30000;Check ($r.reply.ok) "$arch non-rendering Win32 reader readiness"
    Reset-Case $target 1;$r=Call $target.pid @('json',(Wire $positiveStates $positiveDuration 150 'time')) ($positiveDuration+6000);Verify-Radial $target $r 1 "$arch explicit time-only stagger" $false
    $target.evidence.Add(@{name='time-only receipt';reply=$r.reply})
    Check ($null-eq$r.reply.phase_publications[0].press_after_axis_present-and$r.reply.phase_publications[0].published_present-eq0-and$r.reply.timeline_shift_ms-ge150) "$arch no-frame path reports time staggering with no frame-separation claim"
   }finally{if($owned-and-not$owned.HasExited){Stop-Process -Id $owned.Id -Force};$owned=$null}
  }
 }
 $completed=$true
}catch{$checks.Add(@{pass=$false;name=$_.Exception.Message});$failures++;Write-Host $_.Exception.Message}
finally{
 if($owned-and-not$owned.HasExited){Stop-Process -Id $owned.Id -Force}
 foreach($case in $cases){$case.all_samples=if(Test-Path $case.log){@(Get-Content $case.log)}else{@()}}
 @{schema=1;completed=$completed;status=if($failures){'failed'}else{'passed'};failed_check_count=$failures;not_measured_check_count=0;positive_case_count=@($checks|Where-Object pass).Count;checks=$checks;cases=$cases;tool_issues=@()}|ConvertTo-Json -Depth 25|Set-Content "$Out\result.json" -Encoding utf8NoBOM
}
if($failures-or-not$completed){exit 1};exit 0
