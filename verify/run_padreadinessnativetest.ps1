# Actual XInput + actual MCP response image. No game/physical API is driven.
param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$CliDir='', [string]$Out="$PSScriptRoot\_out\pad_readiness_native")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliDir).Path)
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay within private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r1_tail.cs"}
if(-not('ViControllerNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViControllerNative{
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

function Invoke-ViControllerCli([string[]]$Arguments){Live $current;$r=RunOwned $current.cli (@('--json','--pid',[string]$current.pid)+$Arguments);$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});Require ($lines.Count-eq1) 'one CLI JSON record';$r.reply=$lines[0]|ConvertFrom-Json;return $r}
function Control([uint32]$Action){Live $current;[uint32]$pidValue=0;$h=[IntPtr]::new([long][uint64]$current.ready.hwnd);Require ([ViControllerNative]::GetWindowThreadProcessId($h,[ref]$pidValue)-eq$current.ready.thread-and$pidValue-eq$current.pid) 'fresh own control HWND/PID/thread';$script:token++;$mine=$token;Require ([ViControllerNative]::PostMessageW($h,0x80B1,[UIntPtr]::new([uint64]$Action),[IntPtr]::new([long]$mine))) 'own control accepted';$end=[Environment]::TickCount64+4000;do{$row=@(Rows $current.log|Where-Object {$_.event-eq'control'-and$_.token-eq$mine})|Select-Object -Last 1;if($row){Require $row.ok 'actual control ACK';return $row};if($current.process.HasExited){throw 'fixture exited during control'};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);throw 'control ACK unavailable'}
function NeutralSetup{$r=Invoke-ViControllerCli @('pad','none','40','--lx','0','--ly','0','--rx','0','--ry','0','--lt','0','--rt','0');Require ($r.exit-eq0-and$r.reply.ok) 'explicit virtual-neutral setup accepted'}
function StartFixture([string]$Exe,[string]$CliPath,[string]$Dll,[string]$Name){
 $log=Join-Path $run ($Name+'.jsonl');$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;[void]$si.ArgumentList.Add($log);$p=[Diagnostics.Process]::new();$p.StartInfo=$si;Require ($p.Start()) 'owned controller fixture started';$t=NewTracked $p $Exe;$script:current=$t;$t.cli=$CliPath;$t.log=$log;$t.observations=[Collections.Generic.List[object]]::new();ValidateTracked $t
 $end=[Environment]::TickCount64+5000;do{$ready=@(Rows $log|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
 $guard=@(Rows $log|Where-Object event -eq guards)|Select-Object -First 1;Require ($ready-and$ready.pid-eq$p.Id-and$ready.actual_offscreen_checked-and$guard.safe-and$guard.terminal_desktop_writers-eq19-and$guard.terminal_rumble_enable-and-not$guard.rumble_enable_probed-and-not$guard.hardware_reader_called-and-not$guard.native_printwindow_called) 'terminal/no-hardware/offscreen prerequisites';$t.ready=$ready
 [uint32]$fg=0;[void][ViControllerNative]::GetWindowThreadProcessId([ViControllerNative]::GetForegroundWindow(),[ref]$fg);Require ($fg-ne$t.pid) 'actual foreground stays outside fixture'
 $r=Invoke-ViControllerCli @('inject','--dll',$Dll,'--hooks-only');Require ($r.exit-eq0-and$r.reply.ok) 'chosen DLL actually injected';$j=@{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.hwnd);fakefocus=$false;blockfocus=$false;autoactivate=$false;isolate=$true}|ConvertTo-Json -Compress;$r=Invoke-ViControllerCli @('json',$j);Require ($r.exit-eq0-and$r.reply.ok) 'exact healthy target configured';$r=Invoke-ViControllerCli @('readiness');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.hook_ready-and$r.reply.window_ready-and$r.reply.queue_ready-and$r.reply.cursor_hooks_ready) 'actual full readiness';return $t
}
function StopCurrent{if($script:current){try{if(-not$current.process.HasExited){try{$null=Control 8;[void]$current.process.WaitForExit(500)}catch{}};Finish $script:current}finally{$script:current.process.Dispose();$script:current.Remove('process');$script:current=$null}}}

