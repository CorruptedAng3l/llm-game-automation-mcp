param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$SourceFile='',[string]$Out="$PSScriptRoot\_out\cli_budget")
$ErrorActionPreference='Stop';$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;if(-not$SourceFile){$SourceFile="$RepoRoot\src\vinput_cli.cpp"};$SourceFile=(Resolve-Path -LiteralPath $SourceFile).Path
$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'));if(-not$run.StartsWith($RepoRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay in checkout'}
[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP;$env:PYTHONDONTWRITEBYTECODE='1';$rows=@();$failed=$false;$pins=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new()
function Pin([string]$path){$pins.Add(@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash})}
# Reuse the reviewed gated-process/Job Object prefix, never its test body.
$gateSource="$PSScriptRoot/run_topmosthooktest.ps1";Pin $gateSource;$source=Get-Content -LiteralPath $gateSource -Raw
$first=$source.IndexOf('# Reuse the reviewed fixture-only job implementation');$last=$source.IndexOf('. "$PSScriptRoot\_vcvars.ps1"',$first)
if($first-lt0-or$last-le$first){throw 'reviewed process gate prefix missing'}
$Out=$run;$budgetVerifyDir=$PSScriptRoot
. ([scriptblock]::Create($source.Substring($first,$last-$first).Replace('$PSScriptRoot','$budgetVerifyDir')))
. "$PSScriptRoot/_vcvars.ps1";$vcDir=Get-VcVarsDir
foreach($file in @($PSCommandPath,"$PSScriptRoot/_vcvars.ps1",$SourceFile,"$RepoRoot\src\vi_cli_deadline.h","$RepoRoot\src\vi_cli_transport.inl","$RepoRoot\test\vi_cli_deadline_test.cpp","$RepoRoot\test\vi_cli_transport_test.cpp","$RepoRoot\verify\vi_cli_inject_budget_extract.py")){Pin $file}
$extract=RunOwned (Get-Command python).Source @("$RepoRoot/verify/vi_cli_inject_budget_extract.py",'--source',$SourceFile,'--out',"$run/inject.cpp") 15000 'extract';if($extract.exit){throw 'production Inject extraction failed'};Pin "$run/inject.cpp"
foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" "$RepoRoot\test\vi_cli_deadline_test.cpp" /Fo:"$folder\deadline.obj" /Fe:"$folder\deadline.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" "$run\inject.cpp" /Fo:"$folder\inject.obj" /Fe:"$folder\inject.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" "$RepoRoot\test\vi_cli_transport_test.cpp" /Fo:"$folder\transport.obj" /Fe:"$folder\transport.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
"@
 [IO.File]::WriteAllText("$folder/build.cmd",$batch,[Text.Encoding]::ASCII);$build=RunOwned $env:ComSpec @('/d','/c',"$folder/build.cmd") 120000 ($arch+'.build');$build.stdout+$build.stderr|Set-Content -LiteralPath "$folder/build.log";$compile=$build.exit;$results=@()
 if($compile-eq0){foreach($name in @('deadline','inject','transport')){Pin "$folder/$name.exe";$r=RunOwned "$folder/$name.exe" @() 20000 ($arch+'.'+$name);$r.stdout|Set-Content -LiteralPath "$folder/$name.log";$code=$r.exit;$line=($r.stdout.TrimEnd()-split'\r?\n')[-1];$model=$null;if($line-match'^CLI_(BUDGET|INJECT_BUDGET|TRANSPORT)_RESULT (\d+) (\d+)$'){$model=@{passed=[int]$Matches[2];failed=[int]$Matches[3]}};$results+=@{name=$name;exit=$code;result=$model};if($code-ne0-or-not$model-or$model.failed){$failed=$true}}}else{$failed=$true}
 $warning=@(Select-String -LiteralPath "$folder/build.log" -Pattern '\bwarning [A-Z]+\d+').Count;if($warning){$failed=$true};$rows+=@{arch=$arch;compile_exit=$compile;warnings=$warning;models=$results}
}
$passed=0;$failCount=0;foreach($row in $rows){foreach($model in $row.models){$passed+=[int]$model.result.passed;$failCount+=[int]$model.result.failed}}
$stable=$true;foreach($pin in $pins){if((Get-FileHash -LiteralPath $pin.path).Hash-ne$pin.sha256){$stable=$false}};if(-not$stable){$failed=$true}
@{completed=(-not$failed);source_stable=$stable;passed=$passed;failed=$failCount;rows=$rows;pins=$pins;calls=$calls;scope='pure deadline model; exact production Inject tail with fake OS calls; actual named pipe only within this console process; no target process, injection, GUI, GPU, input or desktop writes; original-handle gated Job Object actors'}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath "$run/result.json" -Encoding utf8NoBOM
Write-Output "RESULT $passed PASS / $failCount FAIL $run";if($failed){exit 1};exit 0
