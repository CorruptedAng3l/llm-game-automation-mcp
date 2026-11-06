param([string]$Out="$PSScriptRoot\_out\radar_model")
$ErrorActionPreference='Stop'; $root=(Resolve-Path "$PSScriptRoot\..").Path
. "$PSScriptRoot\_vcvars.ps1"; $vcDir=Get-VcVarsDir
$Out=[IO.Path]::GetFullPath($Out);if(!$Out.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must remain in checkout'}
New-Item -ItemType Directory -Force $Out,"$Out/temp"|Out-Null;$env:TEMP="$Out/temp";$env:TMP=$env:TEMP;$rows=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;New-Item -ItemType Directory -Force $folder|Out-Null;$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /FI`"$root\src\vi_mh_rename.h`" `"$root\test\vi_radarmodeltest.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content "$folder/build.cmd"
 & cmd /d /c "$folder/build.cmd" *> "$folder/build.log";if($LASTEXITCODE){Get-Content "$folder/build.log";throw "$arch radar model compile failed"}
 & "$folder/fixture.exe" *> "$folder/test.log";$exit=$LASTEXITCODE;$text=Get-Content "$folder/test.log" -Raw;Write-Output $text
 if($text-notmatch'radar model: (\d+) PASS / (\d+) FAIL'){throw 'Missing complete radar model footer'};$rows+=@{arch=$arch;completed=$true;passed=[int]$Matches[1];failed=[int]$Matches[2];exit=$exit}
}
@{completed=$true;cases=$rows;scope='pure actual radar detector and decision policy; no target, OS input, capture API or worker'}|ConvertTo-Json -Depth 5|Set-Content "$Out/result.json"
if(@($rows|Where-Object{$_.failed-or$_.exit}).Count){exit 1};exit 0
