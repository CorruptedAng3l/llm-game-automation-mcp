param([Parameter(Mandatory=$true)][string]$RepoRoot,[string]$SourceRoot,[string]$FenceDir,[string]$Out)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not $SourceRoot){$SourceRoot=$RepoRoot};$SourceRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $SourceRoot).Path)
if(-not $FenceDir){$FenceDir=Join-Path $SourceRoot 'src'};$FenceDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $FenceDir).Path)
if(-not $Out){$Out=Join-Path $RepoRoot ('_scratch/claimed_di_memory/run_'+[Guid]::NewGuid().ToString('N'))};$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Output must be fresh'}
if(-not $Out.StartsWith($RepoRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay under the explicitly selected repository'}
[IO.Directory]::CreateDirectory($Out)|Out-Null
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../test/vi_claimed_di_memory.cpp'))
$extractor=Join-Path $PSScriptRoot 'vi_claimed_di_extract.py';$include=Join-Path $Out 'actual'
& python $extractor --source-root $SourceRoot --fence-dir $FenceDir --test-file $source --out $include
if($LASTEXITCODE -ne 0){throw 'Production-body extraction failed; no model was run'}
$pins=Get-Content -LiteralPath (Join-Path $include 'sourcepins.json') -Raw|ConvertFrom-Json
$results=@();$complete=$true
function ModelCount($value){return (($value -is [int] -or $value -is [long]) -and $value -ge 0)}
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;[IO.Directory]::CreateDirectory($folder)|Out-Null
 $tempDir=Join-Path $folder 'temp';[IO.Directory]::CreateDirectory($tempDir)|Out-Null
 $vc=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $exe=Join-Path $folder 'model.exe';$obj=Join-Path $folder 'model.obj';$log=Join-Path $folder 'build.log';$bat=Join-Path $folder 'build.cmd'
 # /OPT:NOICF is model-only: distinct fake methods must have distinct addresses;
 # explicitly shared fake A/W entries still exercise real typed deduplication.
 $lines=@('@echo off','setlocal',('call "'+$vcDir+'\'+$vc+'" >nul'),('set "TEMP='+$tempDir+'"'),('set "TMP='+$tempDir+'"'),('cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /I"'+$include+'" /Fo"'+$obj+'" /Fe"'+$exe+'" "'+$source+'" /link /OPT:NOICF >"'+$log+'" 2>&1'),'exit /b %errorlevel%')
 [IO.File]::WriteAllLines($bat,$lines,[Text.Encoding]::ASCII)
 & cmd.exe /d /c $bat
 $compile=$LASTEXITCODE;$run=$null;$summary=$null
 if($compile -eq 0){$output=Join-Path $folder 'run.log';& $exe *> $output;$run=$LASTEXITCODE;try{$summary=(Get-Content -LiteralPath $output -Tail 1)|ConvertFrom-Json}catch{}}
 if($compile -ne 0 -or -not $summary -or $summary.completed -ne $true -or -not (ModelCount $summary.passed) -or -not (ModelCount $summary.failed)){$complete=$false}
 $row=@{arch=$arch;build_exit=$compile;run_exit=$run;summary=$summary;build_log=$log};$results+=,$row;Write-Output ($row|ConvertTo-Json -Compress -Depth 5)
}
$sourceStable=$true
foreach($property in $pins.inputs.PSObject.Properties){if((Get-FileHash -LiteralPath $property.Name -Algorithm SHA256).Hash -ne $property.Value){$sourceStable=$false}}
$passed=0;$failed=0;foreach($row in $results){if($row.summary){$passed+=$row.summary.passed;$failed+=$row.summary.failed}}
$report=@{completed=($complete -and $sourceStable);passed=$passed;failed=$failed;source_stable=$sourceStable;guarded_source=$pins.guarded_source;cases=$results;scope='pure memory; actual source bodies, all COM/module/MinHook callbacks fabricated; no native desktop or DirectInput API'}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding utf8
Write-Output ('RESULT '+(Join-Path $Out 'result.json'))
if(-not $report.completed -or @($results|Where-Object {$_.build_exit -ne 0 -or $_.run_exit -ne 0}).Count -or $failed){exit 1}
