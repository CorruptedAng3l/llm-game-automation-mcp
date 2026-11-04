# Actual hooked mouse/focus polling. Lower readers fabricated; no game or desktop input.
param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$CliDir='', [string]$Out="$PSScriptRoot\_out\unity_mouse")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliDir).Path)
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay within private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r1_tail.cs"}
if(-not('ViUnityMouseNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViUnityMouseNative{
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
}
'@}
$checks=[Collections.Generic.List[object]]::new();$arches=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$processes=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new()
$complete=$false;$instrument=$true;$current=$null;$token=0;$serial=0;$problem=$null
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name"}
function Require([bool]$Good,[string]$Name){if(-not$Good){throw $Name}}
function Pin([string]$Path){$pins.Add(@{path=$Path;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath $Path).Length})}
function Rows([string]$Path){if(Test-Path -LiteralPath $Path){foreach($line in ((Get-Content -LiteralPath $Path -Raw)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}}
function Live($T){$r=ReadRetainedImage $T.process $T.path $T.created $T.retained_handle;$T.latest=$r;Require $r.matched 'fresh exact retained kernel fixture identity'}
function NewTracked($P,[string]$Image){
 # Record the original returned object BEFORE any metadata check can throw.
 $t=@{process=$P;pid=0;path=$Image;created=[long]0;retained_handle=[long]0;cache=$null;cleanup=$null;startup_error=$null};$processes.Add($t)
 try{$t.pid=$P.Id;$t.retained_handle=$P.Handle.ToInt64()}catch{$t.startup_error=$_.Exception.Message};return $t
}
function ValidateTracked($T){Require ($T.retained_handle-ne0-and-not$T.startup_error) 'original process handle retained';$T.created=$T.process.StartTime.ToUniversalTime().Ticks;$seen=ObserveStartupImage $T.process $T.path $T.created $T.retained_handle;$T.startup=$seen;Require $seen.matched 'exact owned startup image/PID/creation';$T.cache=New-ViStrictCliStartupCache (ReadRetainedImage $T.process $T.path $T.created $T.retained_handle) $T.retained_handle ([uint32]$T.pid) $T.created $T.path}
function Finish($T){if(-not$T-or$T.cleanup){return};try{$T.cleanup=AccountOrReapCli $T.process $T.path $T.created $T.retained_handle ([uint32]$T.pid) $T.cache}catch{$T.cleanup=@{completion_owned=$false;reaped=$false;termination_attempted=$false;error=$_.Exception.Message}};if(-not$T.cleanup.completion_owned-or$T.cleanup.error){$script:instrument=$false;Write-Warning ('Owned cleanup/accounting failed: '+$T.cleanup.error)}}
function RunOwned([string]$Image,[string[]]$Arguments,[int]$Budget=10000,[Threading.EventWaitHandle]$StartupGate=$null){
 $Image=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Image).Path)
 $script:serial++;$prefix=Join-Path $run ('call_'+$serial);$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Image;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$run;$si.Environment['TEMP']="$run\temp";$si.Environment['TMP']="$run\temp";foreach($arg in $Arguments){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;$t=$null;$outTail=$null;$errTail=$null;$record=@{image=$Image;arguments=$Arguments;exit=$null;stdout='';stderr='';cleanup=$null};$calls.Add($record)
 try{Require ($p.Start()) 'owned compiler/CLI started';$t=NewTracked $p $Image;$outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError);ValidateTracked $t;if($StartupGate){Require ($StartupGate.Set()) 'release private startup gate only after strict live kernel identity'};Require ($p.WaitForExit($Budget)) 'owned compiler/CLI bounded exit';$proof=Get-ViExitedCliAccounting $t.retained_handle ([uint32]$t.pid) $t.created $Image $t.cache;Require (Test-ViExitedCliAccounting $proof) 'readonly original-handle CLI exit accounting';$record.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);$record.exit_accounting=$proof
  $null=$outTail.Wait(500);$null=$errTail.Wait(500);$record.stdout=$outTail.Text();$record.stderr=$errTail.Text();$record.stdout_complete=$outTail.Complete;$record.stderr_complete=$errTail.Complete
  Require ($outTail.Complete-and$errTail.Complete-and-not$outTail.Error-and-not$errTail.Error-and$outTail.Characters-le65536-and$errTail.Characters-le65536) 'bounded complete separate stdout/stderr'
  return $record
 }finally{try{if($t){Finish $t;$record.cleanup=$t.cleanup}}finally{
   if($outTail){$null=$outTail.Wait(500);$record.stdout=$outTail.Text()};if($errTail){$null=$errTail.Wait(500);$record.stderr=$errTail.Text()};[IO.File]::WriteAllText($prefix+'.stdout.log',$record.stdout);[IO.File]::WriteAllText($prefix+'.stderr.log',$record.stderr);if($t){$t.process.Dispose();$t.Remove('process')};$p.Dispose()
  }}
}

