param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\foregroundtest")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
$owned=@(); $results=@(); $lock="$Out\owned_$([guid]::NewGuid().ToString('N')).lock"
try {
  foreach($name in @('vi_testinput.exe','vi_testinput32.exe')) {
    $target="$D\$name"; $targetArgs="`"$Out\$name.log`" -28000 600 640 360"
    $reply=& $V launch --exe $target --args $targetArgs --lock $lock --settle 0 --timeout 10 --no-inject | ConvertFrom-Json | Select-Object -Last 1
    if($reply.pid){$owned += Get-Process -Id $reply.pid}
    $results += $reply
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$Out\result.json"
    if(-not $reply.ok -or $reply.foreground_restored -ne $true -or (Test-Path -LiteralPath $lock)){throw 'Launch lacks verified foreground cleanup/owned-lock release'}
  }
  $bad=& $V launch --exe "$Out\nonexistent.exe" --lock $lock --timeout 1 2> "$Out\failure.stderr" | ConvertFrom-Json
  if($bad.ok -or (Test-Path -LiteralPath $lock)){throw 'Launch failure did not release its own lock'}
  'PASS nonactivating x64/x86 launch reports foreground restoration and releases own lock on success/error'
} finally { foreach($p in $owned){if(-not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false}} }
