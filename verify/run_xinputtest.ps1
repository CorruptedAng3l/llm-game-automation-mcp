# XInput pad 0: a synthetic reader (test\vi_xinputtest.cpp) polls XInputGetState (and XInputGetStateEx with --ex)
# every ~2 ms and logs every change. For each case: record the baseline (before injection: no virtual state may appear),
# inject, then drive `vinput pad` and require the exact buttons / triggers / sticks to arrive and to be released.
# Targets are background windows started and stopped by this script (by PID).
# -Vinput runs the same checks with another vinput.exe (negative control against an older build).
param([string]$Out = "$PSScriptRoot\..\build\test\xinputtest", [string[]]$Only = @(), [string]$Vinput = '', [switch]$KeepGoing)
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = if ($Vinput) { $Vinput } else { "$D\vinput.exe" }
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$log = "$Out\xinputtest_log.txt"; Set-Content -LiteralPath $log -Value ''
function Note($m) { ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m) | Tee-Object -Append $log }
$script:fails = 0
function Require($c, $d) { if (-not $c) { if ($KeepGoing) { $script:fails++; Note "FAIL $d"; return }; throw "FAIL $d" }; Note "PASS $d" }
function Cmd([int]$TargetPid, [string[]]$C) { return (& $V --pid $TargetPid @C | ConvertFrom-Json) }
function States([string]$file) {
  $rx = '^(\d\d):(\d\d):(\d\d)\.(\d{3}) STATE src=(\w+) rc=(\d+) pkt=(\d+) b=0x([0-9A-F]{4}) lt=(\d+) rt=(\d+) lx=(-?\d+) ly=(-?\d+) rx=(-?\d+) ry=(-?\d+)'
  $i = 0
  foreach ($l in (Get-Content -LiteralPath $file)) {
    $m = [regex]::Match($l, $rx)
    if ($m.Success) {
      $g = $m.Groups
      [pscustomobject]@{ idx = $i; ms = ((([int]$g[1].Value * 60 + [int]$g[2].Value) * 60 + [int]$g[3].Value) * 1000 + [int]$g[4].Value)
        src = $g[5].Value; rc = [int]$g[6].Value; b = [Convert]::ToInt32($g[8].Value, 16); lt = [int]$g[9].Value; rt = [int]$g[10].Value
        lx = [int]$g[11].Value; ly = [int]$g[12].Value; rxv = [int]$g[13].Value; ry = [int]$g[14].Value }
      $i++
    }
  }
}
function Since([string]$file, [int]$from, [string]$src = 'std') { @(States $file | Where-Object { $_.idx -ge $from -and $_.src -eq $src }) }
function Count([string]$file) { @(States $file).Count }

