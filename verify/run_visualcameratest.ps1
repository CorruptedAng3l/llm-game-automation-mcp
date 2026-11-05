param([string]$RepoRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\visual_camera",[string]$ServerJs='')
$ErrorActionPreference='Stop';$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path
$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Force $run,"$run\temp");$env:TEMP="$run\temp";$env:TMP=$env:TEMP
$beforeServer=$env:CAMERA_HELPER_SERVER;if($ServerJs){$env:CAMERA_HELPER_SERVER=(Resolve-Path -LiteralPath $ServerJs).Path}else{Remove-Item Env:CAMERA_HELPER_SERVER -ErrorAction SilentlyContinue}
$rows=@();$complete=$false;$failure=$null;$pins=@()
try{
 foreach($file in @($PSCommandPath,"$RepoRoot\mcp\src\camerahelpers.ts","$RepoRoot\mcp\test\t_visual_camera.mjs","$RepoRoot\mcp\test\t_visual_camera_mcp.mjs","$RepoRoot\mcp\test\fake_camera.mjs")){$pins+=@{path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash}}
 if($ServerJs){$pins+=@{path=$ServerJs;sha256=(Get-FileHash -LiteralPath $ServerJs).Hash}}
 Push-Location "$RepoRoot\mcp"
 try{
  & npm run build *> "$run\build.log";if($LASTEXITCODE-ne0){throw 'TypeScript build failed'}
  foreach($test in @('t_visual_camera.mjs','t_visual_camera_mcp.mjs')){
   $log=Join-Path $run ($test+'.log');& node "test/$test" *> $log;$code=$LASTEXITCODE
   $line=@(Get-Content -LiteralPath $log|Where-Object {$_.StartsWith('VISUAL_CAMERA_RESULT ')})|Select-Object -Last 1
   if(-not$line){throw "Missing complete result from $test"};$result=$line.Substring(21)|ConvertFrom-Json
   $rows+=@{test=$test;exit=$code;result=$result;log=$log};Write-Output "$test $($result.passed) PASS / $($result.failed) FAIL"
  }
 }finally{Pop-Location}
 $complete=$rows.Count-eq2-and@($rows|Where-Object {$_.result.completed-ne$true}).Count-eq0
}catch{$failure=$_.Exception.Message;Write-Output "FAIL $failure"}finally{if($null-eq$beforeServer){Remove-Item Env:CAMERA_HELPER_SERVER -ErrorAction SilentlyContinue}else{$env:CAMERA_HELPER_SERVER=$beforeServer}}
$passed=0;$failed=0;foreach($r in $rows){$passed+=[int]$r.result.passed;$failed+=[int]$r.result.failed}
@{completed=$complete;passed=$passed;failed=$failed;rows=$rows;error=$failure;pins=$pins;scope='pure visual protocol model plus actual MCP orchestration against simulated CLI target; no native GUI or input consumption';before_server=$ServerJs}|ConvertTo-Json -Depth 8|Set-Content -Encoding utf8NoBOM -LiteralPath "$run\result.json"
Write-Output "RESULT $passed PASS / $failed FAIL $run"
if(-not$complete-or$failed-or@($rows|Where-Object exit -ne 0).Count){exit 1};exit 0
