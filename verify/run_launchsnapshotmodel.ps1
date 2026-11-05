param([string]$AdapterSource=(Join-Path (Split-Path $PSScriptRoot -Parent) 'src\vi_cli_launch_follow.inl'),[string]$Out="$PSScriptRoot\_out\launch_snapshot_model")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent));$output=[IO.Path]::GetFullPath($Out)
if(-not$output.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside this checkout'}
$run=Join-Path $output ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP
Copy-Item -LiteralPath $AdapterSource -Destination "$run\actual_launch_adapter.inl";$cases=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++20 /I"$root\src" /I"$run" "$root\test\vi_launch_snapshot_model.cpp" /Fo:"$folder\model.obj" /Fe:"$folder\model.exe" || exit /b 2
"$folder\model.exe"
"@|Set-Content -LiteralPath "$folder\run.cmd" -Encoding ascii
 & cmd /d /c "$folder\run.cmd" *> "$folder\result.log";$code=$LASTEXITCODE;$text=[IO.File]::ReadAllText("$folder\result.log");$passed=0;$failed=1;$parsed=$text-match'launch snapshot model: (\d+) PASS / (\d+) FAIL';if($parsed){$passed=[int]$Matches[1];$failed=[int]$Matches[2]}
 $cases+=@{arch=$arch;exit=$code;completed=$parsed;passed=$passed;failed=$failed;log="$folder\result.log"};Write-Output "$arch $passed PASS / $failed FAIL"
}
$pins=@($PSCommandPath,$AdapterSource,"$root\src\vi_launch_follow.h","$root\test\vi_launch_snapshot_model.cpp")|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}}
@{completed=(@($cases|Where-Object completed -eq $true).Count-eq2);passed=($cases.passed|Measure-Object -Sum).Sum;failed=($cases.failed|Measure-Object -Sum).Sum;cases=$cases;pins=$pins;scope='exact production Toolhelp adapter with finite private API callbacks; no actual processes, windows, input or native identity stress';failure_is_expected_inversion=$false}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$run\result.json" -Encoding utf8NoBOM
if(@($cases|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
