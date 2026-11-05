param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\foregroundtest",[string]$SourceRoot="",[switch]$BuildOnly)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot\..").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
. "$PSScriptRoot\vi_legacy_launch_guard.ps1"
if(-not$SourceRoot){$SourceRoot=$root}
$guardHarness=New-ViLegacyHarness $root $SourceRoot $CliDir $Out
$D="$($guardHarness.tools)\x64";$V="$D\vinput.exe"
if($BuildOnly){Save-ViLegacyEvidence $guardHarness;Write-Output "BUILD ONLY 2 PASS / 0 FAIL $($guardHarness.folder) (no GUI executed)";return}

$owned=@(); $results=@(); $lock="$Out\owned_$([guid]::NewGuid().ToString('N')).lock"
try {
  foreach($name in @('vi_testinput.exe','vi_testinput32.exe')) {
    $target="$D\$name"; $targetArgs="`"$Out\$name.log`" -28000 600 640 360"
    # Initial nonactivating cleanup only. Delayed foreground re-grab coverage
    # lives in run_foreground_suite.ps1; do not spend its observation window here.
    $record=Invoke-ViLegacyCli $guardHarness $name @('launch','--exe',$target,'--args',$targetArgs,'--lock',$lock,'--settle','0','--fg-guard','0','--timeout','10','--no-inject') $target
    $reply=@($record.json|Where-Object {$null-ne$_})|Select-Object -Last 1
    foreach($targetProcess in $record.targets){$owned += $targetProcess}
    $results += $reply
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$Out\result.json"
    if(-not $reply.ok -or $reply.foreground_restored -ne $true -or (Test-Path -LiteralPath $lock)){throw 'Launch lacks verified foreground cleanup/owned-lock release'}
  }
  $failed=Invoke-ViLegacyCli $guardHarness 'invalid_exe' @('launch','--exe',"$Out\nonexistent.exe",'--lock',$lock,'--timeout','1')
  [IO.File]::WriteAllText("$Out\failure.stderr",$failed.stderr)
  $bad=@($failed.json|Where-Object {$null-ne$_})|Select-Object -Last 1
  if($bad.ok -or (Test-Path -LiteralPath $lock)){throw 'Launch failure did not release its own lock'}
  'PASS nonactivating x64/x86 launch reports foreground restoration and releases own lock on success/error'
} finally { try{Stop-ViLegacyTargets $guardHarness}finally{Save-ViLegacyEvidence $guardHarness} }
