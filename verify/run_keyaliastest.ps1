param([string]$Out="$PSScriptRoot\..\build\test\keyaliastest",[switch]$ExpectRed,[switch]$ParserOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$repo=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force -Path $Out|Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Check([bool]$Condition,[string]$Name){$checks.Add([pscustomobject]@{pass=$Condition;name=$Name});if($Condition){"PASS $Name"}else{"FAIL $Name"}}
foreach($arch in @('x64','x86')){
 $archDir=Join-Path $Out $arch;New-Item -ItemType Directory -Force -Path $archDir|Out-Null
 $vcvars=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
setlocal
call "$vcDir\$vcvars" >nul || exit /b 2
pushd "$archDir"
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /O2 "$repo\test\vi_keystest.cpp" /Fo:vi_keystest.obj /Fe:vi_keystest.exe || exit /b 2
"$archDir\vi_keystest.exe"
exit /b %ERRORLEVEL%
"@
 [IO.File]::WriteAllText("$archDir\run.cmd",$batch,[Text.Encoding]::ASCII)
 & cmd /d /c "$archDir\run.cmd" *> "$archDir\parser.log";$parserExit=$LASTEXITCODE
 $parserLog=Get-Content "$archDir\parser.log" -Raw
 Check ($parserExit -eq 0 -and $parserLog -match 'failures=0' -and $parserLog -notmatch '(?im)warning [A-Z]+\d+|error [A-Z]+\d+') "$arch strict key parser accepts exact num aliases/chords and rejects malformed names"
 if($ParserOnly){continue}
 $targetExe=if($arch -eq 'x64'){'vi_testinput.exe'}else{'vi_testinput32.exe'}
 $cli=Join-Path "$repo\dist" $(if($arch -eq 'x64'){'vinput.exe'}else{'vinput32.exe'})
 $target=$null
 try{
  $target=Start-Process -FilePath "$repo\dist\$targetExe" -ArgumentList "`"$archDir\target.log`" -28000 600 640 360" -WindowStyle Hidden -PassThru
  $reply=& $cli --json inject --pid $target.Id|ConvertFrom-Json
  if(-not $reply.ok){throw 'Own target injection failed'}
  foreach($digit in 0..9){
   $down=& $cli --json --pid $target.Id down "num$digit"|ConvertFrom-Json
   $state=& $cli --json --pid $target.Id status|ConvertFrom-Json
   $up=& $cli --json --pid $target.Id up "num$digit"|ConvertFrom-Json
   $released=& $cli --json --pid $target.Id status|ConvertFrom-Json
   Check ($down.ok -and $up.ok -and (96+$digit) -in $state.virtual_down -and (96+$digit) -notin $released.virtual_down -and -not $state.really_foreground) "$arch num$digit holds/releases VK$(96+$digit) without foreground"
  }
 }finally{if($target -and -not $target.HasExited){Stop-Process -Id $target.Id -Force -Confirm:$false}}
}
$failed=@($checks|Where-Object{-not $_.pass}).Count
[pscustomobject]@{ok=($failed -eq 0);checks=$checks;failed=$failed;expected_red=[bool]$ExpectRed}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$Out\result.json"
if($ExpectRed){if($failed -lt 2){throw 'Baseline unexpectedly accepted num aliases'};'EXPECTED RED: num aliases are rejected by baseline';$global:LASTEXITCODE=0}
elseif($failed){throw "$failed key-alias controls failed"}
else{$global:LASTEXITCODE=0}
