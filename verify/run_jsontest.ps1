param([string]$Out="$PSScriptRoot\..\build\test\jsontest")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V="$D\vinput.exe"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
$p=$null
function OneJson([string[]]$arguments) {
  $lines=@(& $V @arguments 2> "$Out\progress.stderr")
  if($lines.Count -ne 1){throw "Expected one JSON line: $arguments; got $($lines.Count)"}
  return ($lines[0] | ConvertFrom-Json)
}
try {
  if(-not (OneJson @('--json','help')).ok){throw 'JSON help failed'}
  if(-not (OneJson @('keys','--json')).ok){throw 'JSON keys failed'}
  $p=Start-Process -FilePath "$D\vi_testinput.exe" -ArgumentList "`"$Out\target.log`" -28000 600 640 360" -WindowStyle Hidden -PassThru
  if(-not (OneJson @('inject','--pid',"$($p.Id)",'--json')).ok){throw 'Injection failed'}
  Set-Content -LiteralPath "$Out\macro.txt" -Value @('echo first','ping','echo last')
  $r=OneJson @('--json','--pid',"$($p.Id)",'script',"$Out\macro.txt")
  if(-not $r.ok -or $r.commands_run -ne 3){throw 'JSON script failed'}
  if((Get-Content -LiteralPath "$Out\progress.stderr").Count -ne 3){throw 'Script progress was not sent to stderr'}
  Set-Content -LiteralPath "$Out\macro.txt" -Value @('ping','invalid-command')
  $r=OneJson @('--pid',"$($p.Id)",'script',"$Out\macro.txt",'--json')
  if($r.ok -or $r.failures -ne 1){throw 'Script error JSON failed'}
  'PASS one stdout JSON line for help, keys, injection, script success and script failure; progress on stderr'
} finally {if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false}}
