# Native in-memory read counters, actual receipt serializer and GAKS handler.
# No product/dist build, window, target process, OS input or foreground writer.
param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\native_receipts")
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
 $name='Local\vinput_native_receipts_'+[guid]::NewGuid().ToString('N');$gate=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$name);$job=[ViSuiteContractJobV1]::new();$p=$null;$started=$false;$stdout=$null;$stderr=$null;$counts=$null;$errorText=$null;$timeout=$false;$watch=[Diagnostics.Stopwatch]::StartNew()
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
. "$PSScriptRoot\_vcvars.ps1"
$vcDir=Get-VcVarsDir
$core=Get-Content -LiteralPath "$repo\src\vinput_core.cpp" -Raw
function Extract([string]$start,[string]$end){$i=$core.IndexOf($start);if($i-lt0){throw "missing production extraction $start"};$j=$core.IndexOf($end,$i);if($j-le$i){throw "missing production extraction end $end"};return $core.Substring($i,$j-$i)}
try{
 foreach($file in @($PSCommandPath,"$PSScriptRoot\_vcvars.ps1","$repo\src\vinput_core.cpp","$repo\src\vi_trace.h","$SourceRoot\src\vi_input_read_observation.h","$SourceRoot\src\vi_job_reads.inl","$repo\test\vi_receipttest.cpp")){Pin $file}
 foreach($arch in @('x64','x86')){
  $folder=Join-Path $Out $arch;New-Item -ItemType Directory -Force $folder|Out-Null;$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$mh=Join-Path $repo "build\$arch"
  Extract 'struct JOut {' '#include "vi_capture_diagnostics_json.inl"'|Set-Content -LiteralPath "$folder\actual_receipt_jout.inl"
  Extract 'SHORT WINAPI h_GetAsyncKeyState(int vk)' 'SHORT WINAPI h_GetKeyState(int vk)'|Set-Content -LiteralPath "$folder\actual_receipt_gaks.inl"
  $isolated=Join-Path $folder 'vimh';New-Item -ItemType Directory -Force $isolated|Out-Null
  foreach($name in @('vimh_buffer.obj','vimh_hook.obj','vimh_trampoline.obj','vimh_hde.obj')){$origin=Join-Path $mh $name;$copy=Join-Path $isolated $name;Pin $origin;Copy-Item -LiteralPath $origin -Destination $copy;Pin $copy;if((Get-FileHash -LiteralPath $origin).Hash-ne(Get-FileHash -LiteralPath $copy).Hash){throw 'isolated object copy hash mismatch'}}
  @('@echo off',"call `"$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /FI`"$repo\src\vi_mh_rename.h`" /I`"$folder`" /I`"$SourceRoot\src`" /I`"$repo\src`" `"$repo\test\vi_receipttest.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO `"$isolated\vimh_buffer.obj`" `"$isolated\vimh_hook.obj`" `"$isolated\vimh_trampoline.obj`" `"$isolated\vimh_hde.obj`"")|Set-Content -LiteralPath "$folder\build.cmd"
  $build=RunOwned $env:ComSpec @('/d','/c',"$folder\build.cmd") 120000 ($arch+'.build');if($build.exit){throw "$arch compile failed: $($build.stdout) $($build.stderr)"};Pin "$folder\fixture.exe"
  $r=RunOwned "$folder\fixture.exe" @() 15000 ($arch+'.test');$r.stdout|Set-Content -LiteralPath "$folder\test.log"
  if($r.stdout-notmatch'native input receipts: (\d+) PASS / (\d+) FAIL'){throw "$arch missing complete receipt control footer"};$passed+=[int]$Matches[1];$failed+=[int]$Matches[2];$cases.Add(@{arch=$arch;exit=$r.exit;passed=[int]$Matches[1];failed=[int]$Matches[2]});Write-Output "$arch $($Matches[1]) PASS / $($Matches[2]) FAIL"
 }
 $complete=$true
}finally{
 @{schema=1;completed=$complete;passed=$passed;failed=$failed;cases=$cases;calls=$calls;pins=$pins;scope='actual serializer inl + real ChannelTrace counters + extracted actual GetAsyncKeyState handler with finite in-memory endpoints; no Init/window/device/worker/OS input; original-handle gated Job Object ownership'}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $Out 'result.json')
}
if(-not$complete-or$failed-or@($cases|Where-Object exit -ne 0).Count){exit 1};exit 0
