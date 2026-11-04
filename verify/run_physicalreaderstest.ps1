# Fabricated native reader values only. Terminal lower spies absorb all cursor/input
# and foreground writes before product injection. No actual input or activation.
param([string]$Out="$PSScriptRoot\..\build\test\physical_readers",[string]$Vinput='',[switch]$KeepGoing,[switch]$SkipCompatibility)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path
$V=if($Vinput){(Resolve-Path $Vinput).Path}else{(Resolve-Path "$root\dist\vinput.exe").Path}
$Out=[IO.Path]::GetFullPath($Out)
if(-not$Out.StartsWith("$root\",[StringComparison]::OrdinalIgnoreCase)){throw 'Reader output must stay inside this checkout'}
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $run|Out-Null
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=$null;$token=0;$call=0;$errorDetail=$null;$complete=$false
function Check($Good,$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good-and-not$KeepGoing){throw $Name}}
if(-not('ViPhysicalReadersNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViPhysicalReadersNative{
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
}
'@}
function Rows($Target){if(-not(Test-Path -LiteralPath $Target.log)){return @()};@(Get-Content -LiteralPath $Target.log|ForEach-Object{try{$_|ConvertFrom-Json -ErrorAction Stop}catch{}})}
function Capture-Exit($Target,[string]$Phase){
 if(-not$Target.process-or-not$Target.process.HasExited){return $null}
 $code=[int32]$Target.process.ExitCode;$hex=[BitConverter]::ToUInt32([BitConverter]::GetBytes($code),0).ToString('X8')
 if(-not$Target.exit_captured){$record=@{kind='fixture_exit';arch=$Target.arch;pid=$Target.pid;phase=$Phase;nativeExitCode=$code;nativeExitCodeHex=$hex;last_records=@(Rows $Target|Select-Object -Last 3)};$evidence.Add($record);$Target.exit_captured=$true}
 return @{code=$code;hex=$hex}
}
function Await($Target,[string]$Kind,[uint64]$Token,[int]$Budget=4000){$until=[Environment]::TickCount64+$Budget;do{$row=@(Rows $Target|Where-Object {$_.kind-eq$Kind-and[uint64]$_.token-eq$Token}|Select-Object -Last 1);if($row.Count){return $row[0]};$exit=Capture-Exit $Target "await $Kind token$Token";if($exit){throw "Owned fixture exited before $Kind token$Token; nativeExitCode=$($exit.code) (0x$($exit.hex))"};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "Fresh $Kind token$Token receipt absent"}
function Post($Target,[uint32]$Message,[uint64]$W=0,[int64]$L=0){
 $pidNow=[uint32]0;$h=[IntPtr]::new([int64]$Target.hwnd);[void][ViPhysicalReadersNative]::GetWindowThreadProcessId($h,[ref]$pidNow)
 if($pidNow-ne$Target.pid-or-not[ViPhysicalReadersNative]::IsWindow($h)-or$Target.process.HasExited){throw 'Owned fixture HWND/PID changed'}
 if(-not[ViPhysicalReadersNative]::PostMessageW($h,$Message,[UIntPtr]::new($W),[IntPtr]::new($L))){throw "Owned fixture PostMessage rejected: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
}
function Model($Target,[uint64]$Mode){$script:token++;Post $Target (0x8000+0x60) $Mode $script:token;Await $Target model $script:token|Out-Null}
function Native($Target,[bool]$Enabled){$script:token++;Post $Target (0x8000+0x62) ([uint64][int]$Enabled) $script:token;Await $Target native $script:token|Out-Null}
function Sample($Target){$script:token++;Post $Target (0x8000+0x61) $script:token;$s=Await $Target sample $script:token;Check ($s.pid-eq$Target.pid-and$s.hwnd-eq$Target.hwnd) "$($Target.arch) fresh sample identity";$actual=[uint32]0;[void][ViPhysicalReadersNative]::GetWindowThreadProcessId([ViPhysicalReadersNative]::GetForegroundWindow(),[ref]$actual);Check ($actual-ne$Target.pid) "$($Target.arch) actual desktop foreground remains outside fixture";$evidence.Add($s);return $s}
function Vi($Target,[string[]]$Arguments,[int]$Budget=8000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$V;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in @('--json','--pid',[string]$Target.pid)+$Arguments){$info.ArgumentList.Add([string]$arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info
 try{if(-not$p.Start()){throw 'Owned CLI did not start'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$timedOut=-not$p.WaitForExit($Budget);if($timedOut-and-not$p.HasExited){$p.Kill();[void]$p.WaitForExit(2000)}
  $text=if($stdout.Wait(1000)){$stdout.Result}else{'<stdout unavailable after owned deadline>'};$errorText=if($stderr.Wait(1000)){$stderr.Result}else{'<stderr unavailable after owned deadline>'};$lines=@($text-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
  $record=@{arguments=$Arguments;exit=$(if($p.HasExited){$p.ExitCode}else{-1});timeout=$timedOut;stdout=$text;stderr=$errorText;reply=$reply};$script:call++;$record|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $run "call_$script:call.json") -Encoding utf8NoBOM
  Check (-not$timedOut-and$lines.Count-eq1-and$null-ne$reply) "$($Target.arch) bounded single JSON $($Arguments[0]) reply";return $record
 }finally{$p.Dispose()}
}
function Producer($Target,[string[]]$Arguments){$r=Vi $Target $Arguments;Check ($r.exit-eq0-and$r.reply.ok) "$($Target.arch) producer $($Arguments-join' ') succeeds before reader observation";return $r}
function Memory-Probe($Target,[uint64]$Mode){
 $script:token++;Post $Target (0x8000+0x66) $Mode $script:token;$probe=Await $Target memory_probe $script:token
 Check ($probe.results.Count-eq9) "$($Target.arch) mode$Mode has all nine fresh protected-memory reader results"
 foreach($row in $probe.results){$failed=if($row.api-eq'keyboard_state'){$row.result-eq0}elseif($row.api-like'raw_*'){$row.result-eq4294967295}else{$row.result-lt0}
  Check ($row.setup_ok-and$failed-and$row.memory_unchanged-and$row.other_buffer_unchanged-and$row.protection_unchanged-and$row.page_restored-and$row.released) "$($Target.arch) mode$Mode $($row.api) rejects protected caller memory without writes or protection changes"
 }
 $evidence.Add($probe)
}
function Stable-Peek($Device,$Name){
 $first=$Device.peek.events|ConvertTo-Json -Depth 6 -Compress;$second=$Device.peek_again.events|ConvertTo-Json -Depth 6 -Compress;$consumed=$Device.consume.events|ConvertTo-Json -Depth 6 -Compress
 Check ($Device.peek.hr-ge0-and$Device.peek_again.hr-ge0-and$Device.consume.hr-ge0-and$first-ceq$second-and$first-ceq$consumed) "$Name two PEEKs and consume preserve full event metadata"
}
function Masked($S,$Name){
 Check ($S.keys.async_b-eq0-and$S.keys.async_caps-eq0-and$S.keys.async_right-eq0-and$S.keys.state_b-eq0-and$S.keys.state_caps-eq1-and$S.keys.state_right-eq0-and$S.keys.keyboard_b-eq0-and$S.keys.keyboard_caps-eq1-and$S.keys.keyboard_right-eq0) "$Name native key/button highs and ordinary low bits masked; Caps toggle retained"
 Check ($S.raw.result-eq4294967295-and-not$S.raw.physical_bytes-and-not$S.raw.buffer_physical_bytes-and$S.raw.buffer_count-eq0) "$Name raw denied and no hidden native buffer payload"
 Check ($S.di_keyboard.b-eq0-and$S.di_mouse.dx-eq0-and$S.di_mouse.dy-eq0-and$S.di_mouse.wheel-eq0-and$S.di_mouse.right-eq0) "$Name native DirectInput state masked"
 foreach($device in @($S.di_keyboard,$S.di_mouse)){Check ($device.peek.count-eq0-and-not$device.peek.physical_bytes-and$device.consume.count-eq0-and-not$device.consume.physical_bytes-and$device.query_count-eq0) "$Name native DirectInput PEEK/consume/query masked"}
}
function NativeSeen($S,$Name){
 Check (($S.keys.async_b-band0x8000)-ne0-and($S.keys.async_right-band0x8000)-ne0-and($S.keys.state_b-band0x8000)-ne0-and($S.keys.state_right-band0x8000)-ne0-and$S.keys.state_caps-eq1-and$S.keys.keyboard_b-eq128-and$S.keys.keyboard_right-eq128) "$Name fabricated native key/mouse-button positive"
 Check ($S.raw.physical_bytes-and$S.raw.buffer_physical_bytes-and$S.raw.buffer_count-eq1) "$Name fabricated native raw positive"
 Check ($S.di_keyboard.b-eq128-and$S.di_mouse.dx-eq717-and$S.di_mouse.dy-eq-313-and$S.di_mouse.wheel-eq120-and$S.di_mouse.right-eq128-and$S.di_mouse.peek.physical_bytes) "$Name fabricated native DirectInput positive"
}
function InvalidArgs($S,$Name){
 Check (-not$S.keys.null_ok-and$S.raw.null_size_result-eq4294967295-and$S.raw.bad_header_result-eq4294967295-and$S.raw.buffer_null_size_result-eq4294967295-and$S.raw.buffer_bad_header_result-eq4294967295) "$Name native null/header errors remain errors"
 foreach($device in @($S.di_keyboard,$S.di_mouse)){Check ($device.null_count_hr-lt0-and$device.bad_size_hr-lt0-and$device.bad_flags_hr-lt0-and$device.null_state_hr-lt0-and$device.bad_state_size_hr-lt0) "$Name DirectInput invalid arguments remain errors"}
}
try{
 # A new CLI must not let an old DLL silently ignore for_ms and disable isolation
 # indefinitely. This is an owned responder, not an actual host or game pipe.
 if(-not$SkipCompatibility){
 $peerLog=Join-Path $run 'old_peer_requests.jsonl';$peerReady=Join-Path $run 'old_peer_ready.txt'
 $shell=(Get-Process -Id $PID).Path
 $owned=Start-Process -FilePath $shell -ArgumentList @('-NoProfile','-NonInteractive','-File',('"'+$PSScriptRoot+'\vi_isolation_peer.ps1"'),'-Log',('"'+$peerLog+'"'),'-Ready',('"'+$peerReady+'"')) -WindowStyle Hidden -PassThru
 try{
  $until=[Environment]::TickCount64+4000;while(-not(Test-Path -LiteralPath $peerReady)-and[Environment]::TickCount64-lt$until){Start-Sleep -Milliseconds 10}
  if(-not(Test-Path -LiteralPath $peerReady)-or[IO.File]::ReadAllText($peerReady)-ne[string]$owned.Id){throw 'Owned protocol peer did not become ready'}
  $target=@{arch='mixed-version';pid=$owned.Id;process=$owned}
  foreach($request in @(@('config','isolate=false','--for','1000'),@('json','{"cmd":"config","isolate":false,"for\u005fms":1000}'))){$r=Vi $target $request;Check ($r.exit-ne0-and-not$r.reply.ok-and$r.reply.configuration_changed-eq$false) 'Old responder cannot silently turn a timed opt-out into indefinite passthrough'}
  $requests=@(Get-Content -LiteralPath $peerLog|ForEach-Object{$_|ConvertFrom-Json});Check ($requests.Count-ge2-and@($requests|Where-Object cmd -ne ping).Count-eq0) 'Only harmless support probes reached old responder; original configuration was never sent'
 }finally{if($owned-and-not$owned.HasExited){$owned.Kill();$owned.WaitForExit()};if($owned){$owned.Dispose();$owned=$null}}
 }
 foreach($arch in @('x64','x86')){
  $target=$null
  $tools=Join-Path $run $arch;New-Item -ItemType Directory -Force $tools,(Join-Path $tools 'temp')|Out-Null
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($arch-eq'x64'){'hde64'}else{'hde32'};$mh=Join-Path $root "build\$arch\host_mh";$batch=Join-Path $tools 'build.cmd'
  @"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cd /d "$tools"
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /D_CRT_SECURE_NO_WARNINGS /I"$root\third_party\minhook\include" "$root\test\vi_physical_readers.cpp" /Fe:reader.exe /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj" || exit /b 2
"@|Set-Content -LiteralPath $batch -Encoding ascii
  $build=&$env:ComSpec /d /c $batch 2>&1;$buildCode=$LASTEXITCODE;$build|Set-Content -LiteralPath (Join-Path $tools 'build.log')
  Check ($buildCode-eq0-and($build-join"`n")-notmatch'(?im)\bwarning (?:C|LNK)\d+') "$arch fabricated reader fixture /W4 /WX build"
  if($buildCode-ne0){throw "$arch fixture build failed"}
  $log=Join-Path $run "$arch.jsonl";$owned=Start-Process -FilePath (Join-Path $tools 'reader.exe') -ArgumentList ('"'+$log+'"') -WindowStyle Hidden -PassThru
  try{
   $until=[Environment]::TickCount64+5000;$ready=$null;do{$ready=@(Rows @{log=$log}|Where-Object kind -eq ready|Select-Object -Last 1);if($ready.Count){$ready=$ready[0];break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
   if(-not$ready-or-not$ready.hwnd){throw 'Reader fixture not ready'}
   $target=@{arch=$arch;pid=$owned.Id;process=$owned;hwnd=$ready.hwnd;log=$log}
   Check ($ready.pid-eq$owned.Id-and$ready.write_spies_terminal-and$ready.foreground_spies_terminal-and-not$ready.raw_registered_by_fixture) "$arch terminal cursor/foreground guards installed before product"
   NativeSeen (Sample $target) "$arch pre-injection native source"
   $r=Vi $target @('inject','--hooks-only') 30000;Check ($r.exit-eq0-and$r.reply.ok) "$arch actual product injection"
   # Tool windows intentionally stay out of the public default selector. Bind the
   # fresh owned fixture explicitly, then prove its subclass/queue acknowledgement.
   $windowOwner=[uint32]0;[void][ViPhysicalReadersNative]::GetWindowThreadProcessId([IntPtr]::new([int64]$target.hwnd),[ref]$windowOwner)
   Check ($windowOwner-eq$target.pid-and[ViPhysicalReadersNative]::IsWindow([IntPtr]::new([int64]$target.hwnd))) "$arch validates exact owned HWND before explicit adoption"
   $r=Vi $target @('config',('hwnd=0x{0:X}'-f[uint64]$target.hwnd),'isolate=true','fakefocus=true','raw=post');Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.hwnd-and[Convert]::ToUInt64($r.reply.hwnd.Substring(2),16)-eq[uint64]$target.hwnd) "$arch explicitly adopts the owned tool window"
   $r=Vi $target @('readiness','--wait-ms','1500');Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.hook_ready-and$r.reply.window_ready-and$r.reply.queue_ready) "$arch target subclass and fresh readiness ACK precede native/virtual controls"
   $s=Sample $target;Masked $s "$arch default isolation";InvalidArgs $s "$arch default isolation"
   $script:token++;Post $target (0x8000+0x68) $script:token;$classification=Await $target di_classification $script:token;$evidence.Add($classification)
   Check ($classification.entry_preflight_ok-and$classification.synthetic_selves_only-and$classification.unknown_hr-lt0-and$classification.unknown_count-eq0-and-not$classification.unknown_physical_bytes-and$classification.caps_attempts_before_valid-ge2) "$arch unknown DI classification fails closed after both capability layouts"
   Check ($classification.valid_retry_hr-ge0-and$classification.valid_retry_count-eq0-and-not$classification.valid_retry_physical_bytes-and$classification.caps_attempts_after_valid-gt$classification.caps_attempts_before_valid) "$arch failed DI classification is retried successfully instead of cached as other"
   Check ($classification.other_state_hr-ge0-and$classification.other_state_preserved-and$classification.other_buffer_hr-ge0-and$classification.other_buffer_count-eq1-and$classification.other_event_preserved) "$arch proven non-keyboard/mouse DI device retains its native policy"
   foreach($mode in @(2,3)){Model $target $mode;Masked (Sample $target) "$arch unverified foreground model$mode"}
   Model $target 1;$s=Sample $target;NativeSeen $s "$arch modeled verified owner foreground without lease/intent";InvalidArgs $s "$arch modeled foreground"
   Producer $target @('down','W')|Out-Null;$script:token++;Post $target (0x8000+0x67) $script:token;$limit=Await $target di_null_limit $script:token;$evidence.Add($limit)
   Check ($limit.model-eq1-and$limit.native_enabled-and$limit.requested-eq1-and$limit.hr-ge0-and$limit.returned_count-eq1-and$limit.before.w_down_present-and$limit.after.w_down_present) "$arch one native null-buffer slot leaves queued virtual event intact"
   Producer $target @('release')|Out-Null;Sample $target|Out-Null
   Model $target 0;Vi $target @('config','fakefocus=false')|Out-Null;Masked (Sample $target) "$arch isolate with fake focus disabled";Vi $target @('config','fakefocus=true')|Out-Null
   $lease=Vi $target @('config','isolate=false','--for','1500');Check ($lease.reply.physical_input.timed_passthrough-and$lease.reply.physical_input.warning-and$lease.reply.physical_input.remaining_ms-gt0-and$null-ne$lease.reply.physical_input.passthrough_elapsed_ms-and$null-ne$lease.reply.physical_input.last_explicit_set_age_ms-and$lease.reply.isolate-eq$lease.reply.physical_input.isolate) "$arch bounded opt-out reports warning, elapsed age and consistent snapshot"
   NativeSeen (Sample $target) "$arch bounded opt-out native source"
   Start-Sleep -Milliseconds 1650;$s=Sample $target;Masked $s "$arch reader enforces lease expiry";$status=Vi $target @('status');Check ($status.reply.isolate-and$status.reply.physical_input.expired_count-ge1-and-not$status.reply.physical_input.warning-and$null-eq$status.reply.physical_input.passthrough_elapsed_ms-and$status.reply.isolate-eq$status.reply.physical_input.isolate) "$arch expiry status agrees with masking and one isolation snapshot"
   $lease=Vi $target @('json','{"cmd":"config","isolate":false,"for_ms":1500}');Check ($lease.exit-eq0-and$lease.reply.physical_input.timed_passthrough-and$lease.reply.physical_input.warning) "$arch direct JSON timed opt-out parity";NativeSeen (Sample $target) "$arch direct JSON opt-out native source"
   Vi $target @('config','isolate=true')|Out-Null;$status=Vi $target @('status');Check ($status.reply.isolate-and-not$status.reply.physical_input.timed_passthrough-and$status.reply.physical_input.remaining_ms-eq0) "$arch explicit isolation cancels outstanding opt-out"
   $legacy=Vi $target @('config','isolate=false');Check ($legacy.reply.physical_input.warning-and-not$legacy.reply.physical_input.timed_passthrough-and-not$legacy.reply.isolate) "$arch legacy indefinite opt-out preserved with warning";NativeSeen (Sample $target) "$arch indefinite opt-out native source"
   Vi $target @('config','isolate=false','--for','60000')|Out-Null
   $invalids=@(@('config','isolate=false','--for','0'),@('config','isolate=false','--for','1.5'),@('config','isolate=false','--for','120001'),@('config','isolate=true','--for','500'),@('config','isolate=false','fullscreen=invalid','--for','500'),@('json','{"cmd":"config","isolate":false,"isolate":true,"for_ms":500}'),@('json','{"cmd":"config","isolate":false,"for_ms":"500"}'),@('json','{"cmd":"config","for_ms":500}'),@('json','{"cmd":"config","isolate":false,"for-ms":500}'),@('config','isolate=false','--for','500','--for','600'),@('config','isolate=false','--for','500','--for-ms','600'))
   foreach($argsBad in $invalids){$bad=Vi $target $argsBad;Check ($bad.exit-ne0-and-not$bad.reply.ok) "$arch invalid/ambiguous lease rejected";$status=Vi $target @('status');Check (-not$status.reply.isolate-and$status.reply.physical_input.remaining_ms-gt45000) "$arch rejected lease leaves previous configuration/deadline intact"}
   Vi $target @('config','isolate=true')|Out-Null;Native $target $false
   Producer $target @('down','W+Space')|Out-Null;Producer $target @('mousedown','left')|Out-Null;Producer $target @('move','37','-19')|Out-Null
   Memory-Probe $target 0;Memory-Probe $target 1
   $s=Sample $target
   Check (($s.keys.async_w-band0x8000)-ne0-and($s.keys.async_space-band0x8000)-ne0-and$s.keys.keyboard_w-eq128-and$s.keys.keyboard_space-eq128-and$s.di_keyboard.w-eq128-and$s.di_keyboard.space-eq128) "$arch virtual chord remains readable under isolation"
   Check ($s.di_mouse.dx-eq37-and$s.di_mouse.dy-eq-19-and$s.di_mouse.left-eq128-and$s.wm_input.sum_dx-eq37-and$s.wm_input.sum_dy-eq-19-and$s.wm_input.w_down-ge1-and$s.wm_input.space_down-ge1) "$arch virtual mouse and trusted raw transport remain readable"
   Stable-Peek $s.di_keyboard "$arch virtual keyboard";Stable-Peek $s.di_mouse "$arch virtual mouse"
   Check ($s.di_keyboard.peek.w_down_present-and$s.di_keyboard.peek.space_down_present-and$s.di_keyboard.peek.w_space_sequence_distinct) "$arch queued W and Space retain distinct stable sequence numbers"
   Producer $target @('release')|Out-Null
   if($arch-eq'x64'){
    $r=Vi $target @('config','raw=buffer');Check ($r.exit-eq0-and$r.reply.ok) "$arch virtual buffer mode accepted";Producer $target @('down','W+Space')|Out-Null;Producer $target @('move','37','-19')|Out-Null;Memory-Probe $target 0;$s=Sample $target
    Check ($s.raw.buffer_physical_records-eq0-and$s.raw.buffer_w_down-ge1-and$s.raw.buffer_space_down-ge1-and$s.raw.buffer_sum_dx-eq37-and$s.raw.buffer_sum_dy-eq-19) "$arch virtual raw-buffer records survive private native drain"
    Producer $target @('release')|Out-Null
   }
   $evidence.Add(@{arch=$arch;scope='modeled_foreground_only; no actual input/activation; XInput observation only';log=$log})
  }finally{
   if($target-and$target.process){Capture-Exit $target 'before cleanup'|Out-Null}
   if($owned-and-not$owned.HasExited){try{Post $target (0x8000+0x63)}catch{};if(-not$owned.WaitForExit(2000)){$owned.Kill();[void]$owned.WaitForExit(2000)}}
   if($target-and$target.process){Capture-Exit $target 'after owned cleanup'|Out-Null}
   if($owned){$owned.Dispose();$owned=$null}
  }
 }
 $complete=$true
}catch{$errorDetail=$_.Exception.Message;if(-not$checks.Count-or$checks[$checks.Count-1].pass){$checks.Add(@{pass=$false;name="runner error: $errorDetail"});Write-Host "FAIL runner error: $errorDetail"};Write-Host "STOP $errorDetail"}finally{
 if($owned-and-not$owned.HasExited){$owned.Kill();$owned.WaitForExit()}
 $passed=@($checks|Where-Object pass).Count;$failed=@($checks|Where-Object{-not$_.pass}).Count
 @{complete=$complete;passed=$passed;failed=$failed;error=$errorDetail;compatibility_included=(-not$SkipCompatibility);fixture_exits=@($evidence|Where-Object {$_.kind-eq'fixture_exit'});checks=$checks;evidence=$evidence;scope='fabricated native readers; modeled owner foreground; no real game/physical input/activation; XInput unchanged'}|ConvertTo-Json -Depth 25|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
 Write-Host "RESULT passed=$passed failed=$failed complete=$complete path=$run"
}
if(-not$complete-or$failed){exit 1}
