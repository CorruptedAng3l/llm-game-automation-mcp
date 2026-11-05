# Synthetic activation-gated D3D11 progress and loaded/hooks/queue diagnostics. Lower focus readers are fabricated; no game or desktop input.
param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$CliDir='', [string]$Out="$PSScriptRoot\_out\startup_activation")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliDir).Path)
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay within private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r1_tail.cs"}
if(-not('ViStartupNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViStartupNative{
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

function Invoke-ViStartupCli([string[]]$Arguments){Live $current;$r=RunOwned $current.cli (@('--json','--pid',[string]$current.pid)+$Arguments);$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});Require ($lines.Count-eq1) 'one CLI JSON record';$r.reply=$lines[0]|ConvertFrom-Json;return $r}
function Control([uint32]$Action){Live $current;[uint32]$pidValue=0;$h=[IntPtr]::new([long][uint64]$current.ready.hwnd);Require ([ViStartupNative]::GetWindowThreadProcessId($h,[ref]$pidValue)-eq$current.ready.thread-and$pidValue-eq$current.pid) 'fresh own control HWND/PID/thread';$script:token++;$mine=$token;Require ([ViStartupNative]::PostMessageW($h,0x80B3,[UIntPtr]::new([uint64]$Action),[IntPtr]::new([long]$mine))) 'own control accepted';$end=[Environment]::TickCount64+4000;do{$row=@(Rows $current.log|Where-Object {$_.event-eq'control'-and$_.token-eq$mine})|Select-Object -Last 1;if($row){Require $row.ok 'actual control ACK';return $row};if($current.process.HasExited){throw 'fixture exited during control'};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);throw 'control ACK unavailable'}
function StartFixture([string]$Exe,[string]$CliPath,[string]$Mode,[string]$Name,[string]$ResumeName=''){
 $log=Join-Path $run ($Name+'.jsonl');$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;foreach($arg in @($log,$Mode)){[void]$si.ArgumentList.Add($arg)};if($ResumeName){[void]$si.ArgumentList.Add($ResumeName)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;Require ($p.Start()) 'owned activation fixture started';$t=NewTracked $p $Exe;$script:current=$t;$t.cli=$CliPath;$t.log=$log;ValidateTracked $t
 $end=[Environment]::TickCount64+8000;do{$ready=@(Rows $log|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
 $guard=@(Rows $log|Where-Object event -eq guards)|Select-Object -First 1;$before=@(Rows $log|Where-Object event -eq before_dll)|Select-Object -First 1
 Require ($ready-and$ready.pid-eq$p.Id-and$ready.actual_offscreen_checked-and$ready.window_visible-and$ready.windowed_swapchain-and$guard.safe-and$guard.terminal_desktop_writers-eq19-and-not$guard.native_writer_called) 'safe offscreen visible/windowed/terminal prerequisites'
 $t.ready=$ready;$t.before=$before;Background
 Require ($before.present_calls-eq0-and-not$before.app_active-and-not$before.api_gate) 'post-setup consumer pre-DLL gate closed with zero application Present calls'
 if(-not$ResumeName){$first=Snap;Start-Sleep -Milliseconds 50;$second=Snap;$t.pre_dll_baseline=@($first,$second);Require (-not$first.gate-and-not$second.gate-and$first.present_calls-eq0-and$second.present_calls-eq0) 'late natural activation/progress makes the pre-DLL prerequisite ambiguous'}
 return $t
}
function StopCurrent{if($script:current){try{if(-not$current.process.HasExited){try{$null=Control 8;[void]$current.process.WaitForExit(500)}catch{}};Finish $script:current}finally{$script:current.process.Dispose();$script:current.Remove('process');$script:current=$null}}}

if(-not('ViR2Pipe'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r2_pipe.cs"}
function Native($Object,[int]$Budget=2500){Live $current;$request=$Object|ConvertTo-Json -Compress;$wire=[ViR2Pipe]::Query([uint32]$current.pid,$current.retained_handle,$current.created,$current.path,$request,$Budget);$record=@{request=$request;complete=$wire.Complete;server_pid=$wire.ServerPid;text=$wire.Text;error=$wire.Error};$calls.Add($record);Require ($wire.Complete-and$wire.ServerPid-eq$current.pid-and-not$wire.Error) 'bounded owned native pipe response';$record.reply=$wire.Text|ConvertFrom-Json;Require ($record.reply.ok-is[bool]) 'typed actual native result';return $record.reply}
function NStatus{return Native @{cmd='status'}}
function Background{Live $current;[uint32]$fg=0;[void][ViStartupNative]::GetWindowThreadProcessId([ViStartupNative]::GetForegroundWindow(),[ref]$fg);Require ($fg-ne$current.pid) 'actual parent foreground stays outside target'}
function Snapshot($Ack){$row=@(Rows $current.log|Where-Object {$_.event-eq'snapshot'-and$_.token-eq$Ack.token})|Select-Object -Last 1;Require ($null-ne$row) 'exact tokened startup snapshot';return $row}
function Snap{return Snapshot (Control 0)}
function Configure([bool]$Fake,[bool]$Auto,[bool]$Block=$true){$r=Native @{cmd='config';hwnd=('0x{0:X}'-f[uint64]$current.ready.hwnd);fakefocus=$Fake;autoactivate=$Auto;blockfocus=$Block;isolate=$true};Require $r.ok 'explicit known own startup configuration';return $r}
function HooksOnly([string]$Dll){$r=Invoke-ViStartupCli @('inject','--dll',$Dll,'--hooks-only');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.injected-and$r.reply.readiness.hook_ready) 'chosen DLL actually loaded with hooks-only readiness';return $r}
function FullReady{$r=Invoke-ViStartupCli @('readiness');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.hook_ready-and$r.reply.window_ready-and$r.reply.queue_ready) 'fresh actual full window/queue readiness';return $r}
function Progress([int]$Budget=4500){$end=[Environment]::TickCount64+$Budget;do{$row=Snap;if($row.present_calls-gt0){return $row};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end);return $row}
try{
 foreach($p in @($PSCommandPath,"$PSScriptRoot\..\test\vi_startupactivation.cpp","$PSScriptRoot\..\test\vi_r2_spies.h","$PSScriptRoot\..\test\vi_r2_pipe.cs","$PSScriptRoot\..\test\vi_r1_tail.cs","$PSScriptRoot\vi_r1_identity.ps1","$PSScriptRoot\vi_exited_cli_accounting.ps1","$PSScriptRoot\vi_r1_lifecycle.ps1")){Pin $p}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry)
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$exe=Join-Path $folder 'vi_startupactivation.exe';$batch=Join-Path $folder 'build.cmd';$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$mh=Join-Path $RepoRoot "build\$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
   $cli=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'}))).Path);$dll=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'}))).Path);Pin $cli;Pin $dll
   @"
