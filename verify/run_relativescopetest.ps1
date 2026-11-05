param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\relative_scope")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP;$rows=@();$failed=$false
foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$RepoRoot\test\vi_capturefencetest.cpp" /Fo:"$folder\model.obj" /Fe:"$folder\model.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /I"$RepoRoot\third_party\minhook\include" /c "$RepoRoot\src\vinput_core.cpp" /Fo:"$folder\core.obj" || exit /b 2
"@
 [IO.File]::WriteAllText("$folder/build.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$folder/build.cmd" *> "$folder/build.log";$compile=$LASTEXITCODE
 $result=$null;$code=$null;if($compile-eq0){& "$folder/model.exe" *> "$folder/test.log";$code=$LASTEXITCODE;$line=Get-Content -LiteralPath "$folder/test.log" -Tail 1;if($line-match'^CAPTURE_FENCE_RESULT (\d+) (\d+)$'){$result=@{passed=[int]$Matches[1];failed=[int]$Matches[2]}}}
 $warning=@(Select-String -LiteralPath "$folder/build.log" -Pattern '\bwarning [A-Z]+\d+').Count
 $rows+=@{arch=$arch;compile_exit=$compile;warnings=$warning;exit=$code;result=$result};if($compile-ne0-or$warning-or$code-ne0-or-not$result-or$result.failed){$failed=$true}
}
$passed=0;$failCount=0;foreach($r in $rows){$passed+=[int]$r.result.passed;$failCount+=[int]$r.result.failed}
@{completed=(-not$failed);passed=$passed;failed=$failCount;rows=$rows;scope='exact native token comparison model and both-arch production-core compilation; native command dispatch not executed'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$run/result.json" -Encoding utf8NoBOM
Write-Output "RESULT $passed PASS / $failCount FAIL $run";if($failed){exit 1};exit 0
