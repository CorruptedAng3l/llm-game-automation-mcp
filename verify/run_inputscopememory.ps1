param([string]$Out="$PSScriptRoot\_out\input_scope_memory")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent));$output=[IO.Path]::GetFullPath($Out)
if(-not$output.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside this checkout'}
$run=Join-Path $output ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP
$cases=@();foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cd /d "$folder"
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /I"$root\third_party\minhook\include" /I"$root\third_party\minhook\src" /FI"$root\src\vi_mh_rename.h" /c "$root\third_party\minhook\src\buffer.c" "$root\third_party\minhook\src\hook.c" "$root\third_party\minhook\src\trampoline.c" "$root\third_party\minhook\src\hde\$hde.c" || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /I"$root\src" /I"$root\third_party\minhook\include" "$root\test\vi_input_scope_memory.cpp" buffer.obj hook.obj trampoline.obj $hde.obj /Fo:"$folder\scope.obj" /Fe:"$folder\scope.exe" || exit /b 2
"$folder\scope.exe"
"@|Set-Content -LiteralPath "$folder\run.cmd" -Encoding ascii
 & cmd /d /c "$folder\run.cmd" *> "$folder\result.log";$code=$LASTEXITCODE;$text=[IO.File]::ReadAllText("$folder\result.log");$passed=0;$failed=1;$parsed=$text-match'input scope memory: (\d+) PASS / (\d+) FAIL';if($parsed){$passed=[int]$Matches[1];$failed=[int]$Matches[2]}
 $cases+=@{arch=$arch;exit=$code;completed=$parsed;passed=$passed;failed=$failed;log="$folder\result.log"};Write-Output "$arch $passed PASS / $failed FAIL"
}
@{completed=(@($cases|Where-Object completed -eq $true).Count-eq2);passed=($cases.passed|Measure-Object -Sum).Sum;failed=($cases.failed|Measure-Object -Sum).Sum;cases=$cases;scope='actual admission mismatch path under terminal writer spies; no window, GPU, worker, OS input or product Init; valid live-scope positive control belongs to guarded fixture'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$run\result.json" -Encoding utf8NoBOM
if(@($cases|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
