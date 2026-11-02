param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\capturetest")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
$p=$null; $waiter=$null
try {
  $p=Start-Process -FilePath "$D\vi_testinput.exe" -ArgumentList "`"$Out\target.log`" -28000 600 640 360" -WindowStyle Hidden -PassThru
  $r=& $V inject --pid $p.Id | ConvertFrom-Json; if(-not $r.ok){throw 'Injection failed'}
  Start-Sleep -Milliseconds 400
  & $V --pid $p.Id key m | Out-Null
  $capture=& $V --pid $p.Id capture "$Out\after.png" --after-frames 4 | ConvertFrom-Json
  $capture | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$Out\capture.json"
  if(-not $capture.ok -or $capture.after_frames -ne 4 -or ($capture.captured_present-$capture.present_before) -lt 4){throw 'Present-count capture unsupported or failed'}
  $crop=& $V --pid $p.Id capture "$Out\crop.png" --region '20,30,40,25' | ConvertFrom-Json
  if(-not $crop.ok -or $crop.w -ne 40 -or $crop.h -ne 25 -or $crop.origin_x -ne 20 -or $crop.origin_y -ne 30){throw 'Capture region failed'}
  $waiter=Start-Process -FilePath $V -ArgumentList "--pid $($p.Id) wait-change --region 0,0,100,100 --timeout 4" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$Out\changed.json" -RedirectStandardError "$Out\wait.stderr"
  Start-Sleep -Milliseconds 400
  & $V --pid $p.Id key n | Out-Null
  if(-not $waiter.WaitForExit(7000)){throw 'wait-change blocked input or timed out'}
  $changed=Get-Content -LiteralPath "$Out\changed.json" -Raw | ConvertFrom-Json
  if(-not $changed.ok -or -not $changed.changed -or $changed.hash -eq $changed.before_hash){throw 'wait-change failed positive control'}
  $negative=& $V --pid $p.Id wait-change --timeout-ms 250 | ConvertFrom-Json
  if($negative.ok -or $negative.changed -ne $false){throw 'wait-change negative control failed'}
  $negative | ConvertTo-Json | Set-Content -LiteralPath "$Out\unchanged.json"
  # wait-change/frame-hash must not redefine --capture-space (regression: it once mapped 10,10 to 310,210).
  $full=& $V --pid $p.Id capture "$Out\full.png" | ConvertFrom-Json
  & $V --pid $p.Id wait-change --region 300,200,100,100 --timeout-ms 200 | Out-Null
  $mapped=& $V --pid $p.Id click left 10 10 --capture-space | ConvertFrom-Json
  $expectX=[math]::Round(10*$full.client_w/$full.w); $expectY=[math]::Round(10*$full.client_h/$full.h)
  if(-not $mapped.ok -or $mapped.x -ne $expectX -or $mapped.y -ne $expectY){throw "capture-space changed by wait-change: got $($mapped.x),$($mapped.y) expected $expectX,$expectY"}
  'PASS Present-count capture, client region, nonblocking wait-change and timeout control'
} finally {
  if($waiter -and -not $waiter.HasExited){Stop-Process -Id $waiter.Id -Force -Confirm:$false}
  if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false}
}
