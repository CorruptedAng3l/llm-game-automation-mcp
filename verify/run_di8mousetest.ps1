# Actual DI8 A/W mouse polling through production hooks, with terminal desktop writers.
param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$SourceRoot='',[string]$Out="$PSScriptRoot\_out\di8_mouse")
$ErrorActionPreference='Stop';$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$SourceRoot){$SourceRoot=$RepoRoot};$SourceRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $SourceRoot).Path)
if($SourceRoot-ne$RepoRoot-and-not$SourceRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'SourceRoot must stay within private RepoRoot'}
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay within private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$RepoRoot\verify\_vcvars.ps1"; $vcDir=Get-VcVarsDir
. "$RepoRoot\verify\vi_r1_identity.ps1"
. "$RepoRoot\verify\vi_exited_cli_accounting.ps1"
. "$RepoRoot\verify\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$RepoRoot\test\vi_r1_tail.cs"}
$jobSourcePath=Join-Path $PSScriptRoot 'run_suitecontracttest.ps1'
if(-not('ViSuiteContractJobV1'-as[type])){$jobText=Get-Content -LiteralPath $jobSourcePath -Raw;$begin=$jobText.IndexOf('using System;');$end=$jobText.IndexOf("'@}",$begin);if($begin-lt0-or$end-le$begin){throw 'reviewed owned-job helper unavailable'};Add-Type $jobText.Substring($begin,$end-$begin)}
$checks=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$processes=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new();$arches=[Collections.Generic.List[object]]::new()
$instrument=$true;$serial=0;$complete=$false;$problem=$null
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
function RunOwned([string]$Image,[string[]]$Arguments,[int]$Budget=10000,[bool]$AwaitAck=$false){
 $Image=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Image).Path)
 $script:serial++;$prefix=Join-Path $run ('call_'+$serial);$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Image;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardInput=$AwaitAck;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$run;$si.Environment['TEMP']="$run\temp";$si.Environment['TMP']="$run\temp";foreach($arg in $Arguments){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;$job=[ViSuiteContractJobV1]::new();$t=$null;$outTail=$null;$errTail=$null;$record=@{image=$Image;arguments=$Arguments;exit=$null;stdout='';stderr='';cleanup=$null};$calls.Add($record)
 try{Require ($p.Start()) 'owned compiler/CLI started';$t=NewTracked $p $Image;$job.Assign($p,$Image);$outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError);ValidateTracked $t;if($AwaitAck){$p.StandardInput.WriteLine('RUN');$p.StandardInput.Flush();$p.StandardInput.Close()};Require ($p.WaitForExit($Budget)) 'owned compiler/CLI bounded exit';$proof=Get-ViExitedCliAccounting $t.retained_handle ([uint32]$t.pid) $t.created $Image $t.cache;Require (Test-ViExitedCliAccounting $proof) 'readonly original-handle CLI exit accounting';$record.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);$record.exit_accounting=$proof
  $null=$outTail.Wait(500);$null=$errTail.Wait(500);$record.stdout=$outTail.Text();$record.stderr=$errTail.Text();$record.stdout_complete=$outTail.Complete;$record.stderr_complete=$errTail.Complete
  Require ($outTail.Complete-and$errTail.Complete-and-not$outTail.Error-and-not$errTail.Error-and$outTail.Characters-le65536-and$errTail.Characters-le65536) 'bounded complete separate stdout/stderr'
  return $record
 }finally{try{if($t){Finish $t;$record.cleanup=$t.cleanup};try{$counts=$job.Counts();if($counts[1]-gt0){$job.Terminate()};$until=[Environment]::TickCount64+5000;do{$counts=$job.Counts();if($counts[1]-eq0){break};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$until);$record.job_active_final=$counts[1];Require ($counts[1]-eq0) 'owned descendants empty after finite cleanup'}catch{$record.job_cleanup_error=$_.Exception.Message;$script:instrument=$false}}finally{
   if($outTail){$null=$outTail.Wait(500);$record.stdout=$outTail.Text()};if($errTail){$null=$errTail.Wait(500);$record.stderr=$errTail.Text()};[IO.File]::WriteAllText($prefix+'.stdout.log',$record.stdout);[IO.File]::WriteAllText($prefix+'.stderr.log',$record.stderr);if($t){$t.process.Dispose();$t.Remove('process')};$p.Dispose();$job.Dispose()
  }}
}

