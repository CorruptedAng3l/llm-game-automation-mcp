# Multi-agent control: 4 targets (3 x64 + 1 x86), 4 independent client PROCESSES driving them concurrently.
#  - ownership: claim/unclaim/expiry/force; another owner (or no owner) is refused and nothing reaches the target
#  - no cross-talk: every target's log holds exactly its own client's keys
#  - no starvation: a long `capture --after-frames` on target A does not slow the other targets, and a `status` on
#    target A during it returns at once (on older builds it queued behind the capture on A's exec mutex)
# No real foreground or shared lock is used; targets are offscreen and never activated.
param([string]$CliDir = "$PSScriptRoot\..\dist", [string]$Out = "$PSScriptRoot\..\build\test\multitest")
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = (Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$checks = [Collections.Generic.List[object]]::new()
function Check([bool]$c, [string]$name, [string]$detail = '') { $checks.Add([pscustomobject]@{pass=$c; name=$name; detail=$detail}); "$(if ($c) {'PASS'} else {'FAIL'}) $name$(if ($detail) {': ' + $detail})" }
function J([string[]]$a) { $o = & $V @a; ($o | Select-Object -Last 1) | ConvertFrom-Json }
$agents = @(
  @{o='agentA'; exe='vi_testinput.exe';   keys='a,b,c,d,e'},
  @{o='agentB'; exe='vi_testinput.exe';   keys='f,g,h,i,j'},
  @{o='agentC'; exe='vi_testinput.exe';   keys='k,l,m,n,o'},
  @{o='agentD'; exe='vi_testinput32.exe'; keys='p,q,r,s,t'})
$procs = @(); $clients = @()
try {
  foreach ($a in $agents) {
    $a.log = "$Out\$($a.o).log"; Remove-Item -LiteralPath $a.log -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath "$D\$($a.exe)" -ArgumentList "`"$($a.log)`" -28000 600 420 260" -WindowStyle Hidden -PassThru
    $procs += $p; $a.pid = $p.Id
    $r = J @('inject', '--pid', "$($p.Id)"); if (-not $r.ok) { throw "inject $($a.o) failed: $($r | ConvertTo-Json -Compress)" }
  }
  foreach ($a in $agents) { for ($i = 0; $i -lt 50; $i++) { $s = J @('--pid', "$($a.pid)", 'status'); if ($s.hwnd -ne '0x0' -and $s.counters.present_ours -gt 0) { break }; Start-Sleep -Milliseconds 100 } }
  $tA, $tB, $tC, $tD = $agents   # NB: PowerShell names are case-insensitive, so not $A (= the loop variable $a)
  # --- ownership -------------------------------------------------------------------------------------------
  foreach ($a in $agents) { $r = J @('--owner', $a.o, '--pid', "$($a.pid)", 'claim', '--ttl', '120'); Check ($r.ok -and $r.claim.owner -eq $a.o) "$($a.o) claims pid $($a.pid)" }
  $r = J @('--owner', 'agentB', '--pid', "$($tA.pid)", 'key', 'z'); Check (-not $r.ok -and $r.claim_refused -and $r.claim_owner -eq 'agentA') "agentB driving agentA's target is refused" $r.error
  $r = J @('--pid', "$($tA.pid)", 'click', 'left', '10', '10'); Check (-not $r.ok -and $r.claim_refused) "a client with no owner is refused"
  $r = J @('--owner', 'agentB', '--pid', "$($tA.pid)", 'claim'); Check (-not $r.ok -and $r.claim_refused) "agentB cannot take over agentA's claim"
  $r = J @('--owner', 'agentB', '--pid', "$($tA.pid)", 'status'); Check ($r.ok -and $r.claim.owner -eq 'agentA') "status stays readable by anyone (owner shown)"
  $r = J @('--owner', 'agentB', '--pid', "$($tA.pid)", 'unclaim'); Check (-not $r.ok) "agentB cannot release agentA's claim"
  $list = (& $V list) | ConvertFrom-Json
  $mine = @($list | Where-Object { $_.pid -eq $tA.pid })
  Check ($mine.Count -eq 1 -and $mine[0].owner -eq 'agentA' -and $mine[0].really_foreground -eq $false -and $mine[0].uptime_ms -gt 0) "list shows owner/uptime/really_foreground" ($mine | ConvertTo-Json -Compress)
  # expiry: D re-claims with a 2 s TTL, goes idle; another owner may take it after expiry
  $r = J @('--owner', 'agentD', '--pid', "$($tD.pid)", 'claim', '--ttl', '2'); Check $r.ok "agentD re-claims with ttl 2 s"
  $r = J @('--owner', 'agentE', '--pid', "$($tD.pid)", 'key', 'z'); Check (-not $r.ok) "before expiry agentE is refused"
  Start-Sleep -Milliseconds 2600
  $r = J @('--owner', 'agentE', '--pid', "$($tD.pid)", 'claim', '--ttl', '30'); Check ($r.ok -and $r.claim.owner -eq 'agentE') "after expiry agentE can claim"
  $r = J @('--owner', 'agentE', '--pid', "$($tD.pid)", 'unclaim'); Check $r.ok "agentE releases explicitly"
  $r = J @('--owner', 'agentD', '--pid', "$($tD.pid)", 'claim', '--ttl', '120'); Check ($r.ok -and $r.claim.owner -eq 'agentD') "agentD claims again after the release"
  # force: logged override (used here to prove the key really reaches the target when forced)
  $r = J @('--owner', 'agentB', '--force-claim', '--pid', "$($tB.pid)", 'key', 'u'); Check $r.ok "--force-claim overrides (agentB's own target, harmless)"
  $r = J @('--owner', 'agentC', '--force-claim', '--pid', "$($tB.pid)", 'key', 'v'); Check $r.ok "--force-claim from agentC reaches agentB's target"
  # --- concurrency -----------------------------------------------------------------------------------------
  $longCap = Start-Process -FilePath $V -ArgumentList "--owner agentA --pid $($tA.pid) capture `"$Out\longcap.png`" --after-frames 240 --timeout-ms 30000" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$Out\longcap.json"
  Start-Sleep -Milliseconds 700
  $sw = [Diagnostics.Stopwatch]::StartNew(); $st = J @('--owner', 'agentA', '--pid', "$($tA.pid)", 'status'); $sw.Stop()
  $statusMs = $sw.Elapsed.TotalMilliseconds
  $capStillRunning = -not $longCap.HasExited
  Check ($st.ok -and $capStillRunning -and $statusMs -lt 1000) "status on target A answers during A's long --after-frames capture" ("{0:N0} ms, capture still running={1}" -f $statusMs, $capStillRunning)
  $t0 = Get-Date
  foreach ($a in $agents) {
    $cp = Start-Process -FilePath 'pwsh' -ArgumentList @('-NoProfile', '-File', "`"$PSScriptRoot\_multi_client.ps1`"", '-V', "`"$V`"", '-TargetPid', "$($a.pid)", '-Owner', $a.o, '-Keys', $a.keys, '-Out', "`"$Out\client_$($a.o).json`"") -WindowStyle Hidden -PassThru
    $clients += $cp
  }
  foreach ($cp in $clients) { $cp.WaitForExit(120000) | Out-Null }
  $wall = ((Get-Date) - $t0).TotalSeconds
  $longCap.WaitForExit(30000) | Out-Null
  $lc = Get-Content -Raw "$Out\longcap.json" | ConvertFrom-Json
  Check ($lc.ok -and ($lc.captured_present - $lc.present_before) -ge 240) "the long capture on A completed after >= 240 frames" "present $($lc.present_before)->$($lc.captured_present)"
  Start-Sleep -Milliseconds 400
  $stats = @()
  foreach ($a in $agents) {
    $cs = Get-Content -Raw "$Out\client_$($a.o).json" | ConvertFrom-Json; $stats += $cs
    Check ($cs.failures -eq 0 -and $cs.commands -eq 20) "$($a.o): 20 parallel key commands, 0 failures" ("p50 {0} ms, p95 {1} ms, max {2} ms" -f $cs.p50_ms, $cs.p95_ms, $cs.max_ms)
    $log = Get-Content -Raw -LiteralPath $a.log
    $seen = @([regex]::Matches($log, 'WM_KEYDOWN vk=0x([0-9A-F]{2})') | ForEach-Object { [char][Convert]::ToInt32($_.Groups[1].Value, 16) } | ForEach-Object { "$_".ToLower() } | Sort-Object -Unique)
    $expected = @($a.keys.Split(',')); if ($a.o -eq 'agentB') { $expected += 'u', 'v' }
    $expected = @($expected | Sort-Object -Unique)
    Check ((Compare-Object $seen $expected).Count -eq 0) "$($a.o) target saw exactly its own keys" ("seen=" + ($seen -join '') + " expected=" + ($expected -join ''))
    $downs = ([regex]::Matches($log, 'WM_KEYDOWN vk=0x')).Count
    $want = 20 + $(if ($a.o -eq 'agentB') { 2 } else { 0 })
    Check ($downs -eq $want) "$($a.o) target: $want key-downs, none lost or duplicated" "got $downs"
    Check ($log -notmatch 'vk=0x5A') "$($a.o) target never received a refused 'z'"
    $s = J @('--pid', "$($a.pid)", 'status'); Check (-not $s.really_foreground) "$($a.o) target stayed in the background"
  }
  # Input commands to ONE target are serialized by design (ordered); the long capture runs on target A only, so
  # B/C/D must be unaffected while A's own input waits for its capture (that wait is the per-target mutex).
  $others = @($stats | Where-Object { $_.owner -ne 'agentA' })
  $p95 = ($others | Measure-Object -Property p95_ms -Maximum).Maximum
  Check ($p95 -lt 1500) "targets B/C/D unaffected by A's long capture: worst p95 under 1.5 s" ("{0} ms, wall {1:N1} s" -f $p95, $wall)
  $aStat = $stats | Where-Object { $_.owner -eq 'agentA' }
  Check ($aStat.failures -eq 0) "target A's own input is ordered behind its capture, none lost" ("A max {0} ms (= the capture's frame wait)" -f $aStat.max_ms)
} finally {
  foreach ($cp in $clients) { if (-not $cp.HasExited) { Stop-Process -Id $cp.Id -Force -Confirm:$false } }
  if ($longCap -and -not $longCap.HasExited) { Stop-Process -Id $longCap.Id -Force -Confirm:$false }
  foreach ($p in $procs) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
}
$failed = @($checks | Where-Object { -not $_.pass })
[pscustomobject]@{ok = ($failed.Count -eq 0); checks = $checks.ToArray(); clients = $stats} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$Out\result.json"
if ($failed.Count) { throw "FAIL $($failed.Count) multi-agent checks; see $Out\result.json" }
"PASS multi-agent: 4 targets, 4 parallel clients, ownership + no cross-talk + no starvation ($($checks.Count) checks)"
