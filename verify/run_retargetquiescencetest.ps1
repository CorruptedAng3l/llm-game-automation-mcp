# Conservative retarget refusal contract; exact original F6 observer/support bytes.
# Lower write/foreground spies are terminal. Outer recursion is bounded.
param([string]$RepoRoot='', [string]$CliDir='', [string]$Out="$PSScriptRoot\..\build\test\retarget_quiescence")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
. "$PSScriptRoot\vi_retargetrelease_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_retargetrelease_native.cs"}
if(-not$RepoRoot){$d=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($i=0;$i-lt7-and$d;$i++){if(Test-Path (Join-Path $d.FullName 'build\x64\vimh_buffer.obj')){$RepoRoot=$d.FullName;break};$d=$d.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot for tested MinHook objects'};$RepoRoot=(Resolve-Path $RepoRoot).Path;if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$V=(Resolve-Path (Join-Path $CliDir 'vinput.exe')).Path
$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $run,"$run\calls","$run\tools"|Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$owned=$null;$serial=0;$token=0;$complete=$false;$scopeComplete=$true;$completedModes=[Collections.Generic.List[object]]::new();$failures=0
function Check($Good,[string]$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good){$script:failures++}}
function Require($Good,[string]$Name){Check $Good $Name;if(-not$Good){throw $Name}}
if(-not('ViWndProcNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViWndProcNative{[DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();}'}
function Text($T){if(Test-Path $T.log){Get-Content -LiteralPath $T.log -Raw}else{''}}
function Records($T){foreach($line in ((Text $T)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Await($T,[string]$Event,[long]$Token,[int]$Budget=4000){$end=[Environment]::TickCount64+$Budget;do{AssertOwned $T 'await';foreach($r in @(Records $T)){if($r.event-eq$Event-and$r.token-eq$Token){return $r}};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end);throw "Missing $Event token$Token acknowledgement"}
function AssertOwned($T,[string]$Stage='lifecycle'){
 if(-not$T-or-not$T.process-or-not$T.created-or-not$T.retained_handle){throw 'Owned retained identity unavailable'}
 $row=ReadRetainedImage $T.process $T.path $T.created $T.retained_handle
 $T.latest_lifecycle_identity=$row;$T.identity_checks++
 $T.identity_observations.Add(@{stage=$Stage;identity=$row});if($T.identity_observations.Count-gt32){$T.identity_observations.RemoveAt(0);$T.identity_observations_dropped++}
 if(-not$row.matched-or$T.process.HasExited){throw ('Owned retained kernel identity failed: '+$row.reason)}
}
function Control($T,[uint32]$Message,[string]$Event){AssertOwned $T 'before_control_owner';[uint32]$owner=0;$thread=[ViWndProcNative]::GetWindowThreadProcessId($T.control,[ref]$owner);if(-not$thread-or$owner-ne$T.process.Id-or$thread-ne$T.thread){throw 'Owned control HWND/PID/thread changed'};AssertOwned $T 'before_control_post';$script:token++;if(-not[ViWndProcNative]::PostMessageW($T.control,$Message,[UIntPtr]::new([uint64]$script:token),[IntPtr]::Zero)){throw 'Owned control post failed'};return Await $T $Event $script:token}
function Snap($T){Control $T 0x8094 snapshot}
function Foreground{[uint32]$owner=0;$h=[ViWndProcNative]::GetForegroundWindow();[void][ViWndProcNative]::GetWindowThreadProcessId($h,[ref]$owner);return @{hwnd=$h.ToInt64();pid=$owner;read_only=$true}}
function Reap($P,[string]$Expected,$Created,[long]$Retained){
 $r=@{reaped=$false;identity_verified=$false;exit_known=$false;native_exit_code=$null;error=$null;deadline_ms=1500;termination_attempted=$false;identity_source='retained handle PID + creation + nonempty exact QueryFullProcessImageNameW'}
 try{
  $r.identity=ReadRetainedImage $P $Expected $Created $Retained
  $r.identity_verified=[bool]$r.identity.primary_identity_match
  if(-not$r.identity_verified){throw ('Retained kernel ownership unverified; termination refused: '+$r.identity.reason)}
  if($P.HasExited){$r.reaped=$true}else{
   if(-not$r.identity.matched){throw ('Retained original process not live: '+$r.identity.reason)}
   if(-not$P.HasExited){$r.termination_attempted=$true;$P.Kill()};$r.reaped=$P.WaitForExit(1500)
  }
 }catch{$r.error=$_.Exception.Message}
 finally{try{if($P.HasExited){$r.reaped=$true;$r.exit_known=$true;$r.native_exit_code=$P.ExitCode}}catch{if(-not$r.error){$r.error=$_.Exception.Message}}}
 return $r
}
function AccountOrReapCli($P,[string]$Expected,$Created,[long]$Retained,[uint32]$ExpectedPid,$StartupCache){
 # This read-only query cannot authorize Reap, Kill, Control, Post or AssertOwned.
 $proof=Get-ViExitedCliAccounting $Retained $ExpectedPid $Created $Expected $StartupCache
 if($proof.native_signaled){
  $identity=ReadRetainedImage $P $Expected $Created $Retained
  $r=@{reaped=$true;identity_verified=[bool]$identity.primary_identity_match;identity=$identity;exit_known=$proof.native_exit_known;native_exit_code=$null;error=$null;deadline_ms=1500;termination_attempted=$false;identity_source='current strict EA65 identity, unchanged; separate read-only exited accounting';exit_accounting=$proof;completion_owned=$false}
 }else{
  # Live action authority is still the unmodified fresh EA65 Reap path.
  $r=Reap $P $Expected $Created $Retained
  $proof=Get-ViExitedCliAccounting $Retained $ExpectedPid $Created $Expected $StartupCache
  $r.exit_accounting=$proof;$r.completion_owned=$false
 }
 if($proof.native_exit_known-and$proof.native_signaled){$r.exit_known=$true;$r.native_exit_code=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0)}
 $r.completion_owned=(Test-ViExitedCliAccounting $proof)-and$r.reaped-and-not$r.error
 if(-not$r.completion_owned-and-not$r.error){$r.error='Owned CLI exit accounting refused: '+$proof.reason}
 return $r
}
function RunCli([string[]]$Arguments,[int]$Budget){
 $s=[Diagnostics.ProcessStartInfo]::new();$s.FileName=$V;$s.UseShellExecute=$false;$s.CreateNoWindow=$true;$s.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$s.RedirectStandardOutput=$true;$s.RedirectStandardError=$true;foreach($arg in $Arguments){[void]$s.ArgumentList.Add([string]$arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$s;$started=$false;$outTail=$null;$errTail=$null;$created=$null;$watch=[Diagnostics.Stopwatch]::StartNew()
 $r=@{arguments=$Arguments;expected_path=$V;pid=$null;creation_utc_ticks=$null;retained_handle=$null;startup_identity=$null;startup_cache=$null;startup_cache_valid=$false;exit=$null;exit_known=$false;timed_out=$false;owned_cli_reaped=$false;stdout='';stderr='';stdout_complete=$false;stderr_complete=$false;stdout_truncated=$false;stderr_truncated=$false;stdout_available=$false;stderr_available=$false;stdout_error=$null;stderr_error=$null;error=$null;deadline_ms=$Budget}
 try{
  if(-not$p.Start()){throw 'CLI start failed'};$started=$true;$r.pid=$p.Id;$r.retained_handle=$p.Handle.ToInt64();$created=$p.StartTime.ToUniversalTime().Ticks;$r.creation_utc_ticks=$created
  try{$r.dotnet_start_path=$p.Path}catch{$r.dotnet_start_path_error=$_.Exception.Message}
  $outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError)
  $r.startup_identity=ReadRetainedImage $p $V $created $r.retained_handle
  # Cache only this earlier strict LIVE original-handle observation before waiting.
  # A fast CLI without that observation is incomplete, even if an exited image works.
  $r.startup_cache=New-ViStrictCliStartupCache $r.startup_identity $r.retained_handle ([uint32]$r.pid) $created $V
  $r.startup_cache_valid=$r.startup_cache-is[ViOwnedCliStartupCacheV1]
  if(-not$r.startup_cache_valid){throw 'Strict original CLI startup cache unavailable'}
  $r.timed_out=-not$p.WaitForExit($Budget)
 }catch{$r.error=$_.Exception.Message}
 finally{
  if($started){$r.cleanup=AccountOrReapCli $p $V $created $r.retained_handle ([uint32]$r.pid) $r.startup_cache;$r.owned_cli_reaped=$r.cleanup.reaped-and$r.cleanup.completion_owned;$r.exit_known=$r.cleanup.exit_known;$r.exit=$r.cleanup.native_exit_code}
  if($outTail){$r.stdout_available=$outTail.Wait(400);$r.stdout=$outTail.Text();$r.stdout_complete=$outTail.Complete;$r.stdout_truncated=$outTail.Characters-gt65536;$r.stdout_error=$outTail.Error}
  if($errTail){$r.stderr_available=$errTail.Wait(400);$r.stderr=$errTail.Text();$r.stderr_complete=$errTail.Complete;$r.stderr_truncated=$errTail.Characters-gt65536;$r.stderr_error=$errTail.Error}
  if($outTail-and-not$outTail.Complete){try{$p.StandardOutput.Close()}catch{}};if($errTail-and-not$errTail.Complete){try{$p.StandardError.Close()}catch{}}
  $watch.Stop();$r.elapsed_ms=$watch.ElapsedMilliseconds;$p.Dispose()
 }
 return $r
}
function Call($T,[string[]]$Arguments,[int]$Budget=9000){
 AssertOwned $T 'before_cli';$beforeIdentity=$T.latest_lifecycle_identity
 $r=RunCli (@('--json','--pid',[string]$T.process.Id)+$Arguments) $Budget
 $r.arguments=$Arguments;$r.fixture_identity_before=$beforeIdentity;$r.fixture_identity_after=$null;$fixtureStillOwned=$false
 try{AssertOwned $T 'after_cli';$r.fixture_identity_after=$T.latest_lifecycle_identity;$fixtureStillOwned=$true}catch{$r.fixture_identity_after=$T.latest_lifecycle_identity;$r.fixture_identity_after_error=$_.Exception.Message}
 $lines=@($r.stdout-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}};$r.reply=$reply
 $script:serial++;$r|ConvertTo-Json -Depth 25|Set-Content "$run\calls\$script:serial.json" -Encoding utf8NoBOM;$T.calls.Add($r)
 Require ($fixtureStillOwned) "$($T.arch) retained target identity after CLI"
 Require (-not$r.error-and-not$r.timed_out-and$r.owned_cli_reaped-and$r.cleanup.completion_owned-and-not$r.cleanup.error-and(Test-ViExitedCliAccounting $r.cleanup.exit_accounting)-and$r.exit_known-and$r.startup_cache_valid-and$r.stdout_complete-and$r.stderr_complete-and-not$r.stdout_truncated-and-not$r.stderr_truncated) "$($T.arch) owned CLI and pipe deadlines"
 Require ($lines.Count-eq1-and$null-ne$reply) "$($T.arch) one JSON reply";return $r
}
function Json($T,$Value){Call $T @('json',($Value|ConvertTo-Json -Compress -Depth 6))}

function Probe($T,[string]$Target,[string]$Name){$before=Snap $T;$message=if($Target-eq'A'){0x8092}else{0x8093};$reply=Control $T $message probe;$after=Await $T snapshot $script:token
 $expected=if($Target-eq'A'){0xA01}else{0xB02};$called=if($Target-eq'A'){$after.a_calls-eq$before.a_calls+1-and$after.b_calls-eq$before.b_calls}else{$after.b_calls-eq$before.b_calls+1-and$after.a_calls-eq$before.a_calls}
 Check ($reply.ok-and$reply.result-eq$expected-and$called-and$after.wrong_class-eq$before.wrong_class-and$after.cycles-eq$before.cycles) "$($T.arch) $Name reaches only its own original procedure without recursion";$T.evidence.Add(@{name=$Name;target=$Target;expected_result=$expected;reply=$reply;before=$before;after=$after});return $reply}
function StopOwned($T){
 if(-not$T-or-not$T.process-or$T.stop){return}
 $T.stop=Reap $T.process $T.path $T.created $T.retained_handle
 Check ($T.stop.reaped-and$T.stop.identity_verified) "$($T.arch) exact retained owned fixture cleanup within deadline"
 if($T.stop.reaped){$T.process.Dispose();$T.process=$null}
}

function CurrentHwnd($R){if(-not$R-or-not$R.reply-or-not$R.reply.hwnd){return [uint64]0};[Convert]::ToUInt64($R.reply.hwnd,16)}
function Snapshot($T){Control $T 0x80A5 release_snapshot}
function RecordError([string]$Name){if(-not$checks.Count-or$checks[$checks.Count-1].pass-or$checks[$checks.Count-1].name-ne$Name){$script:failures++;$checks.Add(@{pass=$false;name=$Name})};Write-Host "FAIL $Name"}
function Cleanup($T){
 if(-not$T-or-not$T.process-or$T.cleanup_done-or$T.process.HasExited){return};$T.cleanup_done=$true;$result=@{name='cleanup';resumed=$false;released=$false}
 try{$r=Control $T 0x80A3 queue_resumed;$result.resume=$r;$drained=Await $T queue_drained $script:token;$result.drained=$drained;$result.resumed=$r.fence_posted-and-not$drained.input_paused}catch{$result.resume_error=$_.Exception.Message}
 try{$r=Call $T @('release');$result.release=$r;$result.released=$r.exit-eq0-and$r.reply.ok}catch{$result.release_error=$_.Exception.Message}
 try{$r=Call $T @('config','isolate=true');$result.config=$r;$result.isolation_restored=$r.exit-eq0-and$r.reply.isolate-eq$true}catch{$result.config_error=$_.Exception.Message}
 # Retained old-window Outer stays intact until this owned process is killed.
 # Removing it here would expose its retained inner hook to unrelated later traffic.
 $T.evidence.Add($result);Check ($result.resumed-and$result.released-and$result.isolation_restored) "$($T.arch) bounded virtual-input cleanup and queue resume"
}
function State($T,[string]$Name){
 $s=Call $T @('status');$g=Call $T @('readiness','--hooks-only','--wait-ms','0')
 Require ($s.exit-eq0-and$s.reply.ok-is[bool]-and$s.reply.ok-and$s.reply.isolate-is[bool]-and$s.reply.fakefocus-is[bool]-and$s.reply.blockfocus-is[bool]-and$s.reply.autoactivate-is[bool]-and$s.reply.physical_input.timed_passthrough-is[bool]-and$s.reply.physical_input.remaining_ms-is[long]-and$s.reply.physical_input.expired_count-is[long]-and$g.reply.ok-is[bool]-and$g.reply.ok-and$g.reply.window_generation-is[long]-and$g.reply.window_generation-gt0) "$($T.arch) $($T.mode) $Name typed status/generation"
 return @{status=$s;generation=$g}
}
function SameConfig($Before,$After){$Before.isolate-eq$After.isolate-and$Before.fakefocus-eq$After.fakefocus-and$Before.blockfocus-eq$After.blockfocus-and$Before.autoactivate-eq$After.autoactivate-and$Before.rawmode-eq$After.rawmode-and$Before.msgmode-eq$After.msgmode-and$Before.physical_input.timed_passthrough-eq$After.physical_input.timed_passthrough-and$Before.physical_input.remaining_ms-eq$After.physical_input.remaining_ms-and$Before.physical_input.expired_count-eq$After.physical_input.expired_count}
function Refusal($T,$Call,[string]$Reason,[string]$Name){
 Check ($Call.reply.ok-is[bool]-and$Call.reply.ok-eq$false-and$Call.reply.adoption_refused-is[bool]-and$Call.reply.adoption_refused-eq$true-and$Call.reply.target_changed-is[bool]-and$Call.reply.target_changed-eq$false-and$Call.reply.adoption_reason-ceq$Reason-and$Call.exit-ne0) "$($T.arch) $($T.mode) $Name"
}
function Resume($T,$Before){$r=Control $T 0x80A3 queue_resumed;$d=Await $T queue_drained $script:token
 Require ($r.fence_posted-is[bool]-and$r.fence_posted-and$d.input_paused-is[bool]-and-not$d.input_paused-and$d.pause_timeouts-eq0-and$d.a_identity_live-is[bool]-and$d.a_identity_live-and$d.b_identity_live-is[bool]-and$d.b_identity_live-and$d.cycles-eq$Before.cycles) "$($T.arch) $($T.mode) actual FIFO drain fence, live window identities, no recursion"
 return @{resume=$r;drained=$d}
}
function ExactUp($T,$Published,$Drained,$Begin){
 $ups=@(Records $T|Where-Object {$_.event-eq'key_up'-and$_.token-eq$Begin.case_token-and$_.target-eq'A'})
 Check ($ups.Count-eq1-and$ups[0].identity_live-is[bool]-and$ups[0].identity_live-and$ups[0].procedure-ceq'A'-and$ups[0].procedure_matches_window-is[bool]-and$ups[0].procedure_matches_window-and$ups[0].message-eq$Published.message-and$ups[0].wparam-eq$Published.wparam-and$ups[0].lparam-eq$Published.lparam-and$Drained.a_ups-eq$Begin.a_ups+1-and$Drained.a_held-is[bool]-and-not$Drained.a_held-and$Drained.wrong_class-eq$Begin.wrong_class) "$($T.arch) $($T.mode) exact accepted up reaches actual A original and clears its latch"
 return $ups
}
try{foreach($arch in @('x64','x86')){
 $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$folder="$run\tools\$arch";New-Item -ItemType Directory -Force "$folder\temp"|Out-Null;$source=(Resolve-Path "$PSScriptRoot\..\test\vi_retargetquiescence.cpp").Path
 $batch=@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" /I"$RepoRoot\third_party\minhook\include" "$source" /Fo:"$folder\fixture.obj" /Fe:"$folder\fixture.exe" /link /SUBSYSTEM:WINDOWS "$RepoRoot\build\$arch\vimh_buffer.obj" "$RepoRoot\build\$arch\vimh_hook.obj" "$RepoRoot\build\$arch\vimh_trampoline.obj" "$RepoRoot\build\$arch\vimh_hde.obj"
exit /b %errorlevel%
"@;[IO.File]::WriteAllText("$folder\build.cmd",$batch,[Text.Encoding]::ASCII)
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";Require ($LASTEXITCODE-eq0) "$arch exact original release observer builds W4 WX"
 $exe=(Resolve-Path "$folder\fixture.exe").Path
 foreach($mode in @('held_outer_refusal','pending_outer_refusal','quiescent_outer_success','known_top_held_success','manual_known_top_refusal')){
  $log="$run\$arch.$mode.fixture.jsonl";$start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$exe;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;[void]$start.ArgumentList.Add($log)
  $p=[Diagnostics.Process]::Start($start)
  $owned=@{arch=$arch;mode=$mode;process=$p;path=$exe;created=$null;retained_handle=$null;stop=$null;log=$log;calls=[Collections.Generic.List[object]]::new();evidence=[Collections.Generic.List[object]]::new();identity_checks=0;identity_observations=[Collections.Generic.List[object]]::new();identity_observations_dropped=0};$cases.Add($owned)
  try{
   $owned.retained_handle=$p.Handle.ToInt64();$owned.created=$p.StartTime.ToUniversalTime().Ticks
   try{$owned.dotnet_start_path=$p.Path}catch{$owned.dotnet_start_path_error=$_.Exception.Message}
   $owned.startup_identity=ObserveStartupImage $p $exe $owned.created $owned.retained_handle
   Require ($owned.startup_identity.matched-and$owned.startup_identity.actual_path-eq$exe) "$arch $mode fixture retained exact kernel identity"
   $end=[Environment]::TickCount64+4000;$ready=$null;$releaseReady=$null
   do{AssertOwned $owned 'ready_poll';$ready=@(Records $owned|Where-Object event -eq ready)|Select-Object -First 1;$releaseReady=@(Records $owned|Where-Object event -eq release_ready)|Select-Object -First 1;if(($ready-and$releaseReady)-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
   Require ($ready-and$ready.pid-eq$p.Id-and$ready.safe_spies-is[bool]-and$ready.safe_spies-and$ready.write_trampolines_called-eq$false-and$ready.outer_recursion_limit-eq2-and$releaseReady.post_observer_verified-eq$true-and$releaseReady.key-eq117-and$releaseReady.pause_limit_ms-eq15000) "$arch $mode terminal spies, bounded outer, exact observer and finite pause"
   $owned.control=[IntPtr]::new([long]$ready.control);$owned.thread=[uint32]$ready.thread;$owned.before=Foreground;Require ($owned.before.pid-ne$p.Id) "$arch $mode passive foreground outside fixture"
   $r=Call $owned @('inject') 30000;Require ($r.exit-eq0-and$r.reply.ok) "$arch $mode injection"
   $shown=Control $owned 0x8091 b_shown;Require $shown.ok "$arch $mode B shown without activation"
   $config=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.a),'isolate=true','fakefocus=true','blockfocus=true','autoactivate=false','msgs=on','raw=off')
   Require ($config.exit-eq0-and$config.reply.ok-and$config.reply.wndproc_binding_ready-and(CurrentHwnd $config)-eq[uint64]$ready.a) "$arch $mode exact fresh A adoption"
   $armed=Call $owned @('readiness','--wait-ms','1000');Require ($armed.exit-eq0-and$armed.reply.ok-and$armed.reply.queue_ready-and(CurrentHwnd $armed)-eq[uint64]$ready.a) "$arch $mode fresh A queue ready before input"
   $begin=Control $owned 0x80A0 case_begin;$caseToken=$script:token
   Require ($begin.a_identity_live-and$begin.b_identity_live-and-not$begin.a_held-and-not$begin.b_held-and-not$begin.input_paused-and$begin.pause_timeouts-eq0) "$arch $mode clean exact application baseline"
   $ev=@{name=$mode;case_token=$caseToken;begin=$begin;measurement_complete=$false};$owned.evidence.Add($ev)
   if($mode-notin@('known_top_held_success','manual_known_top_refusal')){$outer=Control $owned 0x8090 outer_installed;Require ($outer.ok-and$outer.saved_proc-ne0) "$arch $mode actual outer installed";$null=Probe $owned A ($mode+'_original_positive')}
   $before=State $owned before;$ev.before=$before
   if($mode-eq'quiescent_outer_success'){
    $clean=Snapshot $owned;Require (-not$clean.a_held-and-not$clean.async_down-and-not$clean.key_down-and$clean.keyboard_ok-and-not$clean.keyboard_down) "$arch $mode no held input before safe change"
    $change=Json $owned @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.b)};$after=State $owned after;$snap=Snapshot $owned
    Check ($change.exit-eq0-and$change.reply.ok-eq$true-and(CurrentHwnd $after.status)-eq[uint64]$ready.b-and$after.generation.reply.window_generation-gt$before.generation.reply.window_generation) "$arch $mode quiescent opaque-old-chain target change succeeds"
    Check ($snap.a_top_proc-eq$snap.outer_proc-and$snap.a_downs-eq$begin.a_downs-and$snap.a_ups-eq$begin.a_ups-and-not$snap.a_held-and$snap.b_downs-eq$begin.b_downs-and$snap.b_ups-eq$begin.b_ups-and-not$snap.b_held) "$arch $mode outer and clean application latches preserved"
    $null=Probe $owned A ($mode+'_retained_A_original');$ev.change=$change;$ev.after=$after;$ev.drained=$snap
   }else{
    $manual=$mode-eq'manual_known_top_refusal';$job=$null
    if($manual){$manualDown=Call $owned @('down','F6');Require ($manualDown.exit-eq0-and$manualDown.reply.ok) "$arch $mode actual manual F6 down admitted";$ev.manual_down=$manualDown}
    else{$job=Call $owned @('hold','F6','60000','--async');Require ($job.exit-eq0-and$job.reply.ok-and$job.reply.accepted-and$job.reply.job_id) "$arch $mode actual held F6 job admitted";$owned.job_id=[string]$job.reply.job_id}
    $postedDown=Await $owned posted_down $caseToken;$down=Await $owned key_down $caseToken
    Require ($postedDown.accepted-and$postedDown.hwnd-eq$ready.a-and$down.identity_live-and$down.target-ceq'A'-and$down.procedure-ceq'A'-and$down.procedure_matches_window-eq$true-and$down.held-and$down.downs-eq$begin.a_downs+1-and$down.message-eq$postedDown.message-and$down.wparam-eq$postedDown.wparam-and$down.lparam-eq$postedDown.lparam) "$arch $mode accepted down reaches exact A original"
    $running=$null;if(-not$manual){$running=Call $owned @('job-status',$owned.job_id)};$trace=Call $owned @('trace','key','F6')
    Require ($trace.reply.virtual_down-is[bool]-and$trace.reply.virtual_down-and($manual-or($running.reply.job.hold_ready-eq$true-and$running.reply.job.job_state-ceq'running'))) "$arch $mode actual producer/global virtual-down witness"
    $pause=Control $owned 0x80A1 queue_paused;Require ($pause.input_paused-and$pause.a_held-and$pause.async_down-and$pause.key_down-and$pause.keyboard_ok-and$pause.keyboard_down-and$pause.a_ups-eq$begin.a_ups) "$arch $mode actual old-input pause fence"
    $ev.job=$job;$ev.down=$down;$ev.posted_down=$postedDown;$ev.pause=$pause;$ev.running=$running;$ev.down_trace=$trace
    if($mode-in@('held_outer_refusal','manual_known_top_refusal')){
     $same=Json $owned @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.a)};$sameState=State $owned same_target
     Check ($same.reply.ok-eq$true-and(CurrentHwnd $sameState.status)-eq[uint64]$ready.a-and$sameState.generation.reply.window_generation-eq$armed.reply.window_generation) "$arch $mode same-HWND live no-op remains allowed while held"
     $heldBefore=State $owned held_before
     $attempt=Json $owned @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.b);isolate=$false;for_ms=5000;fakefocus=$false;blockfocus=$false;autoactivate=$true}
     Refusal $owned $attempt 'target_change_requires_released_virtual_input' 'unsafe held change refuses before bounded opt-out/config changes'
     $after=State $owned held_after;$still=$null;if(-not$manual){$still=Call $owned @('job-status',$owned.job_id)};$afterTrace=Call $owned @('trace','key','F6');$heldSnap=Snapshot $owned
     Check ((CurrentHwnd $after.status)-eq[uint64]$ready.a-and$after.generation.reply.window_generation-eq$heldBefore.generation.reply.window_generation-and(SameConfig $heldBefore.status.reply $after.status.reply)) "$arch $mode refusal preserves A, generation and every requested configuration setting"
     Check (($manual-or($still.reply.job.job_state-ceq'running'-and$still.reply.job.hold_ready-eq$true))-and$afterTrace.reply.virtual_down-eq$true-and$heldSnap.a_held-and$heldSnap.input_paused-and$heldSnap.a_ups-eq$begin.a_ups) "$arch $mode refusal preserves the held producer and application/global state"
     $ev.same=$same;$ev.attempt=$attempt;$ev.after=$after;$ev.job_after=$still;$ev.trace_after=$afterTrace;$ev.held_snapshot=$heldSnap
     # A baseline unsafe acceptance may already have cancelled the job and moved
     # to B. Collect/resume/clean that ACTUAL state; never readopt opaque old A.
     $cancel=if($manual){Call $owned @('up','F6')}else{Call $owned @('job-cancel',$owned.job_id)}
     Check ($cancel.exit-eq0-and$cancel.reply.ok-eq$true) "$arch $mode explicit producer release/cancel acknowledged before final cleanup"
     $released=Await $owned posted_up $caseToken;$queued=Control $owned 0x80A2 pending_release;$releaseSnap=Await $owned pending_snapshot $script:token
     Require ($released.accepted-is[bool]-and$queued.found-is[bool]-and$queued.exact_accepted_tuple-is[bool]-and$releaseSnap.keyboard_ok-is[bool]) "$arch $mode typed post/queue/release observation before drain"
     Check ($released.accepted-and$released.input_paused-and$released.hwnd-eq$ready.a-and$queued.found-and$queued.exact_accepted_tuple-and$queued.post_sequence-eq$released.post_sequence-and$releaseSnap.a_held-and-not$releaseSnap.async_down-and-not$releaseSnap.key_down-and$releaseSnap.keyboard_ok-and-not$releaseSnap.keyboard_down) "$arch $mode explicit cancellation-up is accepted/queued on A while global state is up"
     $drain=Resume $owned $begin;$ups=ExactUp $owned $released $drain.drained $begin
     Check (-not$drain.drained.async_down-and-not$drain.drained.key_down-and$drain.drained.keyboard_ok-and-not$drain.drained.keyboard_down-and$drain.drained.b_downs-eq$begin.b_downs-and$drain.drained.b_ups-eq$begin.b_ups-and-not$drain.drained.b_held) "$arch $mode explicit cleanup clears global input without redirecting it into B"
     $ev.cancel=$cancel;$ev.cancel_publication=$released;$ev.cancel_pending=$queued;$ev.cancel_snapshot=$releaseSnap;$ev.received_ups=$ups;$ev.drain=$drain
    }else{
     if($mode-eq'pending_outer_refusal'){
      $cancel=Call $owned @('job-cancel',$owned.job_id);Require ($cancel.exit-eq0-and$cancel.reply.ok) "$arch $mode explicit same-A cancel accepted"
     }else{
      $cancel=Json $owned @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.b)}
      Check ($cancel.exit-eq0-and$cancel.reply.ok-eq$true) "$arch $mode known-top held retarget remains accepted"
      if($cancel.reply.ok-ne$true){$ev.unexpected_refusal_fallback_cancel=Call $owned @('job-cancel',$owned.job_id)}
     }
     $published=Await $owned posted_up $caseToken;$pending=Control $owned 0x80A2 pending_release;$pendingSnapshot=Await $owned pending_snapshot $script:token
     Require ($published.accepted-and$published.input_paused-and$published.hwnd-eq$ready.a-and$published.post_sequence-gt$postedDown.post_sequence-and$pending.input_paused-and$pending.found-and$pending.exact_accepted_tuple-and$pending.case_token-eq$caseToken-and$pending.post_sequence-eq$published.post_sequence-and$pending.hwnd-eq$published.hwnd-and$pending.message-eq$published.message-and$pending.wparam-eq$published.wparam-and$pending.lparam-eq$published.lparam) "$arch $mode accepted exact up remains queued via PM_NOREMOVE"
     Require ($pendingSnapshot.a_held-and$pendingSnapshot.a_ups-eq$begin.a_ups-and-not$pendingSnapshot.async_down-and-not$pendingSnapshot.key_down-and$pendingSnapshot.keyboard_ok-and-not$pendingSnapshot.keyboard_down-and$pendingSnapshot.pause_timeouts-eq0) "$arch $mode global-up versus held application latch before drain"
     $pendingBefore=State $owned pending_before
     if($mode-eq'pending_outer_refusal'){
      $attempt=Json $owned @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.b);autoactivate=$true}
      Refusal $owned $attempt 'target_change_requires_drained_legacy_publications' 'pending legacy release blocks opaque-chain target change'
      $pendingAfter=State $owned pending_after
      Check ((CurrentHwnd $pendingAfter.status)-eq[uint64]$ready.a-and$pendingAfter.generation.reply.window_generation-eq$pendingBefore.generation.reply.window_generation-and(SameConfig $pendingBefore.status.reply $pendingAfter.status.reply)) "$arch $mode pending refusal preserves A, generation and configuration"
      $ev.attempt=$attempt;$ev.pending_after=$pendingAfter
     }
     $drain=Resume $owned $begin;$ups=ExactUp $owned $published $drain.drained $begin
     $currentReady=Call $owned @('readiness','--wait-ms','1000');$afterDrain=State $owned after_drain
     Require ($currentReady.reply.ok-is[bool]-and$currentReady.reply.ok-and$currentReady.reply.queue_ready-is[bool]-and$currentReady.reply.queue_ready) "$arch $mode actual current target has fresh queue readiness after drain"
     if($mode-eq'pending_outer_refusal'){
      Check ((CurrentHwnd $currentReady)-eq[uint64]$ready.a-and$currentReady.reply.window_generation-eq$armed.reply.window_generation) "$arch $mode old A obtains full readiness before retry"
      $retry=Json $owned @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.b)};$retried=State $owned retried
      Check ((CurrentHwnd $afterDrain.status)-eq[uint64]$ready.a-and$retry.exit-eq0-and$retry.reply.ok-eq$true-and(CurrentHwnd $retried.status)-eq[uint64]$ready.b-and$retried.generation.reply.window_generation-gt$afterDrain.generation.reply.window_generation) "$arch $mode drained retry performs a successful fresh B change"
      $ev.retry=$retry;$ev.retried=$retried
      Check ($drain.drained.a_top_proc-eq$drain.drained.outer_proc) "$arch $mode old A outer remains intact"
     }else{
      $terminal=Call $owned @('job-status',$owned.job_id)
      Check ((CurrentHwnd $afterDrain.status)-eq[uint64]$ready.b-and$afterDrain.generation.reply.window_generation-gt$armed.reply.window_generation-and$terminal.reply.job.job_complete-eq$true-and$terminal.reply.job.job_state-ceq'cancelled'-and$terminal.reply.job.job_reason-ceq'target_changed'-and$drain.drained.a_top_proc-eq$drain.drained.a_original_proc) "$arch $mode known-top cancellation, exact detach and committed B generation preserved"
      $ev.terminal=$terminal
     }
     Check ($drain.drained.b_downs-eq$begin.b_downs-and$drain.drained.b_ups-eq$begin.b_ups-and-not$drain.drained.b_held-and-not$drain.drained.async_down-and-not$drain.drained.key_down-and$drain.drained.keyboard_ok-and-not$drain.drained.keyboard_down) "$arch $mode B stays clean and global virtual key stays up"
     $ev.cancel=$cancel;$ev.published=$published;$ev.pending=$pending;$ev.pending_snapshot=$pendingSnapshot;$ev.pending_before=$pendingBefore;$ev.drain=$drain;$ev.received_ups=$ups;$ev.current_ready=$currentReady;$ev.after_drain=$afterDrain
    }
   }
   $actual=Foreground;Require ($actual.pid-ne$p.Id) "$arch $mode actual foreground remains outside fixture"
   $ev.actual_foreground=$actual;$ev.measurement_complete=$true;$completedModes.Add(@{arch=$arch;mode=$mode;pid=$p.Id;case_token=$caseToken});$owned.job_id=$null
  }catch{$scopeComplete=$false;RecordError ($arch+' '+$mode+' instrument/prerequisite: '+$_.Exception.Message)}finally{try{Cleanup $owned}finally{StopOwned $owned;$owned=$null}}
 }
};$complete=$scopeComplete-and$completedModes.Count-eq10}catch{$scopeComplete=$false;RecordError $_.Exception.Message}
finally{
 if($owned){try{Cleanup $owned}catch{RecordError $_.Exception.Message}finally{StopOwned $owned}}
 $cleanupComplete=@($cases|Where-Object {-not$_.stop-or$_.stop.reaped-ne$true-or$_.stop.identity_verified-ne$true}).Count-eq0
 $complete=$complete-and$cleanupComplete
 foreach($case in $cases){$case.Remove('process');$case.target_log=Text $case}
 @{schema=1;completed=$complete;expected_mode_count=10;completed_mode_count=$completedModes.Count;completed_modes=$completedModes;measurement_scope_complete=$scopeComplete;cleanup_complete=$cleanupComplete;status=if($failures){'failed'}else{'passed'};failed_check_count=$failures;positive_case_count=@($checks|Where-Object pass -eq $true).Count;checks=$checks;cases=$cases;scope='fresh owned fixture per mode/arch; refuse opaque-chain held/pending or unowned held retarget; preserve known-top job-owned held cancellation and quiescent target change; no old-window permit or actual game';compiler_deadline='external suite limit';failure_is_expected_inversion=$false;real_game_cause_measured=$false;ownership_authority='live: fresh EA65 retained kernel identity; exited CLI completion: immutable8F startup cache and native-signaled object proof only';stream_tail_limit_characters=65536;tool_issues=@()}|ConvertTo-Json -Depth 45|Set-Content "$run\result.json" -Encoding utf8NoBOM
}
Write-Host "$(@($checks|Where-Object pass -eq $true).Count) PASS / $failures FAIL ($run)";if($failures-or-not$complete){exit 1};exit 0
