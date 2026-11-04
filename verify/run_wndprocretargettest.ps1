# Distinct originals and a retained outer hook; no input/capture calls.
# Lower write/foreground spies are terminal. Outer recursion is bounded.
param([string]$RepoRoot='', [string]$CliDir='', [string]$Out="$PSScriptRoot\..\build\test\wndproc_retarget", [switch]$KeepGoing,[switch]$BaseRetargetOnly)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
if(-not$RepoRoot){$d=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($i=0;$i-lt7-and$d;$i++){if(Test-Path (Join-Path $d.FullName 'build\x64\vimh_buffer.obj')){$RepoRoot=$d.FullName;break};$d=$d.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot for tested MinHook objects'};$RepoRoot=(Resolve-Path $RepoRoot).Path;if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$V=(Resolve-Path (Join-Path $CliDir 'vinput.exe')).Path
$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $run,"$run\calls","$run\tools"|Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$owned=$null;$serial=0;$token=0;$complete=$false;$failures=0
function Check($Good,[string]$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good){$script:failures++;if(-not$KeepGoing){throw $Name}}}
function Require($Good,[string]$Name){Check $Good $Name;if(-not$Good){throw $Name}}
if(-not('ViWndProcNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViWndProcNative{[DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();}'}
function Text($T){if(Test-Path $T.log){Get-Content -LiteralPath $T.log -Raw}else{''}}
function Records($T){foreach($line in ((Text $T)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Await($T,[string]$Event,[long]$Token,[int]$Budget=4000){$end=[Environment]::TickCount64+$Budget;do{foreach($r in @(Records $T)){if($r.event-eq$Event-and$r.token-eq$Token){return $r}};if($T.process.HasExited){throw 'Owned fixture exited'};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end);throw "Missing $Event token$Token acknowledgement"}
function AssertOwned($T){
 if(-not$T-or-not$T.process-or$T.process.HasExited){throw 'Owned fixture exited'}
 $fresh=Get-Process -Id $T.process.Id -ErrorAction Stop
 try{if($fresh.HasExited-or$fresh.Path-ne$T.path-or$fresh.StartTime.ToUniversalTime().Ticks-ne$T.created){throw 'Owned process creation/path identity changed'}}finally{$fresh.Dispose()}
 if($T.process.HasExited){throw 'Owned fixture exited during identity check'}
}
function Control($T,[uint32]$Message,[string]$Event){AssertOwned $T;[uint32]$owner=0;$thread=[ViWndProcNative]::GetWindowThreadProcessId($T.control,[ref]$owner);if(-not$thread-or$owner-ne$T.process.Id-or$thread-ne$T.thread){throw 'Owned control HWND/PID/thread changed'};AssertOwned $T;$script:token++;if(-not[ViWndProcNative]::PostMessageW($T.control,$Message,[UIntPtr]::new([uint64]$script:token),[IntPtr]::Zero)){throw 'Owned control post failed'};return Await $T $Event $script:token}
function Snap($T){Control $T 0x8094 snapshot}
function Foreground{[uint32]$owner=0;$h=[ViWndProcNative]::GetForegroundWindow();[void][ViWndProcNative]::GetWindowThreadProcessId($h,[ref]$owner);return @{hwnd=$h.ToInt64();pid=$owner;read_only=$true}}
function Call($T,[string[]]$Arguments,[int]$Budget=9000){AssertOwned $T;$s=[Diagnostics.ProcessStartInfo]::new();$s.FileName=$V;$s.UseShellExecute=$false;$s.CreateNoWindow=$true;$s.RedirectStandardOutput=$true;$s.RedirectStandardError=$true;foreach($arg in @('--json','--pid',[string]$T.process.Id)+$Arguments){[void]$s.ArgumentList.Add([string]$arg)};$p=[Diagnostics.Process]::new();$p.StartInfo=$s
 $started=$false;$exited=$false;$out=$null;$err=$null;$timedOut=$false;$reaped=$false;$outReady=$false;$errReady=$false;$raw='';$errorText=''
 try{
  if(-not$p.Start()){throw 'CLI start failed'};$started=$true;$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync()
  $exited=$p.WaitForExit($Budget);$timedOut=-not$exited
  if(-not$exited){try{$p.Kill()}catch{};$exited=$p.WaitForExit(1500)};$reaped=$exited
  $outReady=$out.Wait(750);$errReady=$err.Wait(750);if($outReady){$raw=$out.Result};if($errReady){$errorText=$err.Result}
  $lines=@($raw-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
  $script:serial++;$r=@{arguments=$Arguments;exit=if($exited){$p.ExitCode}else{$null};reply=$reply;stdout=$raw;stderr=$errorText;deadline_ms=$Budget;timed_out=$timedOut;owned_cli_reaped=$reaped;stdout_complete=$outReady;stderr_complete=$errReady}
  $r|ConvertTo-Json -Depth 25|Set-Content "$run\calls\$script:serial.json" -Encoding utf8NoBOM;$T.calls.Add($r)
  Require (-not$timedOut-and$reaped-and$outReady-and$errReady) "$($T.arch) owned CLI and pipe deadlines"
  Require ($lines.Count-eq1-and$null-ne$reply) "$($T.arch) one JSON reply";return $r
 }finally{if($started-and-not$exited){try{if(-not$p.HasExited){$p.Kill()};[void]$p.WaitForExit(1500)}catch{}};$p.Dispose()}}
function Json($T,$Value){Call $T @('json',($Value|ConvertTo-Json -Compress -Depth 6))}

function Probe($T,[string]$Target,[string]$Name){$before=Snap $T;$message=if($Target-eq'A'){0x8092}else{0x8093};$reply=Control $T $message probe;$after=Await $T snapshot $script:token
 $expected=if($Target-eq'A'){0xA01}else{0xB02};$called=if($Target-eq'A'){$after.a_calls-eq$before.a_calls+1-and$after.b_calls-eq$before.b_calls}else{$after.b_calls-eq$before.b_calls+1-and$after.a_calls-eq$before.a_calls}
 Check ($reply.ok-and$reply.result-eq$expected-and$called-and$after.wrong_class-eq$before.wrong_class-and$after.cycles-eq$before.cycles) "$($T.arch) $Name reaches only its own original procedure without recursion";$T.evidence.Add(@{name=$Name;target=$Target;expected_result=$expected;reply=$reply;before=$before;after=$after});return $reply}
function StopOwned($T){if(-not$T-or-not$T.process){return};if($T.process.HasExited){$T.stop=@{already_exited=$true;reaped=$true};return};try{AssertOwned $T;$T.process.Kill();$T.stop=@{owned_identity_verified=$true;reaped=$T.process.WaitForExit(1500)}}catch{$T.stop=@{reaped=$false;error=$_.Exception.Message}};if(-not$T.stop.reaped){$script:failures++;$checks.Add(@{pass=$false;name="$($T.arch) owned fixture cleanup did not reap within deadline";detail=$T.stop});Write-Host "FAIL $($T.arch) owned fixture cleanup did not reap within deadline"}}
function CleanupLate($T){
 if(-not$T-or-not$T.late_attempted-or$T.late_cleanup_done-or$T.process.HasExited){return}
 $T.late_cleanup_done=$true;$cleanup=@{name='late_control_cleanup';isolation_restored=$false;outer_removed=$false}
 try{$r=Call $T @('config','isolate=true');$cleanup.config=$r;$cleanup.isolation_restored=$r.exit-eq0-and$r.reply.ok-and$r.reply.isolate-eq$true}catch{$cleanup.config_error=$_.Exception.Message}
 try{$r=Control $T 0x8097 late_cleanup;$cleanup.outer=$r;$cleanup.outer_removed=$r.ok-and$r.remaining_test_outer-eq$false}catch{$cleanup.outer_error=$_.Exception.Message}
 $T.evidence.Add($cleanup)
 Check ($cleanup.isolation_restored-and$cleanup.outer_removed) "$($T.arch) late-control cleanup restores isolation and removes only its own outer"
}
try{foreach($arch in @('x64','x86')){
 $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$folder="$run\tools\$arch";New-Item -ItemType Directory -Force "$folder\temp"|Out-Null;$source=(Resolve-Path "$PSScriptRoot\..\test\vi_wndprocretarget.cpp").Path
 $batch=@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" /I"$RepoRoot\third_party\minhook\include" "$source" /Fo:"$folder\fixture.obj" /Fe:"$folder\fixture.exe" /link /SUBSYSTEM:WINDOWS "$RepoRoot\build\$arch\vimh_buffer.obj" "$RepoRoot\build\$arch\vimh_hook.obj" "$RepoRoot\build\$arch\vimh_trampoline.obj" "$RepoRoot\build\$arch\vimh_hde.obj"
exit /b %errorlevel%
"@;[IO.File]::WriteAllText("$folder\build.cmd",$batch,[Text.Encoding]::ASCII)
 # Compiler invocation is covered by the external suite limit, not a per-process hard deadline.
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";Require ($LASTEXITCODE-eq0) "$arch safe distinct-class fixture builds W4 WX"
 $exe=(Resolve-Path "$folder\fixture.exe").Path;$log="$run\$arch.fixture.jsonl";$p=Start-Process $exe -ArgumentList ('"'+$log+'"') -WindowStyle Hidden -PassThru;$owned=@{arch=$arch;process=$p;path=$exe;created=$p.StartTime.ToUniversalTime().Ticks;log=$log;calls=[Collections.Generic.List[object]]::new();evidence=[Collections.Generic.List[object]]::new()};$cases.Add($owned)
 try{$end=[Environment]::TickCount64+4000;$ready=$null;do{$ready=@(Records $owned|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
  Require ($ready-and$ready.pid-eq$p.Id-and$ready.safe_spies-and$ready.write_trampolines_called-eq$false-and$ready.outer_recursion_limit-eq2-and$ready.late_read_spy_verified-and$ready.late_switch_query_ordinal-eq2) "$arch terminal write spies and bounded recursion verified before injection"
  $owned.control=[IntPtr]::new([long]$ready.control);$owned.thread=[uint32]$ready.thread;$owned.before=Foreground;Require ($owned.before.pid-ne$p.Id) "$arch actual foreground outside fixture"
  $r=Call $owned @('inject') 30000;Require ($r.exit-eq0-and$r.reply.ok) "$arch injection";$r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.a),'blockfocus=true','msgs=on','raw=off');Require ($r.reply.ok) "$arch exact A adoption"
  $null=Probe $owned A initial_A;$outer=Control $owned 0x8090 outer_installed;Require ($outer.ok-and$outer.saved_proc-ne0) "$arch host-style outer hook retains the actual installed procedure";$null=Probe $owned A outer_A_before_retarget
  $shown=Control $owned 0x8091 b_shown;Require $shown.ok "$arch B shown without activation";$r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.b));Require ($r.reply.ok) "$arch exact B adoption";$null=Probe $owned B current_B;$null=Probe $owned A retained_old_A
  $beforeRefusal=Call $owned @('status');$r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.a),'fakefocus=false');Check ($r.exit-ne0-and$r.reply.ok-eq$false-and$r.reply.adoption_refused-eq$true-and$r.reply.adoption_reason-eq'retained_opaque_outer_chain'-and$r.reply.target_changed-eq$false-and[Convert]::ToUInt64($r.reply.hwnd,16)-eq[uint64]$ready.b) "$arch opaque retained A readoption explicitly refused without target change"
  $afterRefusal=Call $owned @('status');Check ($afterRefusal.reply.hwnd-eq$beforeRefusal.reply.hwnd-and$afterRefusal.reply.fakefocus-eq$beforeRefusal.reply.fakefocus) "$arch refusal preserves current target and requested config value";$guardBefore=Snap $owned;$guarded=Probe $owned A refused_old_A;$guardAfter=Snap $owned
  $boundedRows=@(Records $owned|Where-Object {$_.event-eq'bounded_cycle'-and$_.token-eq$guarded.token})
  $cycleDelta=[long]$guardAfter.cycles-[long]$guardBefore.cycles
  Check ($cycleDelta-eq0-or($guarded.result-eq0xC03-and$boundedRows.Count-ge1-and@($boundedRows|Where-Object {$_.depth-ne2}).Count-eq0)) "$arch any observed readoption cycle is positively bounded by the fixture guard"
  $owned.evidence.Add(@{name='guarded_readoption_cycle_witness';probe=$guarded;before=$guardBefore;after=$guardAfter;bounded_rows=$boundedRows;cycle_observed=($cycleDelta-gt0);outer_guard_limit=2;no_inner_hook_exposure=$true})
  $null=Probe $owned B current_B_after_refusal
  $bindingContract=$beforeRefusal.reply.PSObject.Properties['wndproc_binding_ready']-and$beforeRefusal.reply.wndproc_binding_ready-eq$true
  $owned.runner_mode=if($BaseRetargetOnly){'base_retarget_only'}else{'full'}
  if($BaseRetargetOnly){
   $owned.unmeasured_controls=@{recovery='not measured: base mode stops before exposing/re-adopting retained inner hook on an old global-original product';late_transaction='not measured: old product has no two-query binding transaction prerequisite';numeric_hwnd_reuse='not measured'}
   $owned.evidence.Add(@{name='base_only_stop';requested_mode='base_retarget_only';binding_contract_observed=[bool]$bindingContract;unmeasured=$owned.unmeasured_controls;safe_stop_before_inner_exposure=$true})
  }elseif(-not$bindingContract){
   Check $false "$arch full recovery/late controls require a ready per-window binding contract; unsafe operations not attempted"
   $owned.unmeasured_controls=@{recovery='not measured: per-window binding contract unavailable';late_transaction='not measured: binding/two-query prerequisite unavailable';numeric_hwnd_reuse='not measured'}
  }else{
  $r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.b));Require ($r.reply.ok-and$r.reply.wndproc_binding_ready) "$arch current known-top B adoption safely reuses its saved original";$null=Probe $owned B reused_current_B
  $removed=Control $owned 0x8095 outer_removed;Require $removed.ok "$arch owned outer removed in reverse order to expose retained inner hook";$r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.a));Require ($r.reply.ok-and$r.reply.wndproc_binding_ready) "$arch retained topmost inner hook readopted with saved original";$null=Probe $owned A reused_topmost_A;$null=Probe $owned B safely_restored_B
  $r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.b));Require ($r.reply.ok-and$r.reply.wndproc_binding_ready) "$arch safely restored original B reattached";$null=Probe $owned B reattached_B;$null=Probe $owned A safely_restored_A
  $beforeLate=Call $owned @('status');$readyBefore=Call $owned @('readiness','--wait-ms','1000')
  Require ($beforeLate.reply.ok-and$beforeLate.reply.isolate-eq$true-and$beforeLate.reply.fakefocus-eq$true-and$beforeLate.reply.physical_input.timed_passthrough-eq$false-and$beforeLate.reply.physical_input.remaining_ms-eq0) "$arch late control starts isolated without a passthrough timer"
  Require ($readyBefore.exit-eq0-and$readyBefore.reply.ok-and$readyBefore.reply.window_ready-and$readyBefore.reply.queue_ready-and[Convert]::ToUInt64($readyBefore.reply.hwnd,16)-eq[uint64]$ready.b) "$arch late control starts with current B readiness"
  $owned.late_attempted=$true;$arm=Control $owned 0x8096 late_armed;$armToken=$script:token
  Require ($arm.ok-and$arm.read_entry_verified-and$arm.switch_query_ordinal-eq2-and$arm.current_proc-eq$arm.expected_proc-and$arm.lifetime_cookie-eq$ready.lifetime_cookie) "$arch owned A original armed for exactly the second procedure query"
  $late=Call $owned @('config','isolate=false','--for','30000','fakefocus=false',('hwnd=0x{0:X}'-f[uint64]$ready.a))
  $switched=Await $owned late_switch $armToken
  $owned.evidence.Add(@{name='late_switch_transaction';arm=$arm;actual_switch=$switched;request=$late;before_status=$beforeLate;before_readiness=$readyBefore})
  Require ($switched.ok-and$switched.ordinal-eq2-and$switched.identity_live-and$switched.setter_preproduct_unchanged-and$switched.hwnd-eq$ready.a-and$switched.pid-eq$p.Id-and$switched.thread-eq$ready.thread-and$switched.lifetime_cookie-eq$arm.lifetime_cookie-and$switched.old_proc-eq$arm.expected_proc-and$switched.saved_proc-eq$arm.expected_proc-and$switched.prior_proc-eq$arm.expected_proc-and$switched.new_proc-eq$arm.outer_proc) "$arch actual late switch has exact query, procedure and lifetime witnesses"
  $afterLate=Call $owned @('status');$readyAfter=Call $owned @('readiness','--wait-ms','1000')
  $owned.evidence.Add(@{name='late_switch_after';status=$afterLate;readiness=$readyAfter})
  Check ($late.exit-ne0-and$late.reply.ok-eq$false-and$late.reply.adoption_refused-eq$true-and$late.reply.target_changed-eq$false-and[Convert]::ToUInt64($late.reply.hwnd,16)-eq[uint64]$ready.b) "$arch witnessed late-chain adoption refuses and keeps B"
  Check ($afterLate.reply.hwnd-eq$beforeLate.reply.hwnd-and$afterLate.reply.fakefocus-eq$beforeLate.reply.fakefocus-and$afterLate.reply.isolate-eq$beforeLate.reply.isolate-and$afterLate.reply.physical_input.timed_passthrough-eq$beforeLate.reply.physical_input.timed_passthrough-and$afterLate.reply.physical_input.remaining_ms-eq0-and$afterLate.reply.physical_input.expired_count-eq$beforeLate.reply.physical_input.expired_count) "$arch late refusal preserves fakefocus isolation and timed passthrough"
  Check ($readyAfter.exit-eq0-and$readyAfter.reply.ok-and$readyAfter.reply.window_ready-and$readyAfter.reply.queue_ready-and$readyAfter.reply.hwnd-eq$readyBefore.reply.hwnd-and$readyAfter.reply.window_generation-eq$readyBefore.reply.window_generation) "$arch late refusal preserves current HWND and readiness generation"
  CleanupLate $owned;$null=Probe $owned B late_refusal_current_B;$null=Probe $owned A late_outer_removed_original_A
  }
  $owned.after=Foreground;Check ($owned.after.pid-ne$p.Id) "$arch actual foreground remains outside fixture";$owned.evidence.Add(@{name='safety';before=$owned.before;after=$owned.after;no_input_commands=$true;outer_guard_prevents_stack_overflow=$true;real_game_cause_measured=$false})
 }finally{try{CleanupLate $owned}finally{StopOwned $owned;$owned=$null}}
};$complete=$true}catch{$message=$_.Exception.Message;if(-not$checks.Count-or$checks[$checks.Count-1].pass-or$checks[$checks.Count-1].name-ne$message){$failures++;$checks.Add(@{pass=$false;name=$message})};Write-Host $message}
finally{if($owned){try{CleanupLate $owned}finally{StopOwned $owned}};foreach($case in $cases){$case.Remove('process');$case.target_log=Text $case};@{schema=2;completed=$complete;status=if($failures){'failed'}else{'passed'};failed_check_count=$failures;positive_case_count=@($checks|Where-Object pass -eq $true).Count;checks=$checks;cases=$cases;runner_mode=if($BaseRetargetOnly){'base_retarget_only'}else{'full'};requested_modes=@{base_retarget_only=[bool]$BaseRetargetOnly;keep_going=[bool]$KeepGoing};advanced_measurements_requested=(-not[bool]$BaseRetargetOnly);scope=if($BaseRetargetOnly){'actual distinct Unicode A/ANSI B originals, retained old-window forwarding and guarded opaque readoption; recovery/late transaction explicitly not measured; stopped before inner exposure'}else{'actual distinct Unicode A/ANSI B originals, retained outer hook and witnessed second-query late chain when binding prerequisite is present'};numeric_hwnd_reuse_measured=$false;compiler_deadline='external suite limit';real_game_cause_measured=$false;tool_issues=@()}|ConvertTo-Json -Depth 30|Set-Content "$run\result.json" -Encoding utf8NoBOM}
Write-Host "$($checks.Count-$failures) PASS / $failures FAIL ($run)";if($failures-or-not$complete){exit 1};exit 0
