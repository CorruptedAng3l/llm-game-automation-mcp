# click-text / wait-text / assert-text against the synthetic menu target (clicks are logged with coordinates).
param([string]$Out = "$PSScriptRoot\..\build\test\text")
. "$PSScriptRoot\_ui_common.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$a = $null; $b = $null; $c = $null
try {
  # ---- A: the PLAY -> SOLO flow by text alone ----
  $a = Start-UiTarget $Out 'flow'
  $r = Vi $a.Pid @('click-text', 'PLAY')
  Require ($r.ok -and $r.clicked -and $r.match.text -eq 'PLAY' -and $r.match_count -eq 1) 'click-text PLAY clicks and reports the matched box'
  Require ($null -eq $r.ocr_confidence -and $r.match.match_score -eq 1) 'reply carries match_score and an honest null ocr_confidence'
  $clicks = Get-Clicks $a
  Require ($clicks.Count -eq 1 -and $clicks[0].Name -eq 'PLAY' -and (In-Rect $clicks[0].X $clicks[0].Y 60 110 260 170)) 'the target logged exactly one click, on the PLAY button'
  Require ($r.match.center[0] -eq $clicks[0].X -and $r.match.center[1] -eq $clicks[0].Y) 'reported centre equals the logged click coordinates'
  $w = Vi $a.Pid @('wait-text', 'Select a game type', '--timeout', '8000')
  Require ($w.ok -and $w.found -and $w.match_count -eq 1) 'wait-text sees the next screen'
  $r = Vi $a.Pid @('click-text', 'solo')
  Require ($r.ok) 'click-text is case-insensitive (solo -> SOLO)'
  Require (Wait-ScreenInLog $a 'GAME' 5) 'SOLO click switched the target to the GAME screen'
  $clicks = Get-Clicks $a
  Require ($clicks.Count -eq 2 -and $clicks[1].Name -eq 'SOLO') 'second click landed on SOLO'
  $w = Vi $a.Pid @('wait-text', 'Game started', '--timeout', '8000')
  Require ($w.ok) 'wait-text sees Game started'
  Stop-UiTarget $a; $a = $null

  # ---- B: ambiguity, --nth, whitespace, --exact, timeout ----
  $b = Start-UiTarget $Out 'menu'
  $before = (Get-Clicks $b).Count
  $r = & $V --pid $b.Pid click-text BATTLE | ConvertFrom-Json
  Require ((-not $r.ok) -and $r.ambiguous -and $r.match_count -eq 2 -and $r.candidates.Count -eq 2 -and $r.candidates[0].center[0] -lt $r.candidates[1].center[0]) 'ambiguous BATTLE is refused and both candidates are listed in reading order'
  Require ((Get-Clicks $b).Count -eq $before) 'the refusal clicked nothing'
  $r = Vi $b.Pid @('click-text', 'BATTLE', '--nth', '2')
  $clicks = Get-Clicks $b
  Require ($r.ok -and $clicks[-1].Name -eq 'BATTLE' -and (In-Rect $clicks[-1].X $clicks[-1].Y 540 340 740 400)) '--nth 2 clicks the RIGHT BATTLE button'
  $r = Vi $b.Pid @('click-text', 'BATTLE', '--nth', '1')
  $clicks = Get-Clicks $b
  Require ($r.ok -and $clicks[-1].Name -eq 'BATTLE' -and (In-Rect $clicks[-1].X $clicks[-1].Y 60 340 260 400)) '--nth 1 clicks the LEFT BATTLE button'
  $n = (Get-Clicks $b).Count
  $r = & $V --pid $b.Pid click-text BATTLE --nth 3 | ConvertFrom-Json
  Require ((-not $r.ok) -and $r.error -match 'only 2' -and (Get-Clicks $b).Count -eq $n) '--nth 3 of 2 is refused without clicking'
  $r = Vi $b.Pid @('click-text', 'LoadGame')
  $clicks = Get-Clicks $b
  Require ($r.ok -and $clicks[-1].Name -eq 'Load___Game') 'whitespace-insensitive: "LoadGame" clicks the "Load   Game" button'
  $r = Vi $b.Pid @('click-text', 'OPTIONS', '--exact')
  Require ($r.ok -and (Get-Clicks $b)[-1].Name -eq 'OPTIONS') '--exact with the right case clicks OPTIONS (label dropped by plain full-frame OCR, recovered by tiles)'
  $n = (Get-Clicks $b).Count
  $r = & $V --pid $b.Pid click-text options --exact --timeout 1200 | ConvertFrom-Json
  Require ((-not $r.ok) -and (Get-Clicks $b).Count -eq $n) '--exact is case-sensitive: "options" is not found and nothing is clicked'
  $t0 = Get-Date
  $r = & $V --pid $b.Pid click-text 'NO SUCH BUTTON' --timeout 1500 | ConvertFrom-Json
  $ms = ((Get-Date) - $t0).TotalMilliseconds
  Require ((-not $r.ok) -and $r.timed_out -and $r.ocr_saw -and $ms -ge 1400 -and $ms -lt 8000 -and (Get-Clicks $b).Count -eq $n) 'missing text times out after ~--timeout, lists what OCR saw, clicks nothing'
  $r = Vi $b.Pid @('click-text', 'QUIT', '--dry-run')
  Require ($r.ok -and -not $r.clicked -and $r.dry_run -and (Get-Clicks $b).Count -eq $n) '--dry-run reports the match without clicking'
  $r = Vi $b.Pid @('assert-text', 'MAIN MENU')
  Require ($r.ok) 'assert-text passes for present text'
  $t0 = Get-Date
  $r = & $V --pid $b.Pid assert-text 'Game started' | ConvertFrom-Json
  Require ((-not $r.ok) -and (((Get-Date) - $t0).TotalMilliseconds -lt 4000)) 'assert-text fails immediately for absent text'
  $r = Vi $b.Pid @('assert-text', 'Game started', '--absent')
  Require ($r.ok) 'assert-text --absent passes for absent text'
  Stop-UiTarget $b; $b = $null

  # ---- C: waiting for a load screen ----
  $c = Start-UiTarget $Out 'load' @('--load-ms', '7000')
  $w = Vi $c.Pid @('wait-text', 'Loading game', '--timeout', '5000')
  Require ($w.ok) 'wait-text sees the load screen'
  $w = Vi $c.Pid @('wait-text', 'MAIN MENU', '--timeout', '30000')
  Require ($w.ok -and $w.found -and $w.elapsed_ms -ge 800) 'wait-text MAIN MENU blocks until the load screen is gone (elapsed >= 800 ms)'
  Require ((Get-LogText $c) -match 'SCREEN MAIN') 'target log agrees: MAIN reached'
  $t0 = Get-Date
  $w = & $V --pid $c.Pid wait-text 'Loading game assets' --timeout 1500 | ConvertFrom-Json
  $ms = ((Get-Date) - $t0).TotalMilliseconds
  Require ((-not $w.ok) -and $w.timed_out -and $ms -ge 1400 -and $ms -lt 8000) 'wait-text for text that is not there times out correctly'
  $w = Vi $c.Pid @('wait-text', 'Loading game assets', '--gone', '--timeout', '3000')
  Require ($w.ok -and $w.gone) 'wait-text --gone succeeds once the text is absent'
  'PASS text'; $global:LASTEXITCODE = 0
} finally { Stop-UiTarget $a; Stop-UiTarget $b; Stop-UiTarget $c }