$cases = @(
  @{ n='xinput1_4_ex_x64'; exe='vi_xinputtest.exe';   a='--dll xinput1_4.dll --ex' },
  @{ n='xinput9_1_0_x64';  exe='vi_xinputtest.exe';   a='--dll xinput9_1_0.dll' },
  @{ n='xinput1_4_ex_x86'; exe='vi_xinputtest32.exe'; a='--dll xinput1_4.dll --ex' }
)
if ($Only.Count) { $cases = $cases | Where-Object { $Only -contains $_.n } }
$owned = @()
try {
  foreach ($c in $cases) {
    $tl = "$Out\$($c.n)_target.log"
    Remove-Item -LiteralPath $tl -ErrorAction SilentlyContinue   # a stale log from the last run reads as this run's
    $p = Start-Process -FilePath "$D\$($c.exe)" -ArgumentList "`"$tl`" -28000 600 320 200 $($c.a)" -PassThru
    $owned += $p
    # the first XInputGetState can take seconds (device enumeration), measured >2 s right after the GPU suites
    for ($i = 0; $i -lt 160 -and -not ((Test-Path $tl) -and (Count $tl) -ge 1); $i++) { Start-Sleep -Milliseconds 50 }
    $base = @(States $tl)[0]
    Require ($null -ne $base) "$($c.n) reader started (baseline rc=$($base.rc), b=0x$('{0:X4}' -f $base.b))"
    $realPad = $base.rc -eq 0
    Note "$($c.n) INFO baseline: $(if ($realPad) { 'a real pad 0 is connected' } else { 'no real pad 0 (rc=' + $base.rc + ' = ERROR_DEVICE_NOT_CONNECTED)' })"
    $pre0 = Count $tl
    Start-Sleep -Milliseconds 300
    $pre = Count $tl
    Require ($pre -eq $pre0 -or $realPad) "$($c.n) control: no state change before injection ($pre0 -> $pre state lines)"
    $r = & $V inject --pid $p.Id | ConvertFrom-Json
    Require $r.ok "$($c.n) inject"
    $s = $null
    for ($i = 0; $i -lt 40; $i++) { $s = Cmd $p.Id @('status'); if ($s.counters.xinput -gt 0) { break }; Start-Sleep -Milliseconds 100 }
    Require ($s.counters.xinput -gt 0) "$($c.n) vinput sees the reader's XInput calls ($($s.counters.xinput))"

    # 1. buttons: press and auto-release
    $from = Count $tl
    $k = Cmd $p.Id @('pad', 'a+b+lb', '300')
    Start-Sleep -Milliseconds 100
    $st = Since $tl $from
    $on = @($st | Where-Object { $_.rc -eq 0 -and $_.b -eq 0x3100 })
    $off = @($st | Where-Object { $on.Count -and $_.idx -gt $on[0].idx -and ($_.b -band 0x3100) -eq 0 })
    Require ($k.ok -and $k.buttons -eq 0x3100 -and $on.Count -ge 1 -and $off.Count -ge 1) "$($c.n) pad a+b+lb: reader saw b=0x3100 then release (xinput_calls=$($k.xinput_calls))"
    Require ((($off[0].ms - $on[0].ms) -ge 250) -and (($off[0].ms - $on[0].ms) -le 600)) "$($c.n) held for the requested 300 ms (seen $($off[0].ms - $on[0].ms) ms)"

    # 2. every axis, including negative extremes and small values
    $from = Count $tl
    $k = Cmd $p.Id @('pad', '--lx', '-32768', '--ly', '32767', '--rx', '12345', '--ry', '-1', '--lt', '255', '--rt', '1', '300')
    Start-Sleep -Milliseconds 100
    $hit = @(Since $tl $from | Where-Object { $_.lx -eq -32768 -and $_.ly -eq 32767 -and $_.rxv -eq 12345 -and $_.ry -eq -1 -and $_.lt -eq 255 -and $_.rt -eq 1 -and $_.b -eq 0 })
    Require ($k.ok -and $k.axes_set_mask -eq 63 -and -not $k.clamped -and $hit.Count -ge 1) "$($c.n) sticks/triggers arrive exactly (lx -32768, ly 32767, rx 12345, ry -1, lt 255, rt 1)"
    $last = @(States $tl)[-1]
    Require ($last.lx -eq 0 -and $last.lt -eq 0) "$($c.n) axes released after the duration"

    # 3. out-of-range values are clamped, not wrapped (40000 used to become -25536)
    $from = Count $tl
    $k = Cmd $p.Id @('pad', '--lx', '40000', '--ry', '-40000', '--lt', '300', '200')
    Start-Sleep -Milliseconds 100
    $hit = @(Since $tl $from | Where-Object { $_.lx -eq 32767 -and $_.ry -eq -32768 -and $_.lt -eq 255 })
    Require ($k.ok -and $k.clamped -and $hit.Count -ge 1) "$($c.n) out-of-range values clamped (lx 40000->32767, ry -40000->-32768, lt 300->255)"

    # 4. a lone number after options is the duration ("pad --lx 20000 600" used to send buttons='600' and hold 100 ms)
    $from = Count $tl
    $k = Cmd $p.Id @('pad', '--lx', '20000', '600')
    Start-Sleep -Milliseconds 100
    $st = Since $tl $from
    $on = @($st | Where-Object { $_.lx -eq 20000 }); $off = @($st | Where-Object { $on.Count -and $_.idx -gt $on[0].idx -and $_.lx -eq 0 })
    Require ($k.ok -and $on.Count -and $off.Count -and ($off[0].ms - $on[0].ms) -ge 500) "$($c.n) 'pad --lx 20000 600' held 600 ms (seen $(if ($on.Count -and $off.Count) { $off[0].ms - $on[0].ms } else { 'n/a' }) ms)"

    # 5. --hold keeps the state until 'pad off', which hands pad 0 back to the real device
    $from = Count $tl
    $k = Cmd $p.Id @('pad', 'x', '--hold')
    Start-Sleep -Milliseconds 700
    $last = @(States $tl)[-1]
    Require ($k.ok -and $k.hold -and $last.b -eq 0x4000) "$($c.n) pad x --hold still held after 700 ms"
    $k = Cmd $p.Id @('pad', 'off')
    Start-Sleep -Milliseconds 150
    $last = @(States $tl)[-1]
    Require ($k.ok -and $last.rc -eq $base.rc -and ($realPad -or $last.b -eq 0)) "$($c.n) pad off: reader back to the real device state (rc=$($last.rc), baseline rc=$($base.rc))"

    # 6. unknown button names are an error, not a silent no-op
    $k = Cmd $p.Id @('pad', 'dpadup', '100')
    Require (-not $k.ok -and $k.error -match 'unknown button') "$($c.n) unknown button name rejected: $($k.error)"

    # 7. Guide: only through XInputGetStateEx, never through the documented XInputGetState
    if ($c.a -match '--ex') {
      $from = Count $tl
      $k = Cmd $p.Id @('pad', 'guide+a', '300')
      Start-Sleep -Milliseconds 100
      $ex = @(Since $tl $from 'ex' | Where-Object { $_.b -eq 0x1400 })
      $std = @(Since $tl $from 'std' | Where-Object { $_.b -eq 0x1000 })
      $leak = @(Since $tl $from 'std' | Where-Object { $_.b -band 0x400 })
      Require ($k.ok -and $ex.Count -ge 1 -and $std.Count -ge 1 -and $leak.Count -eq 0) "$($c.n) guide+a: XInputGetStateEx b=0x1400, XInputGetState b=0x1000 (Guide hidden)"
      Cmd $p.Id @('pad', 'off') | Out-Null
    }
    $st2 = Cmd $p.Id @('status')
    $xh = @(($st2.hooks_ok -split ' ') | Where-Object { $_ -like 'XInput*' })
    Require ([string]::IsNullOrWhiteSpace($st2.hooks_failed) -and $xh.Count -ge 1) "$($c.n) XInput hooks installed ($($xh -join ', ')), none failed; reader calls counted: $($st2.counters.xinput)"
    Stop-Process -Id $p.Id -Force -Confirm:$false
  }
  if ($script:fails) { Note "FAIL xinput suite: $($script:fails) checks failed"; exit 1 }
  Note 'PASS xinput suite'
} finally {
  foreach ($p in $owned) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
  Note 'owned targets stopped by PID'
}
