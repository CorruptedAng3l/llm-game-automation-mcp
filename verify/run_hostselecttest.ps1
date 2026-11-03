param([string]$CliSourceDir="$PSScriptRoot\..\src",[string]$Out="$PSScriptRoot\..\build\test\hostselect",[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$CliSourceDir=(Resolve-Path $CliSourceDir).Path;$Out=[IO.Path]::GetFullPath($Out)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
if(-not $Out.StartsWith("$root\",[StringComparison]::OrdinalIgnoreCase)){throw 'Artifacts must stay within this worktree'}
New-Item -ItemType Directory -Force $Out,"$Out\temp"|Out-Null;$failed=0
foreach($arch in @('x64','x86')){
 $archDir="$Out\$arch";New-Item -ItemType Directory -Force $archDir|Out-Null;$vcvars=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$probe=if($arch -eq 'x64'){'vi_focusprobe.exe'}else{'vi_focusprobe32.exe'}
 $batch=@"
@echo off
setlocal
set "TEMP=$Out\temp"
set "TMP=$Out\temp"
call "$vcDir\$vcvars" >nul || exit /b 2
pushd "$archDir"
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /O2 /utf-8 /I"$CliSourceDir" "$root\test\vi_hostselecttest.cpp" /Fo:vi_hostselecttest.obj /Fe:vi_hostselecttest.exe || exit /b 2
"$archDir\vi_hostselecttest.exe" "$root\dist\$probe" "$archDir\owned"
exit /b %ERRORLEVEL%
"@
 [IO.File]::WriteAllText("$archDir\run.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$archDir\run.cmd" *> "$archDir\result.log";$code=$LASTEXITCODE;Get-Content "$archDir\result.log"
 if($code){++$failed;if(-not $KeepGoing){throw "$arch selector gate failed (exit $code)"}}
}
if($failed){exit 1};$global:LASTEXITCODE=0
