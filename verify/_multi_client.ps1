# One independent "agent" for run_multitest.ps1: its own process, its own target, its own keys.
param([string]$V, [int]$TargetPid, [string]$Owner, [string]$Keys, [int]$Rounds = 4, [string]$Out)
$lat = [Collections.Generic.List[double]]::new(); $fails = 0
foreach ($r in 1..$Rounds) {
  foreach ($k in $Keys.Split(',')) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $reply = & $V --owner $Owner --pid $TargetPid key $k 30 | ConvertFrom-Json
    $sw.Stop(); $lat.Add($sw.Elapsed.TotalMilliseconds)
    if (-not $reply.ok) { $fails++ }
  }
}
$s = $lat | Sort-Object
[pscustomobject]@{owner=$Owner; pid=$TargetPid; commands=$lat.Count; failures=$fails
  p50_ms=[math]::Round($s[[int]($s.Count*0.5)],1); p95_ms=[math]::Round($s[[math]::Min($s.Count-1,[int]($s.Count*0.95))],1); max_ms=[math]::Round($s[-1],1)} |
  ConvertTo-Json -Compress | Set-Content -LiteralPath $Out
