# Actual inert original-process handles only. No GUI/device/vinput/OS input.
param([string]$Out="$PSScriptRoot/_out/early_cleanup")
$ErrorActionPreference='Stop';$repo=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent));$output=[IO.Path]::GetFullPath($Out)
if(-not$output.StartsWith($repo.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay in repo'}
[void](New-Item -ItemType Directory -Force $output,"$output/temp");$env:TEMP="$output/temp";$env:TMP=$env:TEMP
. "$PSScriptRoot/vi_early_cleanup.ps1"
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$processes=[Collections.Generic.List[object]]::new();$gates=[Collections.Generic.List[object]]::new();$complete=$false;$problem=$null
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name"}
$actor=Join-Path $output 'inert_owned.ps1';[IO.File]::WriteAllText($actor,@'
param([string]$GateName)
$event=[Threading.EventWaitHandle]::OpenExisting($GateName)
try{if(-not$event.WaitOne(10000)){exit 111}}finally{$event.Dispose()}
exit 0
'@)
function SpawnInert{
 $name='Local\vinput_early_cleanup_'+[guid]::NewGuid().ToString('N');$gate=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$name);$gates.Add($gate)
 $image=(Get-Command pwsh).Source;$si=[Diagnostics.ProcessStartInfo]::new($image);$si.UseShellExecute=$false;$si.CreateNoWindow=$true;foreach($arg in @('-NoProfile','-File',$actor,'-GateName',$name)){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;if(-not$p.Start()){throw 'inert actor startup failed'};$processes.Add($p);$r=New-ViEarlyOwnedRecord $p $image $p.StartTime.ToUniversalTime().Ticks;$records.Add($r);return @{record=$r;gate=$gate}
}
try{
 $first=SpawnInert;$good=$first.record;Check $good.startup_verified 'actual original creator handle proves kernel image/PID/creation'
 $bad=New-ViEarlyOwnedRecord $good.process (Join-Path $output 'wrong.exe') $good.created;$refused=Stop-ViEarlyOwnedRecord $bad;Check (-not$refused.completed-and-not$refused.termination_requested-and-not$good.process.WaitForExit(0)) 'wrong expected image refuses before any termination'
 $bad=New-ViEarlyOwnedRecord $good.process $good.path ($good.created+1);$refused=Stop-ViEarlyOwnedRecord $bad;Check (-not$refused.completed-and-not$refused.termination_requested-and-not$good.process.WaitForExit(0)) 'wrong expected creation refuses before any termination'
 $bad=$good.Clone();$bad.pid=$good.pid+1;$refused=Stop-ViEarlyOwnedRecord $bad;Check (-not$refused.completed-and-not$refused.termination_requested-and-not$good.process.WaitForExit(0)) 'wrong expected PID never gains termination authority'
 $stopped=Stop-ViEarlyOwnedRecord $good;Check ($stopped.completed-and$stopped.termination_requested-and$stopped.wait_ms-eq5000-and$stopped.original_handle_signaled-and$stopped.exit_accounting.accounted) 'owned termination waits on exact original object and accounts native signaled exit'
 $again=Stop-ViEarlyOwnedRecord $good;Check ([object]::ReferenceEquals($again,$stopped)) 'completed cleanup returns cached result without another termination'
 $second=SpawnInert;$natural=$second.record;[void]$second.gate.Set();Check ($natural.process.WaitForExit(5000)) 'natural owned exit signals original object without PID discovery'
 $stopped=Stop-ViEarlyOwnedRecord $natural;Check ($stopped.completed-and-not$stopped.termination_requested-and$stopped.exit_accounting.accounted) 'already exited original is accounted read-only without termination'
 $bad=$natural.Clone();$bad.cleanup=$null;$bad.cache=$null;$refused=Stop-ViEarlyOwnedRecord $bad;Check (-not$refused.completed-and-not$refused.termination_requested) 'missing strict startup cache never becomes exited action authority'
 $complete=$true
}catch{$problem=$_.Exception.Message;Check $false "infrastructure: $problem"}
finally{foreach($r in $records){if(-not$r.cleanup-or-not$r.cleanup.completed){$null=Stop-ViEarlyOwnedRecord $r}};foreach($gate in $gates){$gate.Dispose()};foreach($p in $processes){$p.Dispose()}}
$pins=@();foreach($path in @($PSCommandPath,"$PSScriptRoot/vi_early_cleanup.ps1","$PSScriptRoot/vi_r1_identity.ps1","$PSScriptRoot/vi_exited_cli_accounting.ps1")){$pins+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}}
@{completed=$complete;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;checks=$checks;actors=@($records|ForEach-Object {@{pid=$_.pid;retained_handle=$_.retained_handle;path=$_.path;created=$_.created;startup_verified=$_.startup_verified;cleanup=$_.cleanup}});pins=$pins;error=$problem;scope='owned inert processes only; no legacy GUI/device/vinput lane was executed'}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath "$output/result.json"
if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
