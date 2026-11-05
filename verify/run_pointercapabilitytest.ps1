param([string]$Out="$PSScriptRoot\_out\pointer_capability")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent));$output=[IO.Path]::GetFullPath($Out)
if(-not$output.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside checkout'}
[void](New-Item -ItemType Directory -Force -Path @($output,"$output/temp"));$env:TEMP="$output/temp";$env:TMP=$env:TEMP;$cases=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $output $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /std:c++17 /I"$root\src" "$root\test\vi_pointer_capability.cpp" /Fo:"$folder\model.obj" /Fe:"$folder\model.exe" || exit /b 2
"$folder\model.exe"
"@|Set-Content -LiteralPath "$folder/run.cmd" -Encoding ascii
 & cmd /d /c "$folder\run.cmd" *> "$folder/result.log";$code=$LASTEXITCODE;$text=[IO.File]::ReadAllText("$folder/result.log");$parsed=$text-match'pointer capability: (\d+) PASS / (\d+) FAIL';$passed=0;$failed=1;if($parsed){$passed=[int]$Matches[1];$failed=[int]$Matches[2]}
 $cases+=@{arch=$arch;exit=$code;completed=$parsed;passed=$passed;failed=$failed;log="$folder/result.log"};Write-Output "$arch $passed PASS / $failed FAIL"
}
@{completed=(@($cases|Where-Object {$_.completed-and$_.exit-eq0}).Count-eq2);cases=$cases;scope='actual CLI admission helper with finite capability/transport callbacks; no native API or input'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$output/result.json" -Encoding utf8NoBOM
if(@($cases|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
