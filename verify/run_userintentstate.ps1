# Hidden HWNDs and fake original-API samples only: no foreground mutation or OS input.
param([string]$Out="$PSScriptRoot\..\build\test\user_intent\state")
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $Out|Out-Null
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path
foreach($exe in @('vi_userintenttest.exe','vi_userintenttest32.exe')){
 $lines=& "$D\$exe"; $exit=$LASTEXITCODE
 $lines|Set-Content "$Out\$exe.log"
 $lines
 if($exit){throw "$exe deterministic user intent state failure ($exit)"}
}
$global:LASTEXITCODE=0
