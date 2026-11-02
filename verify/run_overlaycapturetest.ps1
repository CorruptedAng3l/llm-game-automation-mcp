# Capture of a DDraw-wrapper style game: the game window stays black, the picture is in a separate
# "DDrawCompatPresentationWindow" (scaled 2x, centred). Regression: capture used to return an all-black frame.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Exe='vi_overlaytarget.exe')
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V=(Resolve-Path "$CliDir\vinput.exe").Path
$Out="$PSScriptRoot\..\build\test\overlaycapture"; New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
$p=$null
function Px($x,$y){ $r=& $V --pid $p.Id pixel $x $y | ConvertFrom-Json; if(-not $r.ok){throw "pixel $x $y failed"}; $r }
function Near($c,$r,$g,$b){ [math]::Abs($c.r-$r) -le 12 -and [math]::Abs($c.g-$g) -le 12 -and [math]::Abs($c.b-$b) -le 12 }
try {
  $p=Start-Process -FilePath "$D\$Exe" -WindowStyle Hidden -PassThru
  $ok=$false; for($i=0;$i -lt 50;$i++){ $r=& $V inject --pid $p.Id | ConvertFrom-Json; if($r.ok){$ok=$true;break}; Start-Sleep -Milliseconds 100 }
  if(-not $ok){throw 'Injection failed'}
  Start-Sleep -Milliseconds 500
  $sw=[Diagnostics.Stopwatch]::StartNew()
  $c=& $V --pid $p.Id capture "$Out\full.png" | ConvertFrom-Json
  $ms=$sw.ElapsedMilliseconds
  if(-not $c.ok){throw "capture failed: $($c | ConvertTo-Json -Compress)"}
  if($c.w -ne 320 -or $c.h -ne 240){throw "capture size $($c.w)x$($c.h), expected 320x240"}
  if(-not (Near (Px 100 200) 220 20 20)){throw 'left half is not red: overlay window not captured (black frame)'}
  if(-not (Near (Px 250 200) 20 20 220)){throw 'right half is not blue'}
  if(-not (Near (Px 10 10) 20 220 20)){throw 'green marker not at the top-left: aspect-fit/centre mapping wrong'}
  if(-not (Near (Px 145 120) 220 20 20)){throw 'stale yellow square from the game window leaked into the capture'}
  if(-not (Near (Px 100 100) 220 20 20)){throw 'red region wrong below the marker'}
  if($c.capture_path -ne 'printwindow_overlay'){throw "capture_path $($c.capture_path)"}
  if($ms -gt 2500){throw "capture took $ms ms: waited for a swapchain frame that never comes"}
  "PASS overlay-window capture (red/blue/green at the right client pixels, ${ms} ms)"
} finally { if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false} }
