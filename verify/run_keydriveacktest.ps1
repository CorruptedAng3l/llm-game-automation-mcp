# Actual public sequence ACK parser fragment; finite real65-field wire control, no input.
param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$CliSource='',[string]$AckPath='',[string]$Out="$PSScriptRoot\_out\key_drive_ack")
$ErrorActionPreference='Stop';$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
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
 . "$RepoRoot/verify/_vcvars.ps1";$vcRoot=Get-VcVarsDir
 if(-not$CliSource){$CliSource=Join-Path $RepoRoot 'src/vi_cli_key_sequence.inl'};if(-not$AckPath){$AckPath=Join-Path $RepoRoot 'test/fixtures/sequence_ack65.json'}
 $CliSource=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliSource).Path);$AckPath=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $AckPath).Path)
 foreach($f in @($CliSource,$AckPath)){Require ($f.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) 'controlled source/receipt within repo'}
 $raw=[IO.File]::ReadAllText($CliSource);$start=$raw.LastIndexOf('vi_ready_json::Reader parsed{reply,0,{}};');Require ($start-ge0) 'exact production ACK reader marker';$end=$raw.IndexOf(' return {false,"{',$start);Require ($end-gt$start) 'exact production uncertain ACK boundary'
 [IO.File]::WriteAllText("$run/actual_ack_reader.inl",'static Reply ActualAck(const std::string& reply,bool received){'+$raw.Substring($start,$end-$start)+'return {false,reply};}')
 foreach($f in @($PSCommandPath,$CliSource,$AckPath,"$run/actual_ack_reader.inl","$RepoRoot/test/vi_keydrive_ack.cpp","$RepoRoot/src/vi_ready_json.h","$RepoRoot/verify/_vcvars.ps1")){Pin $f}
 foreach($arch in @('x64','x86')){$entry=@{arch=$arch;completed=$false};$arches.Add($entry);try{
  $folder=Join-Path $run $arch;[void][IO.Directory]::CreateDirectory($folder);$exe=Join-Path $folder 'ack_capacity.exe';$vc=Join-Path $vcRoot $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$batch=Join-Path $folder 'build.cmd'
  @('@echo off',('call "'+$vc+'" >nul || exit /b 2'),('set "TEMP='+$run+'\temp"'),('set "TMP='+$run+'\temp"'),('cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"'+$run+'" "'+$RepoRoot+'\test\vi_keydrive_ack.cpp" /Fo"'+$folder+'\model.obj" /Fe"'+$exe+'" /link /SUBSYSTEM:CONSOLE'),'exit /b %errorlevel%')|Set-Content -LiteralPath $batch -Encoding ascii
  $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0) 'ACK capacity control W4/WX clean build';Pin $exe
  $entry.runtime=RunOwned $exe @($AckPath) 5000;Require ($entry.runtime.stdout-match'key drive ACK capacity: (\d+) PASS / (\d+) FAIL') 'complete ACK capacity result line';$entry.passed=[int]$Matches[1];$entry.failed=[int]$Matches[2];$entry.completed=$true;Write-Host "$arch $($entry.passed) PASS $($entry.failed) FAIL"
 }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}}
 $complete=$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2
}catch{$problem=$_.Exception.Message;Check $false $problem}
$sourceStable=$true;foreach($pin in $pins){try{$pin.final_sha256=(Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash;$pin.unchanged=$pin.final_sha256-eq$pin.sha256}catch{$pin.unchanged=$false};if(-not$pin.unchanged){$sourceStable=$false}}
$passed=0;$failed=0;foreach($entry in $arches){$passed+=[int]$entry.passed;$failed+=[int]$entry.failed}
@{schema=1;completed=($complete-and$instrument-and$sourceStable);source_stable=$sourceStable;passed=$passed;failed=$failed;arches=$arches;calls=$calls;processes=$processes;pins=$pins;error=$problem;scope='exact extracted production sequence ACK parsing body against actual saved65-field3131byte native ACK; finite capacity/type/duplicate controls, no input'}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath "$run/result.json" -Encoding utf8NoBOM
Write-Host "KEYDRIVE_ACK $passed PASS $failed FAIL $run";if(-not$complete-or-not$instrument-or-not$sourceStable-or$failed){exit 1};exit 0
