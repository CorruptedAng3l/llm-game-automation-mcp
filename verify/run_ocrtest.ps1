# ocr: Windows.Media.Ocr in the CLI process; boxes in client pixels on a synthetic menu target.
param([string]$Out = "$PSScriptRoot\..\build\test\ocr")
. "$PSScriptRoot\_ui_common.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$t = $null
try {
  $t = Start-UiTarget $Out 'ocr'
  $r = Vi $t.Pid @('ocr', '--tiles')
  $r | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$Out\ocr.json"
  Require ($r.ok -and $r.method -eq 'swapchain' -and $r.w -eq 800 -and $r.h -eq 450) 'ocr runs on a 800x450 swapchain capture'
  Require ($null -eq $r.ocr_confidence) 'ocr reports no invented confidence (API has none)'
  $find = { param($text) $r.lines | Where-Object { $_.text -eq $text } }
  $play = @(& $find 'PLAY')
  Require ($play.Count -eq 1) 'PLAY found exactly once'
  $c = $play[0].box; Require (In-Rect ($c[0] + $c[2] / 2) ($c[1] + $c[3] / 2) 60 110 260 170) 'PLAY box centre lies inside the PLAY button (client pixels)'
  $battle = @(& $find 'BATTLE')
  Require ($battle.Count -eq 2) 'both BATTLE labels found'
  $xs = $battle | ForEach-Object { $_.box[0] + $_.box[2] / 2 } | Sort-Object
  Require ($xs[0] -gt 60 -and $xs[0] -lt 260 -and $xs[1] -gt 540 -and $xs[1] -lt 740) 'BATTLE boxes lie in the left and right buttons'
  Require (@(& $find 'MAIN MENU').Count -eq 1 -and $r.text.Contains('Load Game') -and $r.text.Contains('OPTIONS') -and $r.text.Contains('QUIT')) 'title, Load Game, OPTIONS and QUIT all read (tiled pass recovers dropped lines)'
  Require ($r.lines[0].words.Count -ge 1 -and $r.lines[0].words[0].box.Count -eq 4) 'word boxes present'
  # region: boxes stay in CLIENT pixels, not region-local pixels
  $reg = Vi $t.Pid @('ocr', '--region', '40,100,300,90')
  Require ($reg.ok -and $reg.origin_x -eq 40 -and $reg.origin_y -eq 100) 'region ocr reports its origin'
  $p2 = @($reg.lines | Where-Object { $_.text -eq 'PLAY' })
  Require ($p2.Count -eq 1 -and [math]::Abs($p2[0].box[0] - $play[0].box[0]) -le 3 -and [math]::Abs($p2[0].box[1] - $play[0].box[1]) -le 3) 'region ocr maps boxes back to client pixels (same PLAY box +-3 px)'
  # negative controls
  $empty = Vi $t.Pid @('ocr', '--region', '0,410,800,38')
  Require ($empty.ok -and $empty.line_count -eq 0) 'text-free region reads zero lines (positive control above proves the reader works)'
  $bad = & $V --pid $t.Pid ocr --region 700,400,200,200 | ConvertFrom-Json
  Require (-not $bad.ok) 'region outside the window is rejected'
  $lang = & $V --pid $t.Pid ocr --lang xx-QQ | ConvertFrom-Json
  Require (-not $lang.ok -and $lang.error) 'unsupported OCR language is reported, not ignored'
  'PASS ocr'; $global:LASTEXITCODE = 0
} finally { Stop-UiTarget $t }
