param([string]$CoreSource=(Join-Path (Split-Path $PSScriptRoot -Parent) 'src\vinput_core.cpp'),[string]$ModeSource=(Join-Path (Split-Path $PSScriptRoot -Parent) 'src\vi_pointer_scope.inl'),[string]$Out="$PSScriptRoot\_out\pointer_widen_model")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent));$output=[IO.Path]::GetFullPath($Out)
if(-not$output.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside this checkout'}
$run=Join-Path $output ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP
$body=[IO.File]::ReadAllText($CoreSource)
foreach($item in @(@{marker='if (cmd == "mouse" || cmd == "move") {';name='actual_motion_branch.inl'},@{marker='if (cmd == "click" || cmd == "mousedown" || cmd == "mouseup") {';name='actual_pointer_branch.inl'})){
 $start=$body.IndexOf($item.marker);if($start-lt0){throw 'actual branch marker missing'};$brace=$body.IndexOf('{',$start);$depth=1;$at=$brace+1
 while($at-lt$body.Length-and$depth){if($body[$at]-eq'{'){$depth++};if($body[$at]-eq'}'){$depth--};$at++};if($depth){throw 'actual branch incomplete'};[IO.File]::WriteAllText((Join-Path $run $item.name),$body.Substring($start,$at-$start),[Text.UTF8Encoding]::new($false))
}
Copy-Item -LiteralPath $ModeSource -Destination "$run\actual_pointer_admit.inl";$cases=@()
foreach($arch in @('x64','x86')){$folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cd /d "$folder"
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$root\src" /I"$run" "$root\test\vi_pointer_motion_model.cpp" /Fo:"$folder\motion.obj" /Fe:"$folder\motion.exe" || exit /b 2
"$folder\motion.exe"
set model_exit=%errorlevel%
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /I"$root\third_party\minhook\include" /I"$root\third_party\minhook\src" /FI"$root\src\vi_mh_rename.h" /c "$root\third_party\minhook\src\buffer.c" "$root\third_party\minhook\src\hook.c" "$root\third_party\minhook\src\trampoline.c" "$root\third_party\minhook\src\hde\$hde.c" || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /I"$root\src" /I"$run" /I"$root\third_party\minhook\include" "$root\test\vi_pointer_release_memory.cpp" buffer.obj hook.obj trampoline.obj $hde.obj /Fo:"$folder\release.obj" /Fe:"$folder\release.exe" || exit /b 2
"$folder\release.exe"
if not %errorlevel%==0 exit /b 1
exit /b %model_exit%
"@|Set-Content -LiteralPath "$folder\run.cmd" -Encoding ascii
 & cmd /d /c "$folder\run.cmd" *> "$folder\result.log";$code=$LASTEXITCODE;$text=[IO.File]::ReadAllText("$folder\result.log");$passed=0;$failed=0;$found=0
 foreach($name in @('pointer motion model','pointer release memory')){if($text-match($name+': (\d+) PASS / (\d+) FAIL')){$passed+=[int]$Matches[1];$failed+=[int]$Matches[2];$found++}else{$failed++}}
 $cases+=@{arch=$arch;exit=$code;completed=($found-eq2);passed=$passed;failed=$failed;log="$folder\result.log"};Write-Output "$arch $passed PASS / $failed FAIL"
}
$pins=@($PSCommandPath,$CoreSource,$ModeSource,"$root\test\vi_pointer_motion_model.cpp","$root\test\vi_pointer_release_memory.cpp")|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}}
@{completed=(@($cases|Where-Object completed -eq $true).Count-eq2);passed=($cases.passed|Measure-Object -Sum).Sum;failed=($cases.failed|Measure-Object -Sum).Sum;cases=$cases;pins=$pins;scope='exact native motion/pointer branches with private motion/sleep/scope callbacks and terminal writer white-box release; no window, GPU, OS input or product Init'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$run\result.json" -Encoding utf8NoBOM
if(@($cases|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
