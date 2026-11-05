param([string]$Out="$PSScriptRoot\_out\text_context")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$output=[IO.Path]::GetFullPath($Out)
if(-not$output.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside checkout'}
[void](New-Item -ItemType Directory -Force $output,"$output\temp");$env:TEMP="$output\temp";$env:TMP=$env:TEMP;$rows=@()
foreach($arch in @('x64','x86')){
 $folder="$output\$arch";[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /std:c++20 /D_CRT_SECURE_NO_WARNINGS "$root\test\vi_textcontexttest.cpp" /Fo:"$folder\fixture.obj" /Fe:"$folder\fixture.exe" /link user32.lib advapi32.lib shell32.lib ole32.lib || exit /b 2
"$folder\fixture.exe"
"@|Set-Content "$folder\run.cmd" -Encoding ascii
 & cmd /d /c "$folder\run.cmd" *> "$folder\result.log";$code=$LASTEXITCODE;$content=Get-Content "$folder\result.log" -Raw;$parsed=$content-match'text context: (\d+) PASS / (\d+) FAIL';$passed=0;$failed=1;if($parsed){$passed=[int]$Matches[1];$failed=[int]$Matches[2]};$rows+=@{arch=$arch;exit=$code;completed=$parsed;passed=$passed;failed=$failed};Write-Output "$arch $passed PASS / $failed FAIL"
}
@{completed=(@($rows|Where-Object {$_.completed-and$_.exit-eq0}).Count-eq2);rows=$rows;scope='actual CLI crop/timing helpers on finite memory pixels; no target, window, OCR or input API execution'}|ConvertTo-Json -Depth 5|Set-Content "$output\result.json" -Encoding utf8NoBOM
if(@($rows|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