function Invoke-ViUnityMouseCli([string[]]$Arguments){Live $current;$r=RunOwned $current.cli (@('--json','--pid',[string]$current.pid)+$Arguments);$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});Require ($lines.Count-eq1) 'one CLI JSON record';$r.reply=$lines[0]|ConvertFrom-Json;return $r}
function Control([uint32]$Action){Live $current;[uint32]$pidValue=0;$h=[IntPtr]::new([long][uint64]$current.ready.hwnd);Require ([ViUnityMouseNative]::GetWindowThreadProcessId($h,[ref]$pidValue)-eq$current.ready.thread-and$pidValue-eq$current.pid) 'fresh own control HWND/PID/thread';$script:token++;$mine=$token;Require ([ViUnityMouseNative]::PostMessageW($h,0x80B2,[UIntPtr]::new([uint64]$Action),[IntPtr]::new([long]$mine))) 'own control accepted';$end=[Environment]::TickCount64+4000;do{$row=@(Rows $current.log|Where-Object {$_.event-eq'control'-and$_.token-eq$mine})|Select-Object -Last 1;if($row){Require $row.ok 'actual control ACK';return $row};if($current.process.HasExited){throw 'fixture exited during control'};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);throw 'control ACK unavailable'}
function StartFixture([string]$Exe,[string]$CliPath,[string]$Dll,[string]$Name){
 $log=Join-Path $run ($Name+'.jsonl');$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;[void]$si.ArgumentList.Add($log);$p=[Diagnostics.Process]::new();$p.StartInfo=$si;Require ($p.Start()) 'owned mouse fixture started';$t=NewTracked $p $Exe;$script:current=$t;$t.cli=$CliPath;$t.log=$log;$t.observations=[Collections.Generic.List[object]]::new();ValidateTracked $t
 $end=[Environment]::TickCount64+5000;do{$ready=@(Rows $log|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
 $guard=@(Rows $log|Where-Object event -eq guards)|Select-Object -First 1;Require ($ready-and$ready.pid-eq$p.Id-and$ready.actual_offscreen_checked-and$guard.safe-and$guard.terminal_desktop_writers-eq19-and$guard.physical_reader_source-eq'fabricated'-and-not$guard.native_writer_called) 'terminal/fabricated-reader/offscreen prerequisites';$t.ready=$ready
 [uint32]$fg=0;[void][ViUnityMouseNative]::GetWindowThreadProcessId([ViUnityMouseNative]::GetForegroundWindow(),[ref]$fg);Require ($fg-ne$t.pid) 'actual foreground stays outside fixture'
 $r=Invoke-ViUnityMouseCli @('inject','--dll',$Dll,'--hooks-only');Require ($r.exit-eq0-and$r.reply.ok) 'chosen DLL actually injected';$j=@{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.hwnd);fakefocus=$true;blockfocus=$true;autoactivate=$false;isolate=$true}|ConvertTo-Json -Compress;$r=Invoke-ViUnityMouseCli @('json',$j);Require ($r.exit-eq0-and$r.reply.ok) 'exact healthy target configured';$r=Invoke-ViUnityMouseCli @('readiness');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.hook_ready-and$r.reply.window_ready-and$r.reply.queue_ready-and$r.reply.cursor_hooks_ready) 'actual full readiness';return $t
}
function StopCurrent{if($script:current){try{if(-not$current.process.HasExited){try{$null=Control 8;[void]$current.process.WaitForExit(500)}catch{}};Finish $script:current}finally{$script:current.process.Dispose();$script:current.Remove('process');$script:current=$null}}}

