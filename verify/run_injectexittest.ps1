param([string]$Out="$PSScriptRoot\..\build\test\injectexittest",[string]$CliDir="$PSScriptRoot\..\dist",[switch]$ExpectRed)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$repo=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out);$CliDir=(Resolve-Path -LiteralPath $CliDir).Path
New-Item -ItemType Directory -Force -Path $Out|Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Check([bool]$Condition,[string]$Name){$checks.Add([pscustomobject]@{pass=$Condition;name=$Name});if($Condition){"PASS $Name"}else{"FAIL $Name"}}
function Run-OwnCli([string]$Cli,[string[]]$Arguments,[string]$Tag,[int]$BudgetMs=4000){
 $info=[Diagnostics.ProcessStartInfo]::new($Cli);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info;$clock=[Diagnostics.Stopwatch]::StartNew()
 try{
  if(-not $process.Start()){throw 'Could not start own CLI'}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $finished=$process.WaitForExit($BudgetMs);if(-not $finished){$process.Kill();$process.WaitForExit()}
  $clock.Stop();$outText=$stdout.GetAwaiter().GetResult();$errText=$stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText("$Out\$Tag.stdout.json",$outText);[IO.File]::WriteAllText("$Out\$Tag.stderr.log",$errText)
  $json=$null;if($outText.Trim()){$json=$outText|ConvertFrom-Json}
  [pscustomobject]@{finished=$finished;exit=$process.ExitCode;elapsed_ms=$clock.ElapsedMilliseconds;json=$json;stdout=$outText}
 }finally{$process.Dispose()}
}
foreach($arch in @('x64','x86')){
 $archDir=Join-Path $Out $arch;New-Item -ItemType Directory -Force -Path $archDir|Out-Null
 New-Item -ItemType Directory -Force -Path (Join-Path $archDir 'temp')|Out-Null
 $vcvars=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
setlocal
set "TEMP=$archDir\temp"
set "TMP=$archDir\temp"
call "$vcDir\$vcvars" >nul || exit /b 2
pushd "$archDir"
cl /nologo /EHsc /W4 /WX /MT /O2 "$repo\test\vi_injectexit.cpp" /Fo:target.obj /Fe:target.exe user32.lib || exit /b 2
cl /nologo /EHsc /W4 /WX /MT /O2 /DBUILD_EXIT_DLL /LD "$repo\test\vi_injectexit.cpp" /Fo:exit.obj /Fe:exit73.dll || exit /b 2
cl /nologo /EHsc /W4 /WX /MT /O2 /DBUILD_EXIT_DLL /DPIPE_EXIT /LD "$repo\test\vi_injectexit.cpp" /Fo:pipeexit.obj /Fe:pipeexit73.dll || exit /b 2
cl /nologo /EHsc /W4 /WX /MT /O2 /DBUILD_EXIT_DLL /DPIPE_EXIT /DPIPE_STAYS_ALIVE /LD "$repo\test\vi_injectexit.cpp" /Fo:pipealive.obj /Fe:pipealive.dll || exit /b 2
exit /b 0
"@
 [IO.File]::WriteAllText("$archDir\build.cmd",$batch,[Text.Encoding]::ASCII)
 & cmd /d /c "$archDir\build.cmd" *> "$archDir\build.log"
 if($LASTEXITCODE -or (Get-Content "$archDir\build.log" -Raw) -match '(?im)warning [A-Z]+\d+|error [A-Z]+\d+'){throw 'Own exit fixture build failed'}
 $cli=Join-Path $CliDir $(if($arch -eq 'x64'){'vinput.exe'}else{'vinput32.exe'})
 $target=$null
 foreach($mode in @('exit73','pipeexit73')){
 try{
  $target=Start-Process -FilePath "$archDir\target.exe" -WindowStyle Hidden -PassThru
  $reply=Run-OwnCli $cli @('--json','inject','--pid',"$($target.Id)",'--dll',"$archDir\$mode.dll") "$arch-$mode"
  Check ($target.WaitForExit(1000) -and $target.ExitCode -eq 73) "$arch $mode owned DLL exits exact target73"
  Check ($reply.finished -and $reply.exit -eq 2 -and $reply.elapsed_ms -lt 2500 -and $reply.json.target_exited -and $reply.json.pid -eq $target.Id -and $reply.json.exit_code -eq 73 -and $reply.json.error -match 'during inject') "$arch $mode injection reports exact exited target/exit73"
  if(-not $ExpectRed){Check ($reply.json.stage -eq $(if($mode -eq 'exit73'){'load-library'}else{'pipe-wait'})) "$arch $mode reports the actual injection stage"}
 }finally{if($target -and -not $target.HasExited){Stop-Process -Id $target.Id -Force -Confirm:$false}}
 }
 if(-not $ExpectRed){
  try{
   $target=Start-Process -FilePath "$archDir\target.exe" -WindowStyle Hidden -PassThru
   $reply=Run-OwnCli $cli @('--json','inject','--pid',"$($target.Id)",'--dll',"$archDir\pipealive.dll") "$arch-live-pipe-failure"
   Check ($reply.finished -and $reply.exit -eq 2 -and -not $target.HasExited -and -not $reply.json.target_exited -and $reply.json.error -eq 'dll loaded but pipe never came up') "$arch live pipe failure is not misreported as target exit"
  }finally{if($target -and -not $target.HasExited){Stop-Process -Id $target.Id -Force -Confirm:$false}}
  try{
   $target=Start-Process -FilePath "$archDir\target.exe" -WindowStyle Hidden -PassThru
   $reply=Run-OwnCli $cli @('--json','inject','--pid',"$($target.Id)",'--hooks-only') "$arch-live" -BudgetMs 8000
   Check ($reply.finished -and $reply.exit -eq 0 -and $reply.json.ok -and $reply.json.readiness.hook_ready -and $reply.json.readiness.scope -eq 'hooks' -and -not $target.HasExited -and -not $reply.json.target_exited) "$arch explicit hooks-only headless injection remains successful"
  }finally{if($target -and -not $target.HasExited){Stop-Process -Id $target.Id -Force -Confirm:$false}}
  $ledger="$archDir\launch_$([guid]::NewGuid().ToString('N')).pid";$began=[DateTime]::UtcNow
  try{
   $reply=Run-OwnCli $cli @('--json','launch','--exe',"$archDir\target.exe",'--args',"window `"$ledger`"",'--dll',"$archDir\exit73.dll",'--fg-guard','0','--settle','0','--timeout','3') "$arch-launch-exit"
   Check ($reply.finished -and $reply.exit -ne 0 -and $reply.json.target_exited -and $reply.json.exit_code -eq 73 -and $reply.json.error -match 'during inject') "$arch launch propagates exact injection exit diagnostic without relaunch"
  }finally{
   if(Test-Path -LiteralPath $ledger){
    $ownedPid=[int]([IO.File]::ReadAllText($ledger));$child=Get-Process -Id $ownedPid -ErrorAction SilentlyContinue
    if($child -and $child.Path -eq "$archDir\target.exe" -and $child.StartTime.ToUniversalTime() -ge $began){Stop-Process -Id $ownedPid -Force -Confirm:$false}
   }
  }
 }
}
$failed=@($checks|Where-Object{-not $_.pass}).Count
[pscustomobject]@{ok=($failed -eq 0);checks=$checks;failed=$failed;expected_red=[bool]$ExpectRed}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$Out\result.json"
if($ExpectRed){if($failed -lt 2){throw 'Baseline unexpectedly diagnosed exited targets'};'EXPECTED RED: target exits73, old CLI waits for absent pipe';$global:LASTEXITCODE=0}
elseif($failed){throw "$failed injection-exit controls failed"}
else{$global:LASTEXITCODE=0}
