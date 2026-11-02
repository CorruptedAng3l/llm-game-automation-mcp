# pixel / scroll / drag / type against the synthetic menu target (which logs WM_MOUSEMOVE, wheel and WM_CHAR).
param([string]$Out = "$PSScriptRoot\..\build\test\small")
. "$PSScriptRoot\_ui_common.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$t = $null
try {
  $t = Start-UiTarget $Out 'small'
  # ---- pixel ----
  $p = Vi $t.Pid @('pixel', '10', '10')
  Require ($p.ok -and [math]::Abs($p.r - 18) -le 2 -and [math]::Abs($p.g - 28) -le 2 -and [math]::Abs($p.b - 56) -le 2 -and $p.hex -match '^#[0-9A-F]{6}$') 'pixel reads the background colour (18,28,56)'
  $p = Vi $t.Pid @('pixel', '70', '120')
  Require ($p.ok -and [math]::Abs($p.r - 40) -le 2 -and [math]::Abs($p.g - 140) -le 2 -and [math]::Abs($p.b - 60) -le 2) 'pixel reads the PLAY button green (40,140,60)'
  $bad = & $V --pid $t.Pid pixel 900 10 | ConvertFrom-Json
  Require (-not $bad.ok) 'pixel outside the client area is an error, not a colour'
  # ---- scroll ----
  $r = Vi $t.Pid @('scroll', '3', '400', '300')
  Require ($r.ok -and $r.notches -eq 3) 'scroll 3 at a point is accepted'
  $r = Vi $t.Pid @('scroll', '-2')
  Require ($r.ok) 'scroll -2 at the current virtual cursor'
  $log = Get-LogText $t
  $up = ([regex]::Matches($log, 'WHEEL 120 400 300')).Count; $down = ([regex]::Matches($log, 'WHEEL -120 400 300')).Count
  Require ($up -eq 3 -and $down -eq 2) 'target received 3 wheel notches up and 2 down, all at (400,300)'
  $bad = & $V --pid $t.Pid scroll 3 100 | ConvertFrom-Json
  Require (-not $bad.ok) 'scroll with X but no Y is rejected'
  # ---- drag ----
  $r = Vi $t.Pid @('drag', '100', '300', '300', '380', '--ms', '400')
  Require ($r.ok -and $r.steps -ge 3) 'drag completes'
  $log = Get-LogText $t
  $moves = @(); foreach ($l in ($log -split "`n")) { if ($l -match 'MOVE (-?\d+) (-?\d+) btn=1') { $moves += [pscustomobject]@{ X = [int]$Matches[1]; Y = [int]$Matches[2] } } }
  Require ($log -match 'CLICK 100 300 none') 'drag pressed the button at the start point'
  Require ($moves.Count -ge 3) "drag produced $($moves.Count) intermediate moves with the button held"
  $mono = $true; for ($i = 1; $i -lt $moves.Count; $i++) { if ($moves[$i].X -lt $moves[$i - 1].X -or $moves[$i].Y -lt $moves[$i - 1].Y) { $mono = $false } }
  Require ($mono -and $moves[0].X -ge 100 -and $moves[-1].X -le 300 -and $moves[-1].Y -le 380) 'held moves progress monotonically from start toward end'
  Require ($log -match 'UP 300 380') 'drag released the button at the end point'
  # ---- type ----
  $text = "h" + [char]0x00E9 + "llo " + [char]0x65E5 + [char]0x672C + " " + [char]::ConvertFromUtf32(0x1F600)
  $r = Vi $t.Pid @('type', $text, '--delay', '5')
  Require ($r.ok) 'type accepts unicode text'
  $log = Get-LogText $t
  $codes = @(); foreach ($l in ($log -split "`n")) { if ($l -match 'CHAR U\+([0-9A-F]{4})') { $codes += $Matches[1] } }
  $expect = @('0068', '00E9', '006C', '006C', '006F', '0020', '65E5', '672C', '0020', 'D83D', 'DE00')
  Require (($codes -join ',') -eq ($expect -join ',')) "WM_CHAR sequence is exact, incl. surrogate pair: $($codes -join ',')"
  'PASS small'; $global:LASTEXITCODE = 0
} finally { Stop-UiTarget $t }