if(-not('ViR2Pipe'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r2_pipe.cs"}
function Native($Object,[int]$Budget=2500){Live $current;$request=$Object|ConvertTo-Json -Compress;$wire=[ViR2Pipe]::Query([uint32]$current.pid,$current.retained_handle,$current.created,$current.path,$request,$Budget);$record=@{request=$request;complete=$wire.Complete;server_pid=$wire.ServerPid;text=$wire.Text;error=$wire.Error};$calls.Add($record);Require ($wire.Complete-and$wire.ServerPid-eq$current.pid-and-not$wire.Error) 'bounded owned native pipe response';$record.reply=$wire.Text|ConvertFrom-Json;Require ($record.reply.ok-is[bool]) 'typed actual native result';return $record.reply}
function NStatus{return Native @{cmd='status'}}
function Snapshot($Ack){$row=@(Rows $current.log|Where-Object {$_.event-eq'snapshot'-and$_.token-eq$Ack.token})|Select-Object -Last 1;Require ($null-ne$row) 'fresh tokened own snapshot';return $row}
function Snap{return Snapshot (Control 0)}
function Configure([bool]$Fake=$true,[bool]$Block=$true,[bool]$Auto=$false){$r=Native @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$current.ready.hwnd);fakefocus=$Fake;blockfocus=$Block;autoactivate=$Auto;isolate=$true};Require $r.ok 'explicit current own configuration';return $r}
function AllUp{foreach($b in @('left','right','middle','x1','x2')){$r=Native @{cmd='mouseup';button=$b};Require $r.ok 'coordinate-free own release accepted'};$row=Snap;Require ($row.mask-eq0) 'actual all mouse buttons released'}
function At([int]$X=160,[int]$Y=120){$r=Native @{cmd='mouse';x=$X;y=$Y};Require $r.ok 'client-space virtual position accepted';return $r}
function MouseRequest([string]$Command,[string]$Button='left',[int]$X=160,[int]$Y=120){return Native @{cmd=$Command;button=$Button;x=$X;y=$Y;hover_frames=0;settle_ms=0;ms=180;di_poll_timeout_ms=0;di_release_timeout_ms=0}}
function Down([string]$Button='left',[int]$X=160,[int]$Y=120){return Native @{cmd='mousedown';button=$Button;x=$X;y=$Y;hover_frames=0;settle_ms=0}}
function Up([string]$Button='left'){return Native @{cmd='mouseup';button=$Button}}
function WaitMask([int]$Mask,[int]$Budget=800){$end=[Environment]::TickCount64+$Budget;do{$row=Snap;if($row.mask-eq$Mask){return $row};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);return $row}
function Background{Live $current;[uint32]$fg=0;[void][ViUnityMouseNative]::GetWindowThreadProcessId([ViUnityMouseNative]::GetForegroundWindow(),[ref]$fg);Require ($fg-ne$current.pid) 'independent actual foreground remains outside fixture'}
function BeginCase([string]$Name,[uint32]$Action=1){AllUp;Background;$row=Snapshot (Control $Action);$case=@{name=$Name;token=$row.case_token;before=$row;completed=$false};$cases.Add($case);return $case}
function EndCase($Case){AllUp;Background;$Case.cleanup=Snap;$Case.completed=$true}

