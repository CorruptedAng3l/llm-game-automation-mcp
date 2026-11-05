param([Parameter(Mandatory=$true)][string]$RepoRoot,[string]$SourceRoot,[string]$CensusDir,[string]$Out)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not $SourceRoot){$SourceRoot=$RepoRoot};$SourceRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $SourceRoot).Path)
if(-not $CensusDir){$CensusDir=Join-Path $SourceRoot 'src'};$CensusDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CensusDir).Path)
if(-not $Out){$Out=Join-Path $RepoRoot ('_scratch/pad_api_census/run_'+[Guid]::NewGuid().ToString('N'))};$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Output must be fresh'}
if(-not $Out.StartsWith($RepoRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Private repository output required'}
[IO.Directory]::CreateDirectory($Out)|Out-Null;$include=Join-Path $Out 'actual'
& python (Join-Path $PSScriptRoot 'vi_pad_api_census_extract.py') --source-root $SourceRoot --census-dir $CensusDir --out $include
if($LASTEXITCODE -ne 0){throw 'Source extraction failed; no model run'}
$pins=Get-Content -LiteralPath (Join-Path $include 'sourcepins.json') -Raw|ConvertFrom-Json
$rows=@();$complete=$true
function ModelCount($value){return (($value -is [int] -or $value -is [long]) -and $value -ge 0)}
$fixturePins=@{}
foreach($leaf in @('vi_pad_api_census_memory.cpp','vi_pad_api_abi.cpp')){$file=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../test/'+$leaf)));$fixturePins[$file]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
foreach($arch in @('x64','x86')){
 foreach($kind in @('model','abi')){
  $folder=Join-Path $Out ($arch+'.'+$kind);[IO.Directory]::CreateDirectory($folder)|Out-Null;$tempDir=Join-Path $folder 'temp';[IO.Directory]::CreateDirectory($tempDir)|Out-Null
  $vc=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  $source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../test/'+$(if($kind -eq 'abi'){'vi_pad_api_abi.cpp'}else{'vi_pad_api_census_memory.cpp'}))))
  $exe=Join-Path $folder 'model.exe';$obj=Join-Path $folder 'model.obj';$log=Join-Path $folder 'build.log';$bat=Join-Path $folder 'build.cmd'
  $lines=@('@echo off','setlocal',('call "'+$vcDir+'\'+$vc+'" >nul'),('set "TEMP='+$tempDir+'"'),('set "TMP='+$tempDir+'"'),('cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /I"'+$include+'" /Fo"'+$obj+'" /Fe"'+$exe+'" "'+$source+'" /link /OPT:NOICF >"'+$log+'" 2>&1'),'exit /b %errorlevel%')
  [IO.File]::WriteAllLines($bat,$lines,[Text.Encoding]::ASCII);& cmd.exe /d /c $bat
  $build=$LASTEXITCODE;$code=$null;$summary=$null
  if($build -eq 0){$output=Join-Path $folder 'run.log';& $exe *> $output;$code=$LASTEXITCODE;try{$summary=(Get-Content -LiteralPath $output -Tail 1)|ConvertFrom-Json}catch{}}
  if($build -ne 0 -or -not $summary -or $summary.completed -ne $true -or -not (ModelCount $summary.passed) -or -not (ModelCount $summary.failed)){$complete=$false}
  $row=@{arch=$arch;kind=$kind;build_exit=$build;run_exit=$code;summary=$summary;build_log=$log};$rows+=,$row;Write-Output ($row|ConvertTo-Json -Compress -Depth 5)
 }
}
$stable=$true;foreach($property in $pins.inputs.PSObject.Properties){if((Get-FileHash -LiteralPath $property.Name -Algorithm SHA256).Hash -ne $property.Value){$stable=$false}}
foreach($file in $fixturePins.Keys){if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $fixturePins[$file]){$stable=$false}}
$passed=0;$failed=0;foreach($row in $rows){if($row.summary){$passed+=$row.summary.passed;$failed+=$row.summary.failed}}
@{completed=($complete -and $stable);passed=$passed;failed=$failed;source_stable=$stable;fixture_pins=$fixturePins;cases=$rows;scope='pure fabricated callbacks and official SDK static_assert types only; no controller object/device/API'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding utf8
Write-Output ('RESULT '+(Join-Path $Out 'result.json'))
if(-not $complete -or -not $stable -or $failed -or @($rows|Where-Object {$_.build_exit -ne 0 -or $_.run_exit -ne 0}).Count){exit 1}
