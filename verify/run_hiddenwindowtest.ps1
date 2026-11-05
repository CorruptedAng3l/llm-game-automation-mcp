# Root-serialized real own hidden HWNDs + synthetic Present metadata. No GPU.
param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\hidden_window_native",[switch]$BuildOnly)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$SourceRoot=$RepoRoot
$ErrorActionPreference='Stop';$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$SourceRoot){$SourceRoot=$RepoRoot};$SourceRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $SourceRoot).Path)
if($SourceRoot-ne$RepoRoot-and-not$SourceRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'SourceRoot must stay within private RepoRoot'}
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay within private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$RepoRoot\verify\vi_r1_identity.ps1"
. "$RepoRoot\verify\vi_exited_cli_accounting.ps1"
. "$RepoRoot\verify\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$RepoRoot\test\vi_r1_tail.cs"}
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
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;$t=$null;$outTail=$null;$errTail=$null;$record=@{image=$Image;arguments=$Arguments;exit=$null;stdout='';stderr='';cleanup=$null};$calls.Add($record)
 try{Require ($p.Start()) 'owned compiler/CLI started';$t=NewTracked $p $Image;$outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError);ValidateTracked $t;if($AwaitAck){$p.StandardInput.WriteLine('RUN');$p.StandardInput.Flush();$p.StandardInput.Close()};Require ($p.WaitForExit($Budget)) 'owned compiler/CLI bounded exit';$proof=Get-ViExitedCliAccounting $t.retained_handle ([uint32]$t.pid) $t.created $Image $t.cache;Require (Test-ViExitedCliAccounting $proof) 'readonly original-handle CLI exit accounting';$record.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);$record.exit_accounting=$proof
  $null=$outTail.Wait(500);$null=$errTail.Wait(500);$record.stdout=$outTail.Text();$record.stderr=$errTail.Text();$record.stdout_complete=$outTail.Complete;$record.stderr_complete=$errTail.Complete
  Require ($outTail.Complete-and$errTail.Complete-and-not$outTail.Error-and-not$errTail.Error-and$outTail.Characters-le65536-and$errTail.Characters-le65536) 'bounded complete separate stdout/stderr'
  return $record
 }finally{try{if($t){Finish $t;$record.cleanup=$t.cleanup}}finally{
   if($outTail){$null=$outTail.Wait(500);$record.stdout=$outTail.Text()};if($errTail){$null=$errTail.Wait(500);$record.stderr=$errTail.Text()};[IO.File]::WriteAllText($prefix+'.stdout.log',$record.stdout);[IO.File]::WriteAllText($prefix+'.stderr.log',$record.stderr);if($t){$t.process.Dispose();$t.Remove('process')};$p.Dispose()
  }}
}


try{
 $source=Join-Path $RepoRoot 'test\vi_hiddenwindow_native.cpp'
 foreach($p in @($PSCommandPath,$source,"$RepoRoot\test\vi_r2_spies.h","$RepoRoot\test\vi_d3d9_fixture_geometry.h","$RepoRoot\test\vi_launch_ack.h","$RepoRoot\verify\vi_r1_identity.ps1","$RepoRoot\verify\vi_exited_cli_accounting.ps1","$RepoRoot\verify\vi_r1_lifecycle.ps1","$RepoRoot\test\vi_r1_tail.cs")){Pin $p}
 foreach($p in (Get-ChildItem -LiteralPath "$RepoRoot\src" -File)){Pin $p.FullName}
 $mh="$RepoRoot\third_party\minhook"
 foreach($p in @("$mh\include\MinHook.h","$mh\src\buffer.c","$mh\src\buffer.h","$mh\src\hook.c","$mh\src\trampoline.c","$mh\src\trampoline.h","$mh\src\hde\hde64.c","$mh\src\hde\hde64.h","$mh\src\hde\table64.h","$mh\src\hde\hde32.c","$mh\src\hde\hde32.h","$mh\src\hde\table32.h","$mh\src\hde\pstdint.h")){Pin $p}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry)
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$exe=Join-Path $folder 'vi_hiddenwindow_native.exe';$batch=Join-Path $folder 'build.cmd'
   $vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'})
   $commands=[Collections.Generic.List[string]]::new();$commands.Add('@echo off');$commands.Add(('call "'+$vc+'" >nul || exit /b 2'))
   $objects=[Collections.Generic.List[string]]::new()
   foreach($instance in @('host_mh','vimh')){
    foreach($unit in @('buffer','hook','trampoline','hde')){
     $file=if($unit-eq'hde'){Join-Path $mh $(if($arch-eq'x64'){'src\hde\hde64.c'}else{'src\hde\hde32.c'})}else{Join-Path $mh ('src\'+$unit+'.c')}
     $obj=Join-Path $folder ($instance+'_'+$unit+'.obj');$objects.Add('"'+$obj+'"');$rename=if($instance-eq'vimh'){' /FI"'+$RepoRoot+'\src\vi_mh_rename.h"'}else{''}
     $commands.Add(('cl /nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /I"'+$mh+'\include"'+$rename+' /c "'+$file+'" /Fo:"'+$obj+'" || exit /b 2'))
    }
   }
   $commands.Add(('cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /I"'+$RepoRoot+'\src" /I"'+$mh+'\include" "'+$source+'" '+($objects-join' ')+' /Fo:"'+$folder+'\fixture.obj" /Fe:"'+$exe+'" /link /SUBSYSTEM:CONSOLE || exit /b 2'));$commands.Add('exit /b 0')
   [IO.File]::WriteAllLines($batch,$commands,[Text.Encoding]::ASCII)
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'native hidden fixture and two separate MinHook instances zero warnings';Pin $exe
   $entry.passed=0;$entry.failed=0
   if($BuildOnly){Check $true "$arch guarded fixture compile only; no GUI executed";$entry.passed=1}else{
    $record=RunOwned $exe @('--await-start') 30000 $true;$entry.result=$record
    Require ($record.stdout-match'HIDDEN_WINDOW_NATIVE_RESULT (\d+) (\d+)') 'complete owned hidden result line'
    $entry.passed=[int]$Matches[1];$entry.failed=[int]$Matches[2]
    Check ($record.exit-eq0-and$entry.failed-eq0) "$arch guarded hidden-window/presentation/show contract"
   };$entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2
}catch{$problem=$_.Exception.Message;Check $false "setup: $problem"}
$passed=0;$failed=0;foreach($a in $arches){$passed+=[int]$a.passed;$failed+=[int]$a.failed}
@{schema=1;completed=$complete;passed=$passed;failed=$failed;error=$problem;arches=$arches;checks=$checks;calls=$calls;processes=$processes;pins=$pins;build_only=[bool]$BuildOnly;actual_owned_hwnd_executed=(-not$BuildOnly);synthetic_present_provider=$true;actual_gpu_test=$false;production_adapter_executed=(-not$BuildOnly);failure_is_expected_inversion=$false;own_show_exception='only explicitly armed exact retained own HWND/UI-thread/private lifetime and fully offscreen pre/post rect; original ShowWindow SW_SHOWNA only; CBT rejects activation; all other desktop writers terminal through cleanup'}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $passed PASS $failed FAIL $run";if(-not$complete-or$failed-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