if(-not('ViR2Pipe'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r2_pipe.cs"}
function Native($Object,[int]$Budget=2500){Live $current;$request=$Object|ConvertTo-Json -Compress;$wire=[ViR2Pipe]::Query([uint32]$current.pid,$current.retained_handle,$current.created,$current.path,$request,$Budget);$record=@{request=$request;complete=$wire.Complete;server_pid=$wire.ServerPid;text=$wire.Text;error=$wire.Error};$calls.Add($record);Require ($wire.Complete-and$wire.ServerPid-eq$current.pid-and-not$wire.Error) 'bounded owned native pipe response';$record.reply=$wire.Text|ConvertFrom-Json;Require ($record.reply.ok-is[bool]) 'typed actual native result';return $record.reply}
function NStatus{return Native @{cmd='status'}}
function Off{$r=Native @{cmd='pad';buttons='off'};Require $r.ok 'explicit own pad off'}
function Model($Ack){$m=@(Rows $current.log|Where-Object {$_.event-eq'model'-and$_.token-eq$Ack.token})|Select-Object -Last 1;Require ($null-ne$m) 'matching own model ACK';return $m}
function MatchState([long]$CaseToken,[int]$Buttons,[int]$Code=0,[long]$After=0,[int]$Budget=700){$end=[Environment]::TickCount64+$Budget;do{$states=@(Rows $current.log|Where-Object {$_.event-eq'state'-and$_.case_token-eq$CaseToken-and$_.tick-gt$After-and$_.rc-eq$Code-and$_.buttons-eq$Buttons-and$_.lt-eq0-and$_.rt-eq0-and$_.lx-eq0-and$_.ly-eq0-and$_.rx-eq0-and$_.ry-eq0});$last=@(foreach($src in @('std','ex')){@($states|Where-Object source -eq $src)|Select-Object -Last 1});if($last.Count-eq2){return $last};if($current.process.HasExited){throw 'fixture exited awaiting actual XInput state'};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);return @()}
function StartA([int]$Ms){$r=Native @{cmd='pad';buttons='a';ms=$Ms;async=$true;lx=0;ly=0;rx=0;ry=0;lt=0;rt=0};Require ($r.ok-and$r.job_id-is[string]-and$r.job_id) 'existing native pad job actually admitted';return $r}
function Started([string]$Id,[int]$MaxActive=0){$end=[Environment]::TickCount64+500;do{$r=Native @{cmd='job-status';id=$Id};Require ($r.ok-and$r.job.job_id-eq$Id) 'same actual job ID';if($r.job.job_state-eq'running'-and$r.job.pad_updates-gt0-and-not$r.job.job_complete){if($MaxActive-and$r.job.active_ms-ge$MaxActive){return $null};return $r.job};if($r.job.job_complete){return $null};Start-Sleep -Milliseconds 1}while([Environment]::TickCount64-lt$end);return $null}
function Done([string]$Id){$end=[Environment]::TickCount64+1500;do{$r=Native @{cmd='job-status';id=$Id};Require ($r.ok-and$r.job.job_id-eq$Id) 'same completion ID';if($r.job.job_complete){return $r.job};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);throw 'finite native job did not complete'}
function Serial($Value){if($Value-isnot[string]-or$Value-notmatch'^\d+$'){return $false};[uint64]$parsedValue=0;return [uint64]::TryParse($Value,[ref]$parsedValue)}
function Held([string]$Buttons){return Native @{cmd='pad';buttons=$Buttons;hold=$true;lx=0;ly=0;rx=0;ry=0;lt=0;rt=0}}
try{
 foreach($f in @($PSCommandPath,"$PSScriptRoot\vi_r1_identity.ps1","$PSScriptRoot\vi_exited_cli_accounting.ps1","$PSScriptRoot\vi_r1_lifecycle.ps1","$PSScriptRoot\..\test\vi_r1_tail.cs","$PSScriptRoot\..\test\vi_r2_spies.h","$PSScriptRoot\..\test\vi_r2_pipe.cs","$PSScriptRoot\..\test\vi_controllercapture.cpp")){Pin $f}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry)
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$cli=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'}))).Path);$dll=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'}))).Path);Pin $cli;Pin $dll
   $mh=Join-Path $RepoRoot "build\$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'};$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$exe=Join-Path $folder 'vi_controllercapture.exe';$batch=Join-Path $folder 'build.cmd'
   @"
