# Native memory-only OCR engine + extracted actual command/geometry functions.
# No product/dist build, window, target process, OS input or foreground writer.
param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\ocr_numbers",[switch]$SkipReal)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$SourceRoot=(Resolve-Path $SourceRoot).Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$Out=(Resolve-Path $Out).Path;$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP
$calls=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new();$passed=0;$failed=0;$complete=$false
function Pin([string]$path){$pins.Add(@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash})}
# Reuse the reviewed fixture-only job implementation without executing its suite.
$jobFile=Join-Path $PSScriptRoot 'run_suitecontracttest.ps1';Pin $jobFile
if(-not('ViSuiteContractJobV1'-as[type])){$jobSource=Get-Content -LiteralPath $jobFile -Raw;$first=$jobSource.IndexOf('using System;');$last=$jobSource.IndexOf("'@}",$first);if($first-lt0-or$last-le$first){throw 'reviewed job helper extraction failed'};Add-Type $jobSource.Substring($first,$last-$first)}
$gateScript=Join-Path $Out 'owned_native_gate.ps1'
[IO.File]::WriteAllText($gateScript,@'
param([string]$GateName,[string]$Image,[string]$ArgumentFile)
$ErrorActionPreference='Stop';$gate=[Threading.EventWaitHandle]::OpenExisting($GateName)
try{if(-not$gate.WaitOne(10000)){throw 'owned native gate not released'}}finally{$gate.Dispose()}
$nativeArguments=@(Get-Content -LiteralPath $ArgumentFile -Raw|ConvertFrom-Json)
& $Image @nativeArguments
exit $LASTEXITCODE
'@)
function RunOwned([string]$image,[string[]]$arguments,[int]$budget,[string]$label){
 $name='Local\vinput_ocr_number_'+[guid]::NewGuid().ToString('N');$gate=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$name);$job=[ViSuiteContractJobV1]::new();$p=$null;$started=$false;$stdout=$null;$stderr=$null;$counts=$null;$errorText=$null;$timeout=$false;$watch=[Diagnostics.Stopwatch]::StartNew()
 $argFile=Join-Path $Out ($label+'.arguments.json');ConvertTo-Json -InputObject @($arguments)|Set-Content -LiteralPath $argFile
 try{
  $si=[Diagnostics.ProcessStartInfo]::new((Get-Command pwsh).Source);$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
  foreach($arg in @('-NoProfile','-File',$gateScript,'-GateName',$name,'-Image',$image,'-ArgumentFile',$argFile)){[void]$si.ArgumentList.Add($arg)}
  $p=[Diagnostics.Process]::new();$p.StartInfo=$si;[void]$p.Start();$started=$true;$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$job.Assign($p,$si.FileName);[void]$gate.Set();$timeout=-not$p.WaitForExit($budget)
 }catch{$errorText=$_.Exception.Message}
 finally{
  try{$counts=$job.Counts();if($counts[1]-gt0){$job.Terminate()};$end=[Environment]::TickCount64+5000;do{$counts=$job.Counts();if($counts[1]-eq0){break};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$end);if($counts[1]-ne0){throw 'owned native job did not become empty'};if($started-and-not$p.WaitForExit(2000)){throw 'original gated process did not exit'}}catch{$errorText=(@($errorText,$_.Exception.Message)|Where-Object{$_})-join'; '}
  if($started-and-not$p.HasExited){try{$p.Kill();[void]$p.WaitForExit(2000)}catch{$errorText=(@($errorText,$_.Exception.Message)|Where-Object{$_})-join'; '}}
  $drains=$true;foreach($task in @($stdout,$stderr)){if($task){try{if(-not$task.Wait(3000)){$drains=$false}}catch{$drains=$false}}}
  $r=@{label=$label;exit=$(if($started-and$p.HasExited){$p.ExitCode}else{$null});stdout=$(if($stdout-and$stdout.IsCompletedSuccessfully){$stdout.Result}else{''});stderr=$(if($stderr-and$stderr.IsCompletedSuccessfully){$stderr.Result}else{''});elapsed_ms=$watch.ElapsedMilliseconds;timeout=$timeout;budget_ms=$budget;job_active_final=$(if($counts){$counts[1]}else{$null});drains_completed=$drains;identity=@{pid=$job.RootPid;created_filetime=$job.CreatedFiletime;image=$job.Image};error=$errorText}
  $calls.Add($r);$r.stdout|Set-Content -LiteralPath (Join-Path $Out ($label+'.stdout.log'));$r.stderr|Set-Content -LiteralPath (Join-Path $Out ($label+'.stderr.log'));$job.Dispose();$gate.Dispose();if($p){$p.Dispose()}
 }
 if($timeout-or$errorText-or-not$drains){throw "owned $label failed or exceeded finite budget: $errorText"};return $r
}
$text=Get-Content -LiteralPath "$SourceRoot\src\vi_cli_ui.inl" -Raw
function Extract([string]$start,[string]$end){$i=$text.IndexOf($start);$j=$text.IndexOf($end,$i);if($i-lt0-or$j-le$i){throw "missing actual extraction $start"};$text.Substring($i,$j-$i)}
try{
 foreach($file in @($PSCommandPath,"$repo\test\vi_ocrnumbertest.cpp","$SourceRoot\src\vi_ocr_numbers.h","$SourceRoot\src\vi_ocr.h","$SourceRoot\src\vi_ocr_read.h","$SourceRoot\src\vi_cli_ui.inl")){Pin $file}
 foreach($file in Get-ChildItem -LiteralPath "$repo\test\data\ocr_numbers" -File){Pin $file.FullName}
 foreach($arch in @('x64','x86')){
  $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  @((Extract 'static std::string JU(' 'struct Frame {'),(Extract 'struct Frame {' 'static std::string ObservationImageFields('),(Extract 'static vi_text::Box ToClient(' 'static std::string FrameTimingFields('),(Extract 'static bool ClientRegion(' 'static bool CropClientFrame('),(Extract 'static std::string BoxJ(' '// <dist>'))|Set-Content -LiteralPath "$folder\actual_ocr_number_frame.inl"
  @((Extract 'static std::vector<double> ParseScales(' '// Windows OCR layout analysis'),(Extract 'static std::string LineJson(' '#include "vi_cli_text.inl"'))|Set-Content -LiteralPath "$folder\actual_ocr_number_command.inl"
  @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W4 /WX /D_CRT_SECURE_NO_WARNINGS /std:c++20 /I`"$folder`" /I`"$SourceRoot\src`" `"$repo\test\vi_ocrnumbertest.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content -LiteralPath "$folder\build.cmd" -Encoding ascii
  $build=RunOwned $env:ComSpec @('/d','/c',"$folder\build.cmd") 120000 ($arch+'.build');$build.stdout+$build.stderr|Set-Content -LiteralPath "$folder\build.log";if($build.exit-ne0){throw "$arch OCR number fixture compile failed: $($build.stdout) $($build.stderr)"};Pin "$folder\fixture.exe"
  $arguments=if($SkipReal){@()}else{@('--real',"$repo\test\data\ocr_numbers")};$r=RunOwned "$folder\fixture.exe" $arguments 120000 ($arch+'.test');$log=$r.stdout+$r.stderr;$log|Set-Content -LiteralPath "$folder\test.log";Write-Output $log
  if($log-notmatch'ocr numbers: (\d+) PASS / (\d+) FAIL'){throw "$arch missing complete footer"};$passed+=[int]$Matches[1];$failed+=[int]$Matches[2];$cases.Add(@{arch=$arch;exit=$r.exit;passed=[int]$Matches[1];failed=[int]$Matches[2]})
 }
 $complete=$true
}finally{
 @{schema=1;completed=$complete;passed=$passed;failed=$failed;cases=$cases;calls=$calls;pins=$pins;real_ocr=(-not$SkipReal);scope='actual extracted CmdOcr/ToClient/serializer + strict callback and memory-only WinRT OCR; finite original-handle gated job cleanup; no windows/focus/OS input/live game'}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $Out 'result.json')
}
if(-not$complete-or$failed-or@($cases|Where-Object exit -ne 0).Count){exit 1};exit 0
