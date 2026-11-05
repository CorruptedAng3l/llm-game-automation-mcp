param([Parameter(Mandatory=$true)][string]$RepoRoot,[string]$SourceRoot,[Parameter(Mandatory=$true)][string]$Out)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;if(-not$SourceRoot){$SourceRoot=$RepoRoot};$SourceRoot=(Resolve-Path -LiteralPath $SourceRoot).Path
$Out=[IO.Path]::GetFullPath($Out);if(-not$Out.StartsWith($RepoRoot+'\',[StringComparison]::OrdinalIgnoreCase)-or(Test-Path -LiteralPath $Out)){throw 'Fresh private output required'}
[void][IO.Directory]::CreateDirectory($Out);$header=Join-Path $SourceRoot 'src/vi_text.h';$fixture=Join-Path $RepoRoot 'test/vi_textwordtest.cpp';$pins=@{header=(Get-FileHash $header).Hash;fixture=(Get-FileHash $fixture).Hash};$rows=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out ('arch_'+$arch);[void][IO.Directory]::CreateDirectory($folder);[void][IO.Directory]::CreateDirectory("$folder/temp");$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @('@echo off',('call "'+$vcDir+'\'+$vc+'" >nul || exit /b 2'),('set "TEMP='+$folder+'\temp"'),('set "TMP='+$folder+'\temp"'),('cl /nologo /std:c++17 /O2 /EHsc /W4 /WX /I"'+$SourceRoot+'\src" /Fo"'+$folder+'\model.obj" /Fe"'+$folder+'\model.exe" "'+$fixture+'" >"'+$folder+'\build.log" 2>&1'),'exit /b %errorlevel%')|Set-Content -LiteralPath "$folder/build.cmd" -Encoding ascii
 & cmd.exe /d /c "$folder/build.cmd";$compile=$LASTEXITCODE;$exitCode=$null;$summary=$null
 if($compile-eq0){& "$folder/model.exe" >"$folder/run.log";$exitCode=$LASTEXITCODE;try{$summary=Get-Content "$folder/run.log" -Tail 1|ConvertFrom-Json}catch{}}
 $rows+=@{arch=$arch;compile=$compile;exit=$exitCode;summary=$summary};$rows[-1]|ConvertTo-Json -Compress -Depth 4
}
$stable=(Get-FileHash $header).Hash-eq$pins.header-and(Get-FileHash $fixture).Hash-eq$pins.fixture;$passed=0;$failed=0;foreach($row in $rows){if($row.summary){$passed+=$row.summary.passed;$failed+=$row.summary.failed}}
$complete=$stable-and@($rows|Where-Object {$_.compile-ne0-or$_.summary.completed-ne$true}).Count-eq0
@{completed=$complete;passed=$passed;failed=$failed;source_stable=$stable;pins=$pins;rows=$rows;scope='actual vi_text header and Windows lowercase string API only; no OCR/window/device/input APIs'}|ConvertTo-Json -Depth 6|Set-Content "$Out/result.json" -Encoding utf8
if(-not$complete-or$failed){exit 1};exit 0