@echo off
call "$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_controllercapture.cpp" /Fo:"$folder\fixture.obj" /Fe:"$exe" /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'unchanged W4/WX fixture zero warnings';Pin $exe
   $current=StartFixture $exe $cli $dll ($arch+'.target');$entry.ready=$current.ready
   $initial=Control 0;$disconnected=@(MatchState 0 0 1167 $initial.tick);Require ($disconnected.Count-eq2) 'post-injection original disconnected lower route positive';$entry.initial=$disconnected
   NeutralSetup;$arm=Control 1;$case=@{name="$arch.fence";token=$arm.case_token;completed=$false};$cases.Add($case)
   Off;$marker=Control 0;Require (@(MatchState $arm.case_token 0 1167 $marker.tick).Count-eq2) 'pad off still uses original disconnected route'
   $pause=Control 2;$before=Model $pause;Require ($before.polling_paused-and$before.cold_remaining-eq2) 'actual polling-only pause ACK'
   $r=Native @{cmd='pad-arm';wait_ms=150};$case.no_poll=$r;$paused=Control 0;$after=Model $paused;Require ($after.polling_paused-and$after.native_reads-eq$before.native_reads) 'no native reads while UI pump remains responsive'
   Check ($r.ok-and$r.pad_readiness_version-eq1-and$r.arm_accepted-and$r.pad_armed-and$r.poll_observed-eq$false-and$null-eq$r.neutral_return_observed-and$null-eq$r.application_ready-and$r.device_arrival_announced-eq$false) "$arch no-poll arm never invents a return or game readiness"
   $resumeAck=Control 4;$resumed=Native @{cmd='pad-arm';wait_ms=500};$case.resumed=$resumed;$visible=@(MatchState $arm.case_token 0 0 $resumeAck.tick);$case.actual=$visible;$status=NStatus;$case.status=$status.virtual_pad
   Check ($resumed.ok-and$resumed.pad_readiness_version-eq1-and$resumed.poll_observed-and$resumed.neutral_return_observed-eq$true-and$visible.Count-eq2) "$arch real std/Ex neutral returns establish the fence"
   Check ((Serial $resumed.arm_publication)-and(Serial $status.virtual_pad.last_return_publication)-and$status.virtual_pad.last_return_publication-eq$resumed.arm_publication-and(Serial $status.virtual_pad.standard_returns)-and[uint64]$status.virtual_pad.standard_returns-gt0-and(Serial $status.virtual_pad.extended_returns)-and[uint64]$status.virtual_pad.extended_returns-gt0-and$status.virtual_pad.capabilities_virtualized-eq$false-and$status.virtual_pad.device_arrival_announced-eq$false) "$arch status serial names actual completed virtual returns only"
   $again=Native @{cmd='pad-arm';wait_ms=500};$case.again=$again;Check ($again.ok-and$again.state_changed-eq$false-and$again.poll_observed-and$again.arm_publication-eq$resumed.arm_publication) "$arch active empty arm is a no-op but requires a fresh return";$case.completed=$true

   NeutralSetup;$warm=Control 1;$producer=StartA 150;$warmActual=@(MatchState $warm.case_token 4096);Require ($warmActual.Count-eq2) 'existing150ms A positive before any cold source';$warmDone=Done $producer.job_id;Require ($warmDone.job_state-eq'completed') 'warm existing pad job completes';$warmZero=Control 0;Require (@(MatchState $warm.case_token 0 0 $warmZero.tick).Count-eq2) 'warm actual fresh neutral positive'
   $arm=Control 1;$case=@{name="$arch.cold_first_press";token=$arm.case_token;completed=$false};$cases.Add($case);$pause=Control 2;$before=Model $pause;Require ($before.polling_paused-and$before.cold_remaining-eq2) 'cold read source armed with polling paused'
   $producer=StartA 150;$started=Started $producer.job_id 50;Require ($null-ne$started) 'A actually published/running with active_ms below50 before resume';$case.started=$started;$case.producer=$producer
   $resume=Control 3;$actual=@(MatchState $arm.case_token 4096 0 0 1000);$case.actual=$actual;$done=Done $producer.job_id;$case.done=$done;$snapshot=Control 0;$after=Model $snapshot;$case.before=$before;$case.after=$after
   $events=@(Rows $current.log);$begins=@($events|Where-Object {$_.event-eq'cold_native_begin'-and$_.tick-ge$pause.tick-and$_.tick-le$snapshot.tick});$returns=@($events|Where-Object {$_.event-eq'cold_native_return'-and$_.tick-ge$pause.tick-and$_.tick-le$snapshot.tick});$case.lower_begin=$begins;$case.lower_return=$returns
   if($actual.Count-ne2){foreach($src in @(0,1)){$b=@($begins|Where-Object source -eq $src)|Select-Object -First 1;$e=@($returns|Where-Object source -eq $src)|Select-Object -First 1;Require ($b-and$e-and$b.delay_ms-eq300-and$e.begin-eq$b.tick-and$e.tick-$b.tick-ge280) 'missing pulse has actual std/Ex300ms lower begin/return witnesses'}}
   Check ($actual.Count-eq2) "$arch actual first150ms A survives controlled cold native source"
   Check ($after.native_reads-eq$before.native_reads-and$after.cold_remaining-eq2-and$begins.Count-eq0-and$returns.Count-eq0) "$arch active isolated std/Ex never enter the cold lower reader"
   Check ($done.job_state-eq'completed'-and@(MatchState $arm.case_token 0 0 $snapshot.tick).Count-eq2) "$arch finite cold-case A still releases to actual fresh neutral";$null=Control 4;$case.completed=$true

   foreach($heldButtons in @('a','none')){
    NeutralSetup;$arm=Control 1;$case=@{name="$arch.held_$heldButtons";token=$arm.case_token;completed=$false};$cases.Add($case);$hold=Held $heldButtons;Require ($hold.ok-and$hold.hold-and$hold.axes_set_mask-eq63) 'existing explicit held state published with zero axes mask';$buttonValue=if($heldButtons-eq'a'){4096}else{0};Require (@(MatchState $arm.case_token $buttonValue).Count-eq2) 'actual held-state positive'
    $before=NStatus;$r=Native @{cmd='pad-arm';wait_ms=0};$after=NStatus;$case.reply=$r;$case.before=$before.virtual_pad;$case.after=$after.virtual_pad;$afterAck=Control 0;$actual=@(MatchState $arm.case_token $buttonValue 0 $afterAck.tick);$case.actual=$actual
    Check (-not$r.ok-and$r.error-match'held'-and(Serial $before.virtual_pad.publication)-and$before.virtual_pad.publication-eq$after.virtual_pad.publication-and$actual.Count-eq2) "$arch arm refuses manual $heldButtons/mask and preserves actual state/publication";Off;$case.completed=$true
   }
   NeutralSetup;$arm=Control 1;$case=@{name="$arch.running_job";token=$arm.case_token;completed=$false};$cases.Add($case)
   # Pad jobs republish each tick. A static timeline publishes only its first
   # point here, so an unchanged serial actually tests that arm did not mutate it.
   $staticA=@(@{at_ms=0;buttons='a';lx=0;ly=0;rx=0;ry=0;lt=0;rt=0},@{at_ms=3000;buttons='a';lx=0;ly=0;rx=0;ry=0;lt=0;rt=0})|ConvertTo-Json -Compress
   $producer=Native @{cmd='pad-timeline';states_json=$staticA;ms=3000;pacing='time';async=$true};Require ($producer.ok-and$producer.job_id-is[string]-and$producer.job_id) 'existing static timeline actually admitted'
   $started=Started $producer.job_id;Require ($null-ne$started-and@(MatchState $arm.case_token 4096).Count-eq2) 'actual running pad job positive';$before=NStatus;$r=Native @{cmd='pad-arm';wait_ms=0};$after=NStatus;$job=Native @{cmd='job-status';id=$producer.job_id};$case.reply=$r;$case.job=$job.job;$case.before=$before.virtual_pad;$case.after=$after.virtual_pad
   $afterAck=Control 0;$actual=@(MatchState $arm.case_token 4096 0 $afterAck.tick);$case.actual=$actual
   Check (-not$r.ok-and$r.error-match'queued/running'-and(Serial $before.virtual_pad.publication)-and$before.virtual_pad.publication-eq$after.virtual_pad.publication-and$job.job.job_id-eq$producer.job_id-and$job.job.job_state-eq'running'-and-not$job.job.job_complete-and$actual.Count-eq2) "$arch arm preserves running job and pad publication"
   $cancel=Native @{cmd='job-cancel';id=$producer.job_id};Require $cancel.ok 'explicit exact job cancel cleanup';$afterCancel=Control 0;Require (@(MatchState $arm.case_token 0 0 $afterCancel.tick).Count-eq2) 'actual fresh neutral after explicit own cancel';$case.completed=$true
   $entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}finally{if($current){try{$null=Control 4;NeutralSetup}catch{$instrument=$false;Check $false "$arch controlled cleanup failed"}};StopCurrent}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2-and$cases.Count-eq10-and@($cases|Where-Object completed -eq $true).Count-eq10
}catch{$problem=$_.Exception.Message;Check $false "setup: $problem"}
@{schema=1;completed=$complete;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;cases=$cases;arches=$arches;calls=$calls;processes=$processes;pins=$pins;checks=$checks;error=$problem;failure_is_expected_inversion=$false;game_readiness_measured=$false;hardware_read=$false;scope='actual owned XInput std/Ex neutral fence, cold pulse and manual/running busy controls; queued admission source-reviewed/unmeasured; supersession/store-error covered by separate pure controls'}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $(@($checks|Where-Object pass -eq $true).Count) PASS $(@($checks|Where-Object pass -eq $false).Count) FAIL $run";if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
