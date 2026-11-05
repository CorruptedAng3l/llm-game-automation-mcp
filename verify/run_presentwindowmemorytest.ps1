param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\present_window_memory")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP;$rows=@();$failed=$false;$pins=@()
foreach($source in @($PSCommandPath,"$RepoRoot\src\vi_hidden_show.inl","$RepoRoot\src\vi_hidden_show_json.inl","$RepoRoot\test\vi_hidden_show_memory.cpp","$RepoRoot\src\vi_present_window.h","$RepoRoot\src\vi_present_window.inl","$RepoRoot\src\vi_present_window_json.inl","$RepoRoot\src\vi_cli_hidden_window.inl","$RepoRoot\test\vi_present_window_memory.cpp","$RepoRoot\test\vi_cli_hidden_window_memory.cpp","$RepoRoot\src\vi_cap9.inl","$RepoRoot\src\vinput_core.cpp")){$pins+=@{path=$source;sha256=(Get-FileHash -LiteralPath $source).Hash}}
foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$RepoRoot\test\vi_present_window_memory.cpp" /Fo:"$folder\model.obj" /Fe:"$folder\model.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++20 "$RepoRoot\test\vi_cli_hidden_window_memory.cpp" /Fo:"$folder\cli_model.obj" /Fe:"$folder\cli_model.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$RepoRoot\test\vi_hidden_show_memory.cpp" /Fo:"$folder\show_model.obj" /Fe:"$folder\show_model.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /I"$RepoRoot\third_party\minhook\include" /c "$RepoRoot\src\vinput_core.cpp" /Fo:"$folder\core.obj" || exit /b 2
"@|Set-Content -LiteralPath "$folder/build.cmd" -Encoding ascii
 & cmd /d /c "$folder/build.cmd" *> "$folder/build.log";$compile=$LASTEXITCODE;$models=@();if($compile-eq0){foreach($model in @('model','cli_model','show_model')){& "$folder/$model.exe" *> "$folder/$model.log";$code=$LASTEXITCODE;$line=Get-Content -LiteralPath "$folder/$model.log" -Tail 1;$result=$null;if($line-match'^(CLI_HIDDEN_WINDOW|PRESENT_WINDOW|HIDDEN_SHOW)_MEMORY_RESULT (\d+) (\d+)$'){$result=@{passed=[int]$Matches[2];failed=[int]$Matches[3]}};$models+=@{name=$model;exit=$code;result=$result};if($code-ne0-or-not$result-or$result.failed){$failed=$true}}}else{$failed=$true}
 $warnings=@(Select-String -LiteralPath "$folder/build.log" -Pattern '\bwarning [A-Z]+\d+').Count;$rows+=@{arch=$arch;compile_exit=$compile;warnings=$warnings;models=$models};if($warnings){$failed=$true}
}
$passed=0;$failCount=0;foreach($row in $rows){foreach($model in $row.models){$passed+=[int]$model.result.passed;$failCount+=[int]$model.result.failed}}
@{completed=(-not$failed);passed=$passed;failed=$failCount;rows=$rows;pins=$pins;scope='exact production presentation metadata adapter under memory-only window/process/clock fakes, plus both native core compilations; no GUI, GPU, injection or input'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$run/result.json" -Encoding utf8NoBOM
Write-Output "RESULT $passed PASS / $failCount FAIL $run";if($failed){exit 1};exit 0
