param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$SchedulerSource='', [string]$Out="$PSScriptRoot\_out\sequence_deadline")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent));$SourceRoot=[IO.Path]::GetFullPath($SourceRoot)
if(-not$SchedulerSource){$SchedulerSource=Join-Path $SourceRoot 'src\vi_key_sequence_jobs.inl'}
$output=[IO.Path]::GetFullPath($Out);if(-not$output.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside this checkout'}
$run=Join-Path $output ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
$env:TEMP="$run\temp";$env:TMP=$env:TEMP
$body=[IO.File]::ReadAllText($SchedulerSource);$start=$body.IndexOf('void JobRunSequence(');if($start-lt0){throw 'actual scheduler marker missing'};$brace=$body.IndexOf('{',$start);$depth=1;$at=$brace+1
while($at-lt$body.Length-and$depth){if($body[$at]-eq'{'){$depth++};if($body[$at]-eq'}'){$depth--};$at++};if($depth){throw 'actual scheduler body incomplete'}
[IO.File]::WriteAllText("$run\actual_sequence.inl",$body.Substring($start,$at-$start),[Text.UTF8Encoding]::new($false))
$pins=@($PSCommandPath,$SchedulerSource,"$root\test\vi_sequence_deadline_model.cpp","$root\src\vi_qpc.h","$root\src\vi_key_sequence.h","$root\src\vi_sequence_frame.h")|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}}
$cases=@();foreach($arch in @('x64','x86')){
 $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$run" "$root\test\vi_sequence_deadline_model.cpp" /Fo:"$folder\model.obj" /Fe:"$folder\model.exe" || exit /b 2
"$folder\model.exe"
"@|Set-Content -LiteralPath "$folder\run.cmd" -Encoding ascii
 & cmd /d /c "$folder\run.cmd" *> "$folder\result.log";$code=$LASTEXITCODE;$text=[IO.File]::ReadAllText("$folder\result.log");$passed=0;$failed=1;$parsed=$text-match'sequence deadline model: (\d+) PASS / (\d+) FAIL';if($parsed){$passed=[int]$Matches[1];$failed=[int]$Matches[2]}
 $cases+=@{arch=$arch;exit=$code;completed=$parsed;passed=$passed;failed=$failed;log="$folder\result.log"};Write-Output "$arch $passed PASS / $failed FAIL"
}
$result=@{completed=(@($cases|Where-Object completed -eq $true).Count-eq2);passed=($cases.passed|Measure-Object -Sum).Sum;failed=($cases.failed|Measure-Object -Sum).Sum;cases=$cases;pins=$pins;scope='actual extracted scheduler with finite QPC/input callbacks only; no native input or game timing claim'};$result|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$run\result.json" -Encoding utf8NoBOM
if(@($cases|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
