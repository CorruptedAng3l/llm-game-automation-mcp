param([string]$Out="$PSScriptRoot\_out\motion_metrics")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out)
[void](New-Item -ItemType Directory -Force $Out,"$Out\temp");$env:TEMP="$Out\temp";$env:TMP=$env:TEMP;$cases=@()
foreach($arch in @('x64','x86')){
 $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$dir=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $dir)
 @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I`"$root\src`" `"$root\test\vi_motiontest.cpp`" /Fo:`"$dir\fixture.obj`" /Fe:`"$dir\fixture.exe`" || exit /b 2","`"$dir\fixture.exe`"")|Set-Content "$dir\run.cmd" -Encoding ascii
 & cmd /d /c "$dir\run.cmd" *> "$dir\test.log";$rc=$LASTEXITCODE;$text=Get-Content "$dir\test.log" -Raw;Write-Output $text
 if($text-notmatch'motion metrics: (\d+) PASS / (\d+) FAIL'){throw "missing complete $arch result"}
 $cases+=@{arch=$arch;exit=$rc;passed=[int]$Matches[1];failed=[int]$Matches[2]}
}
@{completed=$true;cases=$cases;scope='actual RGB difference and area-average code on known pixels; no renderer'}|ConvertTo-Json -Depth 4|Set-Content "$Out\result.json"
if(@($cases|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
