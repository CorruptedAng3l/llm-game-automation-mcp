# Legacy DirectInput (dinput.dll, DirectInputCreateA): a synthetic reader (test\vi_dinput1test.cpp) polls the keyboard and
# mouse through the DirectInput 1-7 interfaces, which the dinput8 hooks never see. Requires virtual key and mouse-button
# input to arrive, in both polling styles (GetDeviceState, buffered GetDeviceData) and both arches.
# The target is a background window started and stopped by this script (by PID).
# -Vinput runs the same checks with another vinput.exe (negative control against an older build).
param([string]$Out = "$PSScriptRoot\..\build\test\dinput1test", [string]$Vinput = '', [switch]$KeepGoing)
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = if ($Vinput) { $Vinput } else { "$D\vinput.exe" }
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$script:fails = 0
function Require($c, $d) { if (-not $c) { if ($KeepGoing) { $script:fails++; Write-Host "FAIL $d"; return }; throw "FAIL $d" }; Write-Host "PASS $d" }
$cases = @(
  @{ n = 'state_x64';    exe = 'vi_dinput1test.exe';   a = '' },
  @{ n = 'buffered_x64'; exe = 'vi_dinput1test.exe';   a = '--buffered' },
  @{ n = 'state_x86';    exe = 'vi_dinput1test32.exe'; a = '' },
  @{ n = 'buffered_x86'; exe = 'vi_dinput1test32.exe'; a = '--buffered' },
  # --gated: a never-activated window that ignores input until WM_ACTIVATEAPP(TRUE) (Fallout's GNW95 loop). vinput must
  # post the activation itself (autoactivate) without the target ever becoming the real foreground.
  @{ n = 'gated_x64';    exe = 'vi_dinput1test.exe';   a = '--gated' },
  @{ n = 'gated_x86';    exe = 'vi_dinput1test32.exe'; a = '--gated' }
)
$owned = @()
try {
  foreach ($c in $cases) {
    $tl = "$Out\$($c.n).log"; Remove-Item -LiteralPath $tl -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath "$D\$($c.exe)" -ArgumentList "`"$tl`" $($c.a)" -PassThru; $owned += $p
    for ($i = 0; $i -lt 100 -and -not ((Test-Path $tl) -and (Select-String -LiteralPath $tl -Pattern READY -Quiet)); $i++) { Start-Sleep -Milliseconds 50 }
    Require ((Test-Path $tl) -and (Select-String -LiteralPath $tl -Pattern READY -Quiet)) "$($c.n) reader started"
    $r = & $V inject --pid $p.Id | ConvertFrom-Json; Require $r.ok "$($c.n) inject"
    Start-Sleep -Milliseconds 600
    if ($c.a -eq '--gated') {
      $st = & $V --pid $p.Id status | ConvertFrom-Json
      Require ((Select-String -LiteralPath $tl -Pattern 'ACTIVATEAPP 1' -Quiet) -and $st.counters.act_synth -ge 1) "$($c.n) background window was told it is active (act_synth=$($st.counters.act_synth))"
      Require (-not $st.really_foreground) "$($c.n) target is still not the real foreground"
    }
    $pre = @(Get-Content -LiteralPath $tl | Where-Object { $_ -match 'KEY|MOUSE' }).Count
    Require ($pre -eq 0) "$($c.n) control: no input before any command ($pre lines)"
    & $V --pid $p.Id key a --ms 150 | Out-Null
    & $V --pid $p.Id click left 200 100 --ms 150 | Out-Null
    Start-Sleep -Milliseconds 500
    $txt = Get-Content -LiteralPath $tl -Raw
    Require ($txt -match 'KEY dik=0x1E down' -and $txt -match 'KEY dik=0x1E up') "$($c.n) virtual key A reaches a DirectInput 1-7 keyboard (down and up)"
    Require ($txt -match 'MOUSE b0 down' -and $txt -match 'MOUSE b0 up') "$($c.n) virtual left button reaches a DirectInput 1-7 mouse"
    $tr = & $V --pid $p.Id trace key lbutton | ConvertFrom-Json
    Require ($tr.ok -and $tr.channels.DirectInputState.down_reads -gt 0) "$($c.n) trace key lbutton shows the DirectInput mouse reads ($($tr.channels.DirectInputState.down_reads) down reads)"
    Stop-Process -Id $p.Id -Force; Start-Sleep -Milliseconds 200
  }
} finally { foreach ($p in $owned) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } } }
if ($script:fails) { throw "$($script:fails) dinput1 check(s) failed" }
Write-Host "dinput1 suite OK"