@echo off
call "$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_startupactivation.cpp" /Fo:"$folder\fixture.obj" /Fe:"$exe" /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'native fixture W4/WX zero warnings';Pin $exe
   foreach($mode in @('app','activate','api','setfocus')){
    $case=@{name="$arch.$mode.positive";completed=$false};$cases.Add($case)
    try{$current=StartFixture $exe $cli $mode $case.name;$case.ready=$current.ready;$case.before=$current.before;$case.inject=HooksOnly $dll;$null=Configure $true $true;$case.readiness=FullReady;$row=Progress;$case.actual=$row;$case.status=NStatus
     Check ($row.gate-and$row.present_calls-gt0-and$case.status.frames-gt0) "$arch $mode activation-gated actual D3D11 Present progresses in background";Background;$case.completed=$true
    }finally{StopCurrent}
   }
   foreach($mode in @('app','api')){
    $case=@{name="$arch.$mode.negative";completed=$false};$cases.Add($case)
    try{$current=StartFixture $exe $cli $mode $case.name;$case.ready=$current.ready;$case.inject=HooksOnly $dll;$null=Configure $false $false $false;$marker=Snap;$null=Control 1;$end=[Environment]::TickCount64+1000
     do{$row=Snap;$st=NStatus;if(-not$row.app_active-and-not$row.window_active-and$st.app_active-eq0){break};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end)
     Require (-not$row.app_active-and-not$row.window_active-and$st.app_active-eq0) 'actual own inactive messages reached original callback/cache'
     $null=Configure ($mode-eq'app') $false;$case.readiness=FullReady;$before=Snap;Start-Sleep -Milliseconds 150;$after=Snap;$case.before=$before;$case.actual=$after
     Check (-not$after.gate-and$before.present_calls-eq$after.present_calls-and($mode-ne'app'-or$after.api_gate)) "$arch $mode gate blocks new Presents while input queue can be ready";Background;$case.completed=$true
    }finally{StopCurrent}
   }
   $case=@{name="$arch.paused_queue";completed=$false};$cases.Add($case);$resume=$null
   try{$created=$false;$resumeName='Local\ViStartup_'+[guid]::NewGuid().ToString('N');$resume=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$resumeName,[ref]$created);Require $created 'fresh own resume event created'
    $current=StartFixture $exe $cli 'app' $case.name $resumeName;$case.ready=$current.ready;$case.inject_hooks=HooksOnly $dll;$end=[Environment]::TickCount64+2500
    do{$armed=Native @{cmd='readiness';hooks_only=$true;wait_ms=0};if($armed.window_ready){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
    Require ($armed.hook_ready-and$armed.window_ready-and$armed.hwnd-eq('0x{0:X}'-f[uint64]$current.ready.hwnd)) 'paused target adopted without a queue ACK';$case.armed=$armed
    $failed=Invoke-ViStartupCli @('inject','--dll',$dll);$case.failed=$failed
    Check ($failed.exit-eq2-and-not$failed.reply.ok-and$failed.reply.injected-and$failed.reply.already_injected-and$failed.reply.readiness.readiness_reason-eq'queue_not_progressing'-and$failed.reply.ready-is[bool]-and-not$failed.reply.ready-and$failed.reply.hooks_ready) "$arch loaded/hooks-ready remains explicit while bounded queue readiness fails"
    Live $current;Require ($resume.Set()) 'own original resume event signaled after strict target identity';$case.resumed_readiness=FullReady;$again=Invoke-ViStartupCli @('inject','--dll',$dll);$case.inject_ready=$again;$case.actual=Progress
    Check ($again.exit-eq0-and$again.reply.ok-and$again.reply.injected-and$again.reply.already_injected-and$again.reply.ready-is[bool]-and$again.reply.ready-and$case.actual.present_calls-gt0) "$arch same original PID becomes ready later without treating already-injected as failure";Background;$case.completed=$true
   }finally{try{if($resume-and$current-and-not$current.process.HasExited){Live $current;[void]$resume.Set()}}finally{if($resume){$resume.Dispose()};StopCurrent}}
   $entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}finally{StopCurrent}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2-and$cases.Count-eq14-and@($cases|Where-Object completed -eq $true).Count-eq14
}catch{$problem=$_.Exception.Message;Check $false "setup: $problem"}
@{schema=1;completed=$complete;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;cases=$cases;arches=$arches;calls=$calls;processes=$processes;pins=$pins;error=$problem;failure_is_expected_inversion=$false;retail_cause='UNKNOWN';source_window_created_before_injection=$true;early_default_creation_visibility_tested=$false;scope='actual activation-gated synthetic D3D11 progress plus strict loaded/hooks/queue diagnostics; real foreground stays external; not UE4/retail proof'}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $(@($checks|Where-Object pass -eq $true).Count) PASS $(@($checks|Where-Object pass -eq $false).Count) FAIL $run";if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
