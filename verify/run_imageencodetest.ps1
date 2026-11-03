param([string]$Out="$PSScriptRoot\..\build\test\imageencodetest")
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force -Path $Out,"$repo\build\temp"|Out-Null
foreach($arch in @('x64','x86')){
 $dir="$Out\$arch";New-Item -ItemType Directory -Force -Path $dir|Out-Null;$vcvars=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
setlocal
set "TEMP=$repo\build\temp"
set "TMP=$repo\build\temp"
call "$vcDir\$vcvars" >nul || exit /b 2
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /O2 "$repo\test\vi_imageencodetest.cpp" /Fo:"$dir\fixture.obj" /Fe:"$dir\fixture.exe" || exit /b 2
"$dir\fixture.exe" "$dir"
exit /b %ERRORLEVEL%
"@
 [IO.File]::WriteAllText("$dir\run.cmd",$batch,[Text.Encoding]::ASCII)
 & cmd /d /c "$dir\run.cmd" *> "$dir\result.log";$code=$LASTEXITCODE;Get-Content "$dir\result.log"
 if($code -or (Get-Content "$dir\result.log" -Raw)-match '(?im)warning [A-Z]+\d+|error [A-Z]+\d+'){throw "$arch WIC output tests failed"}
}
$global:LASTEXITCODE=0
