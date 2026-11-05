param([string]$Out="$PSScriptRoot\_out\capture_history")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out)
if(-not$Out.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside checkout'}
[void](New-Item -ItemType Directory -Force -Path @($Out,"$Out/temp"));$env:TEMP="$Out/temp";$env:TMP=$env:TEMP;$rows=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root/test/vi_capturehistory.cpp" /Fo:"$folder/fixture.obj" /Fe:"$folder/fixture.exe" /link /PDB:NONE /INCREMENTAL:NO || exit /b 2
"$folder/fixture.exe"
"@
 [IO.File]::WriteAllText("$folder/run.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$folder/run.cmd" *> "$folder/result.log";$code=$LASTEXITCODE
 $log=[IO.File]::ReadAllText("$folder/result.log");$complete=$log-match'capture history: (\d+) PASS / (\d+) FAIL';$passed=0;$failed=1;if($complete){$passed=[int]$Matches[1];$failed=[int]$Matches[2]}
 $rows+=@{arch=$arch;completed=$complete;exit=$code;passed=$passed;failed=$failed};Write-Output "$arch $passed PASS / $failed FAIL"
}
@{completed=(@($rows|Where-Object completed -eq $true).Count-eq2);rows=$rows;scope='bounded capture history over controlled source hashes and clocks; no target APIs or renderer freshness proof'}|ConvertTo-Json -Depth 5|Set-Content "$Out/result.json"
if(@($rows|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
