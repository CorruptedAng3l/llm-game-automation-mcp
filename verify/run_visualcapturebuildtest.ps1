param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\visual_capture_build")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP;$rows=@();$failed=$false
foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++20 "$RepoRoot\src\vinput_cli.cpp" /Fo:"$folder\cli.obj" /Fe:"$folder\cli.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
"@
 [IO.File]::WriteAllText("$folder/build.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$folder/build.cmd" *> "$folder/build.log";$compile=$LASTEXITCODE
 $help=$null;$code=$null;if($compile-eq0){& "$folder/cli.exe" --json help *> "$folder/help.json";$code=$LASTEXITCODE;$help=Get-Content -Raw -LiteralPath "$folder/help.json"|ConvertFrom-Json}
 $warning=@(Select-String -LiteralPath "$folder/build.log" -Pattern '\bwarning [A-Z]+\d+').Count
 $pass=$compile-eq0-and$warning-eq0-and$code-eq0-and$help.ok-eq$true-and$help.image_scope_version-eq1
 $rows+=@{arch=$arch;compile_exit=$compile;warnings=$warning;help_exit=$code;image_scope_version=$help.image_scope_version;passed=$pass};if(-not$pass){$failed=$true}
}
$passed=@($rows|Where-Object passed -eq $true).Count;$failCount=@($rows|Where-Object passed -ne $true).Count
@{completed=(-not$failed);passed=$passed;failed=$failCount;rows=$rows;scope='both-architecture native CLI builds plus local help capability; no native target, input, capture or window manipulation'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$run/result.json" -Encoding utf8NoBOM
Write-Output "RESULT $passed PASS / $failCount FAIL $run";if($failed){exit 1};exit 0
