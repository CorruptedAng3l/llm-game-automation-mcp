param([string]$Out="$PSScriptRoot\..\build\test\legacylocktest",[string]$HeaderDir="$PSScriptRoot\..\src",[switch]$KeepGoing,[switch]$CliCheck)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$HeaderDir=(Resolve-Path $HeaderDir).Path
if(-not $Out.StartsWith("$root\",[StringComparison]::OrdinalIgnoreCase)){throw 'Artifacts must stay within this repository'}
New-Item -ItemType Directory -Force $Out,"$Out\temp"|Out-Null;$failed=0
foreach($arch in @('x64','x86')){
 $archDir="$Out\$arch";New-Item -ItemType Directory -Force $archDir|Out-Null
 $vcvars=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$fixture="$archDir\owned_$([guid]::NewGuid().ToString('N')).lock"
 $batch=@"
@echo off
setlocal
set "TEMP=$Out\temp"
set "TMP=$Out\temp"
call "$vcDir\$vcvars" >nul || exit /b 2
pushd "$archDir"
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /O2 /I"$HeaderDir" "$root\test\vi_legacylocktest.cpp" /Fo:vi_legacylocktest.obj /Fe:vi_legacylocktest.exe || exit /b 2
"$archDir\vi_legacylocktest.exe" "$fixture"
exit /b %ERRORLEVEL%
"@
 [IO.File]::WriteAllText("$archDir\run.cmd",$batch,[Text.Encoding]::ASCII)
 & cmd /d /c "$archDir\run.cmd" *> "$archDir\result.txt";$code=$LASTEXITCODE;Get-Content "$archDir\result.txt"
 if($code -or (Get-Content "$archDir\result.txt" -Raw) -match '(?im)warning [A-Z]+\d+|error [A-Z]+\d+') {++$failed;if(-not $KeepGoing){throw "$arch legacy diagnostics failed (exit $code)"}}
 if(Test-Path -LiteralPath $fixture){throw "Own fixture left behind: $fixture"}
 if($CliCheck){
  # A nonexistent executable makes process creation impossible even if lease refusal regresses.
  $cli=if($arch -eq 'x64'){"$root\dist\vinput.exe"}else{"$root\dist\vinput32.exe"}
  $missing="$archDir\never_exists_$([guid]::NewGuid().ToString('N')).exe";$body="{`"pid`":$PID,`"owner`":`"legacy-test-$arch`",`"purpose`":`"DO_NOT_PRINT_SECRET`"}"
  [IO.File]::WriteAllText($fixture,$body,[Text.UTF8Encoding]::new($false))
  try{
   $watch=[Diagnostics.Stopwatch]::StartNew();$reply=& $cli --json launch --exe $missing --lock $fixture --lock-max-wait 1 --lock-retry 1 2> "$archDir\cli.stderr.log";$code=$LASTEXITCODE;$watch.Stop();$reply|Set-Content "$archDir\cli.json"
   $json=$reply|ConvertFrom-Json;$stderr=Get-Content "$archDir\cli.stderr.log" -Raw
   if($code -ne 3 -or $json.ok -or $json.pid -or [IO.File]::ReadAllText($fixture) -cne $body -or $watch.ElapsedMilliseconds -gt 2500){throw 'CLI lock refusal changed ownership, attempted a launch, or exceeded its wait'}
   if($stderr -notmatch "holder UNVERIFIED raw_pid=$PID owner=`"legacy-test-$arch`"" -or $stderr -notmatch 'age_source=file_mtime file_age_ms=' -or $stderr -match 'DO_NOT_PRINT_SECRET'){throw 'CLI stderr omitted bounded unverified holder diagnostics'}
   "PASS $arch CLI launch refusal surfaces unverified holder/age and preserves exact bytes ($($watch.ElapsedMilliseconds)ms)"
  }finally{Remove-Item -LiteralPath $fixture}
 }
}
if($failed){exit 1};$global:LASTEXITCODE=0
