# Synthetic G1 PARTIAL claimed-writer contract. All19 lower writers terminate; no real OS clip/escape proof.
# Unchanged fixture for actual before/after; no expected-red inversion.
param([Parameter(Mandatory=$true)][string]$RepoRoot,[string]$CliDir='', [string]$Out="$PSScriptRoot\_out\claimed_cursor_writer")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliDir).Path)
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must remain under private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r1_tail.cs"}
if(-not('ViR1Native'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViR1Native{
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
}
'@}
$checks=[Collections.Generic.List[object]]::new();$arches=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$processes=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new()
$complete=$false;$instrument=$true;$current=$null;$token=0;$serial=0;$problem=$null
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name"}
function Require([bool]$Good,[string]$Name){Check $Good $Name;if(-not$Good){throw $Name}}
function Pin([string]$Path){$pins.Add(@{path=$Path;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath $Path).Length})}
function Rows([string]$Path){if(Test-Path -LiteralPath $Path){foreach($line in ((Get-Content -LiteralPath $Path -Raw)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}}
function Live($T){$r=ReadRetainedImage $T.process $T.path $T.created $T.retained_handle;$T.latest=$r;Require $r.matched 'fresh exact retained kernel fixture identity'}
function NewTracked($P,[string]$Image){
 # Record the original returned object BEFORE any metadata check can throw.
 $t=@{process=$P;pid=$P.Id;path=$Image;created=[long]0;retained_handle=[long]0;cache=$null;cleanup=$null;startup_error=$null};$processes.Add($t)
 try{$t.retained_handle=$P.Handle.ToInt64()}catch{$t.startup_error=$_.Exception.Message};return $t
}
function ValidateTracked($T){Require ($T.retained_handle-ne0-and-not$T.startup_error) 'original process handle retained';$T.created=$T.process.StartTime.ToUniversalTime().Ticks;$seen=ObserveStartupImage $T.process $T.path $T.created $T.retained_handle;$T.startup=$seen;Require $seen.matched 'exact owned startup image/PID/creation';$T.cache=New-ViStrictCliStartupCache (ReadRetainedImage $T.process $T.path $T.created $T.retained_handle) $T.retained_handle ([uint32]$T.pid) $T.created $T.path}
function Finish($T){if(-not$T-or$T.cleanup){return};try{$T.cleanup=AccountOrReapCli $T.process $T.path $T.created $T.retained_handle ([uint32]$T.pid) $T.cache}catch{$T.cleanup=@{completion_owned=$false;reaped=$false;termination_attempted=$false;error=$_.Exception.Message}};Check ($T.cleanup.completion_owned-and-not$T.cleanup.error) 'owned process finite cleanup/accounting';if(-not$T.cleanup.completion_owned){$script:instrument=$false}}
function RunOwned([string]$Image,[string[]]$Arguments,[int]$Budget=10000){
 $script:serial++;$prefix=Join-Path $run ('call_'+$serial);$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Image;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$run;$si.Environment['TEMP']="$run\temp";$si.Environment['TMP']="$run\temp";foreach($arg in $Arguments){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;$t=$null;$outTail=$null;$errTail=$null;$record=@{image=$Image;arguments=$Arguments;exit=$null;stdout='';stderr='';cleanup=$null};$calls.Add($record)
 try{Require ($p.Start()) 'owned compiler/CLI started';$t=NewTracked $p $Image;$outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError);ValidateTracked $t;Require ($p.WaitForExit($Budget)) 'owned compiler/CLI bounded exit';$proof=Get-ViExitedCliAccounting $t.retained_handle ([uint32]$t.pid) $t.created $Image $t.cache;Require (Test-ViExitedCliAccounting $proof) 'readonly original-handle CLI exit accounting';$record.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);$record.exit_accounting=$proof
  $null=$outTail.Wait(500);$null=$errTail.Wait(500);$record.stdout=$outTail.Text();$record.stderr=$errTail.Text();$record.stdout_complete=$outTail.Complete;$record.stderr_complete=$errTail.Complete
  Require ($outTail.Complete-and$errTail.Complete-and-not$outTail.Error-and-not$errTail.Error-and$outTail.Characters-le65536-and$errTail.Characters-le65536) 'bounded complete separate stdout/stderr'
  return $record
 }finally{try{if($t){Finish $t;$record.cleanup=$t.cleanup}}finally{
   if($outTail){$null=$outTail.Wait(500);$record.stdout=$outTail.Text()};if($errTail){$null=$errTail.Wait(500);$record.stderr=$errTail.Text()};[IO.File]::WriteAllText($prefix+'.stdout.log',$record.stdout);[IO.File]::WriteAllText($prefix+'.stderr.log',$record.stderr);if($t){$t.process.Dispose();$t.Remove('process')};$p.Dispose()
  }}
}
function Invoke-ViR1Cli([string[]]$Arguments){Live $current;$r=RunOwned $current.cli (@('--json','--pid',[string]$current.pid)+$Arguments);$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});Require ($lines.Count-eq1) 'one CLI JSON record';$r.reply=$lines[0]|ConvertFrom-Json;return $r}
function Status{$r=Invoke-ViR1Cli @('status');Require ($r.exit-eq0-and$r.reply.ok) 'actual status reply';return $r.reply}
function Native($s){return $s.cursor_guard.native_clip}
function Config([uint64]$Hwnd){$j=@{cmd='config';hwnd=('0x{0:X}'-f$Hwnd);fakefocus=$false;blockfocus=$false;autoactivate=$false;isolate=$true;cursor='auto'}|ConvertTo-Json -Compress;$r=Invoke-ViR1Cli @('json',$j);Require ($r.exit-eq0-and$r.reply.ok) 'exact owned target configured';$r=Invoke-ViR1Cli @('json','{"cmd":"readiness"}');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.hook_ready-and$r.reply.window_ready-and$r.reply.queue_ready-and$r.reply.cursor_hooks_ready) 'full actual hook/window/queue/cursor readiness'}
function Control([uint32]$Action){Live $current;[uint32]$pidValue=0;$h=[IntPtr]::new([long][uint64]$current.ready.a);Require ([ViR1Native]::GetWindowThreadProcessId($h,[ref]$pidValue)-eq$current.ready.thread-and$pidValue-eq$current.pid) 'exact live own control HWND/PID/thread';$script:token++;$mine=$token;Require ([ViR1Native]::PostMessageW($h,0x80A0,[UIntPtr]::new([uint64]$Action),[IntPtr]::new([long]$mine))) 'owned tokened control accepted';$until=[Environment]::TickCount64+4000;do{$row=@(Rows $current.log|Where-Object {$_.event-eq'snapshot'-and$_.token-eq$mine})|Select-Object -Last 1;if($row){$current.observations.Add($row);return $row};if($current.process.HasExited){throw 'fixture exited awaiting control'};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'owned control receipt unavailable'}
function WaitNative([scriptblock]$Predicate){$until=[Environment]::TickCount64+1800;$last=$null;do{$last=Status;if(& $Predicate (Native $last)){return $last};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$until);return $last}
function WaitRace{$until=[Environment]::TickCount64+2500;do{$r=Control 0;if($r.race_done){return $r};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until);return $r}
function StartFixture([string]$Exe,[string]$CliPath,[string]$DllPath,[string]$Name){
 $log=Join-Path $run ($Name+'.jsonl');$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;[void]$si.ArgumentList.Add($log);$p=[Diagnostics.Process]::new();$p.StartInfo=$si;Require ($p.Start()) 'owned offscreen fixture started';$t=NewTracked $p $Exe;$script:current=$t;$t.cli=$CliPath;$t.log=$log;$t.observations=[Collections.Generic.List[object]]::new();ValidateTracked $t
 $until=[Environment]::TickCount64+4000;do{$ready=@(Rows $log|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 $guard=@(Rows $log|Where-Object event -eq guards)|Select-Object -First 1;Require ($ready-and$ready.pid-eq$p.Id-and$ready.terminal_spies-and$ready.modeled_foreground-and$ready.private_clip_only-and$guard.verified-and$guard.terminal_writes-eq19-and-not$guard.native_writer_called-and-not$guard.keybd_event_probed) 'terminal lower spies/private clip/independent foreground fixture prerequisite';$t.ready=$ready
 $inject=Invoke-ViR1Cli @('inject','--dll',$DllPath,'--hooks-only');Require ($inject.exit-eq0-and$inject.reply.ok) 'actual chosen DLL hooks-only injection';Config ([uint64]$ready.a);return $t
}
function StopCurrent{if($script:current){try{Finish $script:current}finally{$script:current.process.Dispose();$script:current.Remove('process');$script:current=$null}}}
try{
 foreach($f in @($PSCommandPath,"$PSScriptRoot\vi_r1_identity.ps1","$PSScriptRoot\vi_exited_cli_accounting.ps1","$PSScriptRoot\vi_r1_lifecycle.ps1","$PSScriptRoot\..\test\vi_r1_tail.cs","$PSScriptRoot\..\test\vi_nativeclip.cpp")){Pin $f}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry);$current=$null
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$cli=Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'});$dll=Join-Path $CliDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'});$cli=(Resolve-Path -LiteralPath $cli).Path;$dll=(Resolve-Path -LiteralPath $dll).Path;Pin $cli;Pin $dll
   $mh=Join-Path $RepoRoot "build\$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'};foreach($n in @('buffer','hook','trampoline',$hde)){Pin "$mh\$n.obj"};$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$exe=Join-Path $folder 'vi_nativeclip.exe';$batch=Join-Path $folder 'build.cmd'
   @"
@echo off
call "$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_nativeclip.cpp" /Fo:"$folder\fixture.obj" /Fe:"$exe" /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'both-arch fixture W4/WX compile zero warnings';Pin $exe
   $current=StartFixture $exe $cli $dll ($arch+'.claim');$ready=$current.ready;$entry.ready=$ready
   $owner='g1-cursor-'+[guid]::NewGuid().ToString('N')
   $null=Control 2
   $lease=Invoke-ViR1Cli @('json','{"cmd":"foreground-lease","ms":5000}');Require ($lease.exit-eq0-and$lease.reply.ok) 'clock-only lease without activation'
   $before=Control 0;$snap=Control 19
   Check ($snap.position_writes-eq$before.position_writes+2) "$arch unclaimed modeled owner preserves both native setter paths"
   $before=Control 0;$snap=Control 5
   Check ($snap.call_ok-and$snap.sets-eq$before.sets+1) "$arch unclaimed modeled owner preserves contained native clip"
   $entry.unclaimed_snapshot=$snap
   $claim=Invoke-ViR1Cli @('--owner',$owner,'claim','--ttl','60');Require ($claim.exit-eq0-and$claim.reply.ok) 'actual ordinary claim accepted with no pending native callback'
   $entry.claim_reply=$claim.reply
   $s=WaitNative {param($v) $v.policy_version-eq1-and-not$v.pending};$after=Control 0
   Check (-not(Native $s).pending-and$after.clears-eq$snap.clears+1-and-not$after.fake_enabled) "$arch new claim conditionally releases its prior tracked native clip"
   $lease=Invoke-ViR1Cli @('--owner',$owner,'json','{"cmd":"foreground-lease","ms":5000}');Require ($lease.exit-eq0-and$lease.reply.ok) 'renewed clock-only lease while claimed without activation'
   $before=Control 0;$snap=Control 19
   Check ($snap.position_writes-eq$before.position_writes) "$arch live claim overrides foreground lease for both native setters"
   $before=Control 0;$snap=Control 5;$s=Status
   Check ($snap.call_ok-and$snap.sets-eq$before.sets-and$s.cursor_guard.virtual_clip.enabled) "$arch live claim confines only requested virtual shadow"
   $before=Control 0;$snap=Control 6
   Check ($snap.clears-eq$before.clears-and-not$snap.fake_enabled) "$arch claimed NULL without native receipt makes no lower clear"
   $s=Status;$guard=$s.cursor_guard.claim_write_guard
   Check ($guard.version-eq1-and$guard.live_claim-eq$true-and$guard.native_operations_inflight-eq0-and$guard.full_escape_ready-eq$false-and-not$guard.clip_cleanup_pending) "$arch partial writer guard reports live claim and honestly refuses full escape readiness"
   Check ($s.cursor_guard.write_reason-eq'agent_claim_active'-and-not$s.cursor_guard.owner_active) "$arch claim overrides current foreground native-write permission receipt"
   $entry.claimed_status=$s;$entry.claimed_snapshot=$snap
   $unclaim=Invoke-ViR1Cli @('--owner',$owner,'unclaim');Require ($unclaim.exit-eq0-and$unclaim.reply.ok) 'same-owner unclaim remains available'
   $lease=Invoke-ViR1Cli @('json','{"cmd":"foreground-lease","ms":5000}');Require ($lease.exit-eq0-and$lease.reply.ok) 'fresh unclaimed clock-only lease without activation'
   $before=Control 0;$snap=Control 19
   Check ($snap.position_writes-eq$before.position_writes+2) "$arch unclaim restores unchanged leased native setter behavior"
   $before=Control 0;$snap=Control 5
   Check ($snap.sets-eq$before.sets+1) "$arch unclaim restores unchanged contained native clip behavior"
   $null=Control 6
   $lease=Invoke-ViR1Cli @('json','{"cmd":"foreground-lease","ms":0}');Require ($lease.exit-eq0-and$lease.reply.ok) 'clock-only lease cleared without activation'
   [uint32]$fgPid=0;[void][ViR1Native]::GetWindowThreadProcessId([ViR1Native]::GetForegroundWindow(),[ref]$fgPid)
   Check ($fgPid-ne$current.pid) "$arch actual desktop foreground remains external"
   $entry.final_status=Status;$entry.observations=$current.observations;StopCurrent
   $entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}finally{StopCurrent}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2
}catch{$instrument=$false;$problem=$_.Exception.Message;Check $false "setup: $problem"}
@{schema=1;completed=$complete;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;checks=$checks;arches=$arches;calls=$calls;processes=$processes;pins=$pins;error=$problem;failure_is_expected_inversion=$false;native_os_clip_tested=$false;foreground_model='independent lower GFG+GUI';clip_model='private RECT only; all native writers terminate';keybd_event_probed=$false;compiler_descendant_lifetime='run under normal external suite/compiler job boundary; own original process only';tool_issues=@()}|ConvertTo-Json -Depth 35|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $(@($checks|Where-Object pass -eq $true).Count) PASS $(@($checks|Where-Object pass -eq $false).Count) FAIL $run";if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
