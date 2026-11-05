param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$SourceFile='',[string]$Out="$PSScriptRoot\_out\cli_budget")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;if(-not$SourceFile){$SourceFile="$RepoRoot\src\vinput_cli.cpp"};$SourceFile=(Resolve-Path -LiteralPath $SourceFile).Path
$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP;$rows=@();$failed=$false;$pins=@()
foreach($file in @($PSCommandPath,$SourceFile,"$RepoRoot\src\vi_cli_deadline.h","$RepoRoot\src\vi_cli_transport.inl","$RepoRoot\test\vi_cli_deadline_test.cpp","$RepoRoot\test\vi_cli_transport_test.cpp","$RepoRoot\verify\vi_cli_inject_budget_extract.py")){$pins+=@{path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash}}
& python "$RepoRoot\verify\vi_cli_inject_budget_extract.py" --source $SourceFile --out "$run\inject.cpp";if($LASTEXITCODE){throw 'production Inject extraction failed'}
foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" "$RepoRoot\test\vi_cli_deadline_test.cpp" /Fo:"$folder\deadline.obj" /Fe:"$folder\deadline.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" "$run\inject.cpp" /Fo:"$folder\inject.obj" /Fe:"$folder\inject.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" "$RepoRoot\test\vi_cli_transport_test.cpp" /Fo:"$folder\transport.obj" /Fe:"$folder\transport.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
"@
 [IO.File]::WriteAllText("$folder/build.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$folder/build.cmd" *> "$folder/build.log";$compile=$LASTEXITCODE;$results=@()
 if($compile-eq0){foreach($name in @('deadline','inject','transport')){& "$folder/$name.exe" *> "$folder/$name.log";$code=$LASTEXITCODE;$line=Get-Content -LiteralPath "$folder/$name.log" -Tail 1;$model=$null;if($line-match'^CLI_(BUDGET|INJECT_BUDGET|TRANSPORT)_RESULT (\d+) (\d+)$'){$model=@{passed=[int]$Matches[2];failed=[int]$Matches[3]}};$results+=@{name=$name;exit=$code;result=$model};if($code-ne0-or-not$model-or$model.failed){$failed=$true}}}else{$failed=$true}
 $warning=@(Select-String -LiteralPath "$folder/build.log" -Pattern '\bwarning [A-Z]+\d+').Count;if($warning){$failed=$true};$rows+=@{arch=$arch;compile_exit=$compile;warnings=$warning;models=$results}
}
$passed=0;$failCount=0;foreach($row in $rows){foreach($model in $row.models){$passed+=[int]$model.result.passed;$failCount+=[int]$model.result.failed}}
@{completed=(-not$failed);passed=$passed;failed=$failCount;rows=$rows;pins=$pins;scope='pure deadline model; exact production Inject tail with fake OS calls; actual named pipe only within this console process; no target process, injection, GUI, GPU, input or desktop writes'}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath "$run/result.json" -Encoding utf8NoBOM
Write-Output "RESULT $passed PASS / $failCount FAIL $run";if($failed){exit 1};exit 0