try{
 $source=Join-Path $RepoRoot 'test\vi_di8mousetest.cpp'
 foreach($portable in @($jobSourcePath,"$RepoRoot/verify/_vcvars.ps1","$RepoRoot/src/vi_mh_rename.h","$RepoRoot/third_party/minhook/include/MinHook.h","$RepoRoot/third_party/minhook/LICENSE.txt")){Pin $portable}
 foreach($vendor in (Get-ChildItem -LiteralPath "$RepoRoot/third_party/minhook/src" -File -Recurse)){Pin $vendor.FullName}
 foreach($p in @($PSCommandPath,$source,"$RepoRoot\test\vi_launch_terminal.h","$RepoRoot\test\vi_cursor_spies.h","$RepoRoot\src\vi_di_poll_hooks.inl","$RepoRoot\verify\vi_r1_identity.ps1","$RepoRoot\verify\vi_exited_cli_accounting.ps1","$RepoRoot\verify\vi_r1_lifecycle.ps1","$RepoRoot\test\vi_r1_tail.cs")){Pin $p}
 foreach($p in (Get-ChildItem -LiteralPath "$SourceRoot\src" -File)){Pin $p.FullName}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry)
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$exe=Join-Path $folder 'vi_di8mousetest.exe';$batch=Join-Path $folder 'build.cmd'
   $private=Join-Path $folder 'vimh';[void](New-Item -ItemType Directory -Force $private)
   foreach($name in @('vimh_buffer.obj','vimh_hook.obj','vimh_trampoline.obj','vimh_hde.obj')){$origin=Join-Path "$RepoRoot/build/$arch" $name;$copy=Join-Path $private $name;$hash=(Get-FileHash -LiteralPath $origin).Hash;Copy-Item -LiteralPath $origin -Destination $copy;Require ((Get-FileHash -LiteralPath $origin).Hash-eq$hash-and(Get-FileHash -LiteralPath $copy).Hash-eq$hash) 'hash-matched isolated vimh object';Pin $copy}
   $lower=Join-Path $folder 'lower_mh';[void](New-Item -ItemType Directory -Force $lower)
   # This ordinary MH_* instance is deliberately built WITHOUT the rename
   # header; the production fixture separately links only copied VIMH_* objs.
   $lowerSources=@(@{source='buffer.c';object='buffer.obj'},@{source='hook.c';object='hook.obj'},@{source='trampoline.c';object='trampoline.obj'},@{source=$(if($arch-eq'x64'){'hde/hde64.c'}else{'hde/hde32.c'});object='hde.obj'})
   $lowerCommands=@($lowerSources|ForEach-Object {'cl /nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /TC /c /I"'+$RepoRoot+'/third_party/minhook/include" "'+$RepoRoot+'/third_party/minhook/src/'+$_.source+'" /Fo:"'+$lower+'/'+$_.object+'" || exit /b 2'})
   $vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'})
   @"
@echo off
call "$vc" >nul || exit /b 2
$($lowerCommands -join "`r`n")
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /FI"$RepoRoot\src\vi_mh_rename.h" /I"$SourceRoot\src" /I"$RepoRoot\src" /I"$RepoRoot\third_party\minhook\include" "$source" "$folder\vimh\vimh_buffer.obj" "$folder\vimh\vimh_hook.obj" "$folder\vimh\vimh_trampoline.obj" "$folder\vimh\vimh_hde.obj" /Fo:"$folder\fixture.obj" /Fe:"$exe" /link /SUBSYSTEM:CONSOLE "$lower\buffer.obj" "$lower\hook.obj" "$lower\trampoline.obj" "$lower\hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'native DI8 fixture zero warnings';foreach($item in $lowerSources){Pin (Join-Path $lower $item.object)};Pin $exe
   $entry.result=RunOwned $exe @('--await-start') 55000 $true
   Require ($entry.result.stdout-match'DI8 mouse: (\d+) PASS / (\d+) FAIL') 'complete finite callback result line';$entry.passed=[int]$Matches[1];$entry.failed=[int]$Matches[2]
   Check ($entry.result.exit-eq0-and$entry.failed-eq0) "$arch native DI8 mouse polling result";$entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2
}catch{$problem=$_.Exception.Message;Check $false "setup: $problem"}
$sourceStable=$true;foreach($pin in $pins){try{$pin.final_sha256=(Get-FileHash -LiteralPath $pin.path).Hash;$pin.unchanged=$pin.final_sha256-eq$pin.sha256}catch{$pin.unchanged=$false;$pin.final_error=$_.Exception.Message};if(-not$pin.unchanged){$sourceStable=$false}}
if(-not$sourceStable){$complete=$false;Check $false 'all pinned source and isolated link inputs remain unchanged'}
$passed=0;$failed=0;foreach($a in $arches){$passed+=[int]$a.passed;$failed+=[int]$a.failed}
@{schema=1;completed=$complete;source_stable=$sourceStable;passed=$passed;failed=$failed;error=$problem;arches=$arches;checks=$checks;calls=$calls;processes=$processes;pins=$pins;lower_hook_source='canonical vendored C compiled per architecture without rename';actual_di8_com_test=$true;production_adapter_executed=$true;failure_is_expected_inversion=$false}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $passed PASS $failed FAIL $run";if(-not$complete-or$failed-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