try{
 foreach($f in @($PSCommandPath,"$PSScriptRoot\vi_r1_identity.ps1","$PSScriptRoot\vi_exited_cli_accounting.ps1","$PSScriptRoot\vi_r1_lifecycle.ps1","$PSScriptRoot\..\test\vi_r1_tail.cs","$PSScriptRoot\..\test\vi_r2_spies.h","$PSScriptRoot\..\test\vi_r2_pipe.cs","$PSScriptRoot\..\test\vi_unitymouse.cpp")){Pin $f}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry)
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$cli=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'}))).Path);$dll=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'}))).Path);Pin $cli;Pin $dll
   $mh=Join-Path $RepoRoot "build\$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'};$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$exe=Join-Path $folder 'vi_unitymouse.exe';$batch=Join-Path $folder 'build.cmd'
   @"
@echo off
call "$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_unitymouse.cpp" /Fo:"$folder\fixture.obj" /Fe:"$exe" /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'W4/WX fixture zero warnings';Pin $exe
   $current=StartFixture $exe $cli $dll ($arch+'.target');$entry.ready=$current.ready
   $beforeDll=@(Rows $current.log|Where-Object event -eq before_dll)|Select-Object -First 1;Require ($beforeDll.mask-eq31-and$beforeDll.screen_x-eq5000-and$beforeDll.screen_y-eq6000-and-not$beforeDll.foreground-and-not$beforeDll.active-and-not$beforeDll.focus) 'fabricated native mouse/cursor/focus source positive';$entry.fabricated_source=$beforeDll

   $case=BeginCase "$arch.isolation_and_focus";$null=At;$row=Snap;$case.actual=$row
   Check ($row.mask-eq0-and$row.foreground-and$row.active-and$row.focus-and$row.gate-and$row.roundtrip_ok-and$row.client_x-eq160-and$row.client_y-eq120) "$arch physical mouse highs masked; actual virtual cursor and UI focus agree";EndCase $case

   $case=BeginCase "$arch.timed_click";$case.command=MouseRequest 'click';$row=Snap;$case.actual=$row
   Check ($case.command.ok-and$row.mask-eq0-and$row.down_samples-gt0-and$row.hits-eq1-and$row.first_down-gt0-and$row.up_after-ge$row.last_down-and$row.up_after-gt0-and$row.roundtrip_ok) "$arch timed left click was actually polled, hit once and released";EndCase $case

   $buttons=@('left','right','middle','x1','x2');$traceNames=@('lbutton','rbutton','mbutton','xbutton1','xbutton2')
   for($i=0;$i-lt5;$i++){
    $case=BeginCase "$arch.held_$($buttons[$i])";$before=Native @{cmd='trace';key=$traceNames[$i]};$case.command=Down $buttons[$i];$down=WaitMask (1-shl$i);$case.down=$down;$case.release=Up $buttons[$i];$up=WaitMask 0;$case.actual=$up;$after=Native @{cmd='trace';key=$traceNames[$i]};$case.trace_before=$before;$case.trace_after=$after
    Check ($case.command.ok-and$case.release.ok-and$down.mask-eq(1-shl$i)-and$down.point_inside-and$down.gate-and$down.roundtrip_ok-and$down.hits-eq1-and$up.mask-eq0-and$up.up_after-ge$down.first_down-and$after.channels.GetAsyncKeyState.down_reads-gt$before.channels.GetAsyncKeyState.down_reads-and-not$after.virtual_down) "$arch actual held/released $($buttons[$i]) high bit, focus gate and per-VK trace";EndCase $case
   }

   $case=BeginCase "$arch.wrong_point";$case.command=Down 'left' 20 20;$row=WaitMask 1;$case.actual=$row;$case.release=Up
   Check ($case.command.ok-and$row.mask-eq1-and$row.gate-and-not$row.point_inside-and$row.hits-eq0-and$row.client_x-eq20-and$row.client_y-eq20) "$arch wrong point refuses application hit despite actual mouse state";EndCase $case

   $null=Configure $false $true $false;$case=BeginCase "$arch.fake_focus_disabled";$case.command=Down;$row=WaitMask 1;$case.actual=$row;$case.release=Up
   Check ($case.command.ok-and$row.mask-eq1-and$row.point_inside-and-not$row.foreground-and-not$row.active-and-not$row.focus-and-not$row.gate-and$row.hits-eq0) "$arch absent fake focus rejects application hit while virtual button still arrives";EndCase $case;$null=Configure

   $case=BeginCase "$arch.cached_inactive" 3;$case.command=Down;$row=WaitMask 1;$case.actual=$row;$case.release=Up
   Check ($case.command.ok-and$row.mask-eq1-and$row.foreground-and$row.active-and$row.focus-and$row.requires_cached-and-not$row.cached_active-and-not$row.gate-and$row.hits-eq0) "$arch explicit cached inactive application gate rejects an otherwise visible press";EndCase $case

   $case=BeginCase "$arch.worker_focus_boundary";$ack=Control 4;$worker=@(Rows $current.log|Where-Object {$_.event-eq'worker_focus'-and$_.token-eq$ack.token})|Select-Object -Last 1;$case.actual=$worker
   Check ($null-ne$worker-and$worker.foreground_is_target-and$worker.active_is_null-and$worker.focus_is_null-and$worker.thread-ne$current.ready.thread) "$arch worker keeps null active/focus boundary while foreground shim is process-wide";EndCase $case

   # First make BOTH target and vinput observe a normal application-inactive message.
   # Private cached flag reset alone must never be used to demand another refresh.
   $null=Configure $false $false $false;$marker=Snap;$null=Control 5;$end=[Environment]::TickCount64+1000
   do{$inactive=Snap;$inactiveMessage=@(Rows $current.log|Where-Object {$_.event-eq'activateapp'-and-not$_.active-and$_.tick-ge$marker.tick})|Select-Object -Last 1;$inactiveStatus=NStatus;if($inactiveMessage-and-not$inactive.cached_active-and$inactiveStatus.app_active-eq0){break};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end)
   Require ($null-ne$inactiveMessage-and-not$inactive.cached_active-and$inactiveStatus.app_active-eq0) 'actual owned inactive delivery before guarded-refresh control'
   $case=BeginCase "$arch.guarded_cached_activation" 2;$case.inactive=$inactiveMessage;$null=Configure $true $true $true;$end=[Environment]::TickCount64+3500
   do{$active=Snap;if($active.cached_active){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
   $case.activation=$active;$case.command=Down;$row=WaitMask 1;$case.actual=$row;$case.release=Up;$case.status=NStatus
   Check ($active.cached_active-and$case.command.ok-and$row.mask-eq1-and$row.gate-and$row.point_inside-and$row.hits-eq1-and$case.status.app_active-eq1) "$arch existing guarded WM_ACTIVATEAPP refresh enables actual cached-gate hit";EndCase $case
   $entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}finally{if($current){try{AllUp}catch{$instrument=$false;Check $false "$arch explicit release cleanup failed"}};StopCurrent}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2-and$cases.Count-eq24-and@($cases|Where-Object completed -eq $true).Count-eq24
}catch{$problem=$_.Exception.Message;Check $false "setup: $problem"}
@{schema=1;completed=$complete;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;cases=$cases;arches=$arches;calls=$calls;processes=$processes;pins=$pins;checks=$checks;error=$problem;failure_is_expected_inversion=$false;game_measured=$false;F1_cause='UNKNOWN';physical_reader_source='fabricated';actual_parent_foreground_witness=$true;scope='actual hooked five mouse VKs, screen/client cursor, UI/worker focus and source-owned application gates; no Unity/game/native physical compatibility claim'}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $(@($checks|Where-Object pass -eq $true).Count) PASS $(@($checks|Where-Object pass -eq $false).Count) FAIL $run";if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
