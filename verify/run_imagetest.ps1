# find-image / click-image / wait-image (NCC, multi-scale) against the synthetic menu target.
param([string]$Out = "$PSScriptRoot\..\build\test\image")
. "$PSScriptRoot\_ui_common.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$a = $null; $b = $null; $c = $null; $e = $null
try {
  $a = Start-UiTarget $Out 'icon1'
  $cap = Vi $a.Pid @('capture', "$Out\icon.png", '--region', '600,110,64,64')
  Require ($cap.ok -and $cap.w -eq 64) 'icon template captured from the live target'
  $r = Vi $a.Pid @('find-image', "$Out\icon.png")
  $m = $r.matches[0]
  Require ($r.ok -and $r.match_count -eq 1 -and [math]::Abs($m.box[0] - 600) -le 2 -and [math]::Abs($m.box[1] - 110) -le 2 -and $m.score -gt 0.95) 'find-image finds the icon at its exact position with score > 0.95'
  Require ([math]::Abs($m.scale - 1.0) -lt 0.001) 'same-size match reports scale 1'
  $r = Vi $a.Pid @('click-image', "$Out\icon.png")
  $clicks = Get-Clicks $a
  Require ($r.ok -and $r.clicked -and $clicks.Count -eq 1 -and $clicks[0].Name -eq 'ICON' -and (In-Rect $clicks[0].X $clicks[0].Y 600 110 664 174)) 'click-image clicks the icon (target logged ICON)'
  # flat template refused
  $null = Vi $a.Pid @('capture', "$Out\flat.png", '--region', '700,300,40,40')
  $f = & $V --pid $a.Pid find-image "$Out\flat.png" | ConvertFrom-Json
  Require ((-not $f.ok) -and $f.error -match 'no texture') 'a flat template is refused with a clear error'
  # two identical buttons -> ambiguity and --nth
  $null = Vi $a.Pid @('capture', "$Out\battle.png", '--region', '60,340,200,60')
  $n = (Get-Clicks $a).Count
  $r = & $V --pid $a.Pid click-image "$Out\battle.png" --threshold 0.9 | ConvertFrom-Json
  Require ((-not $r.ok) -and $r.ambiguous -and $r.match_count -eq 2 -and (Get-Clicks $a).Count -eq $n) 'two identical buttons: click-image refuses and clicks nothing'
  $r = Vi $a.Pid @('click-image', "$Out\battle.png", '--threshold', '0.9', '--nth', '2')
  $clicks = Get-Clicks $a
  Require ($r.ok -and $clicks[-1].Name -eq 'BATTLE' -and (In-Rect $clicks[-1].X $clicks[-1].Y 540 340 740 400)) '--nth 2 clicks the right-hand button'
  # absence: leave the main menu, the icon disappears
  $null = Vi $a.Pid @('click-text', 'PLAY')
  $null = Vi $a.Pid @('wait-text', 'Select a game type', '--timeout', '8000')
  $r = & $V --pid $a.Pid find-image "$Out\icon.png" | ConvertFrom-Json
  Require ((-not $r.ok) -and $r.match_count -eq 0) 'icon not on the SELECT screen: find-image reports no match (negative control)'
  $t0 = Get-Date
  $r = & $V --pid $a.Pid wait-image "$Out\icon.png" --timeout 1200 | ConvertFrom-Json
  $ms = ((Get-Date) - $t0).TotalMilliseconds
  Require ((-not $r.ok) -and $r.timed_out -and $ms -ge 1100 -and $ms -lt 8000) 'wait-image times out correctly when the template never appears'
  $r = Vi $a.Pid @('wait-image', "$Out\icon.png", '--gone', '--timeout', '3000')
  Require ($r.ok -and $r.gone) 'wait-image --gone succeeds when the icon is absent'
  $null = Vi $a.Pid @('click-text', 'BACK')
  $r = Vi $a.Pid @('wait-image', "$Out\icon.png", '--timeout', '8000')
  Require ($r.ok -and $r.found) 'wait-image sees the icon again after BACK'
  Stop-UiTarget $a; $a = $null

  # ---- scale tolerance ----
  $b = Start-UiTarget $Out 'icon11' @('--icon-scale', '1.1')
  $r = Vi $b.Pid @('find-image', "$Out\icon.png")
  $m = $r.matches[0]
  Require ($r.ok -and $m.scale -gt 1.0 -and $m.score -gt 0.85 -and (In-Rect $m.center[0] $m.center[1] 600 110 670 180)) 'a 1.1x scaled icon is found by the default scale list (scale > 1 reported)'
  $r = & $V --pid $b.Pid find-image "$Out\icon.png" --no-scale --threshold 0.95 | ConvertFrom-Json
  Require (-not $r.ok) 'with --no-scale at 0.95 the 1.1x icon is NOT found (documented limit)'
  $r = Vi $b.Pid @('click-image', "$Out\icon.png")
  $clicks = Get-Clicks $b
  Require ($r.ok -and $clicks[-1].Name -eq 'ICON') 'click-image hits the 1.1x icon'
  Stop-UiTarget $b; $b = $null
  $c = Start-UiTarget $Out 'icon16' @('--icon-scale', '1.6')
  $r = & $V --pid $c.Pid find-image "$Out\icon.png" | ConvertFrom-Json
  Require (-not $r.ok) 'a 1.6x icon is outside the default scale range and is not found (documented limit)'
  $r = Vi $c.Pid @('find-image', "$Out\icon.png", '--scales', '1.5,1.6,1.7')
  Require ($r.ok -and $r.matches[0].scale -ge 1.5) '--scales 1.5,1.6,1.7 widens the range and finds it'
  Stop-UiTarget $c; $c = $null

  # ---- wait for a load screen by image ----
  $e = Start-UiTarget $Out 'iconload' @('--load-ms', '6000')
  $r = Vi $e.Pid @('wait-image', "$Out\icon.png", '--timeout', '30000')
  Require ($r.ok -and $r.found -and $r.elapsed_ms -ge 800) 'wait-image blocks through the load screen until the icon appears'
  'PASS image'; $global:LASTEXITCODE = 0
} finally { Stop-UiTarget $a; Stop-UiTarget $b; Stop-UiTarget $c; Stop-UiTarget $e }
