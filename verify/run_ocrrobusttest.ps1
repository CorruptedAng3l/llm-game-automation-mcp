# Real OCR on synthetic pixels. --BaselineDir points at an old CLI/DLL copy; targets are always the current fixture.
param([string]$CliDir="$PSScriptRoot\..\dist", [string]$BaselineDir='', [string]$Out="$PSScriptRoot\..\build\test\_out\ocr_robust")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
$checks=@(); $owned=@(); $evidence=@()
function Check($yes,$name) { $script:checks += [pscustomobject]@{pass=[bool]$yes;name=$name}; "$(if($yes){'PASS'}else{'FAIL'}) $name" }
function Call($pidValue,[string[]]$argv,[string]$cli=$V) { (& $cli --json --pid $pidValue @argv | ConvertFrom-Json) }
function StartTarget($exe,$name,$style=0,$width=800) {
  $p=Start-Process -FilePath "$D\$exe" -ArgumentList "`"$Out\$name.log`" -28000 600 --stylised $style --canvas-width $width" -WindowStyle Hidden -PassThru
  $script:owned += $p
  $i=& $V --json inject --pid $p.Id | ConvertFrom-Json
  if(-not $i.ok){throw 'synthetic injection failed'}
  for($j=0;$j -lt 80;$j++) { $s=Call $p.Id @('status'); if($s.frames -gt 2){break}; Start-Sleep -Milliseconds 100 }
  if($s.really_foreground){throw 'synthetic target became foreground'}
  return $p
}
try {
  foreach($exe in @('vi_uitarget.exe','vi_uitarget32.exe')) {
    $p=StartTarget $exe "$exe.plain"
    $r=Call $p.Id @('wait-text','--any','MISSING LABEL|PLAY','--timeout','0')
    Check ($r.ok -and $r.found -and $r.match.query -eq 'PLAY' -and $r.match_count -eq 1) "$exe --any selects a visible alternative"
    $r=Call $p.Id @('wait-text','--any','PLAY|play','--timeout','0')
    Check ($r.ok -and $r.match_count -eq 1) "$exe alternatives sharing an OCR span are deduplicated"
    $r=Call $p.Id @('wait-text','--any','NO SUCH LABEL|MAIN MENU','--gone','--timeout','0','--preprocess','none','--scale','1','--no-tiles')
    Check (-not $r.ok -and $r.match_count -eq 1) "$exe --gone waits until every alternative is absent"
    $r=Call $p.Id @('wait-text','--any','NO SUCH LABEL|ABSENT LABEL','--gone','--timeout','0','--preprocess','none','--scale','1','--no-tiles')
    Check ($r.ok -and $r.gone) "$exe --gone succeeds when all alternatives are absent"
    $r=Call $p.Id @('click-text','BATTIE','--exact','--timeout','0','--preprocess','none','--scale','1','--no-tiles','--dry-run')
    $evidence += [pscustomobject]@{target=$exe;test='nearest';reply=$r}
    Check (-not $r.ok -and $r.nearest_candidates.Count -gt 0 -and $r.nearest_candidates.Count -le 5 -and $r.nearest_candidates[0].text -eq 'BATTLE' -and $r.nearest_candidates[0].edit_distance -eq 1) "$exe miss reports ranked actual OCR candidates"
    $r=Call $p.Id @('click-text','BATTIE','--exact','--optional','--timeout','0','--preprocess','none','--scale','1','--no-tiles','--dry-run')
    Check ($r.ok -and $r.skipped -and $null -ne $r.nearest_candidates -and $r.nearest_candidates[0].text -eq 'BATTLE') "$exe optional miss still reports what OCR read without clicking"
    $r=Call $p.Id @('click-text','BATTLE','--timeout','0','--preprocess','contrast','--scale','2','--dry-run')
    Check (-not $r.ok -and $r.ambiguous -and $r.match_count -eq 2) "$exe preprocessing preserves ambiguity safety"
    $r=Call $p.Id @('click-text','BATTLE','--nth','2','--timeout','0','--preprocess','threshold','--scale','3','--region','530,330,220,80','--dry-run')
    # The crop contains one BATTLE, so asking for #2 must fail and never click.
    Check (-not $r.ok -and $r.match_count -eq 1) "$exe transformed cropped search has one match, not an invented second match"
    $r=Call $p.Id @('click-text','BATTLE','--timeout','0','--preprocess','threshold','--scale','3','--region','530,330,220,80','--dry-run')
    Check ($r.ok -and $r.match.center[0] -gt 540 -and $r.match.center[0] -lt 740 -and $r.match.center[1] -gt 340 -and $r.match.center[1] -lt 400) "$exe 3x preprocessing maps crop coordinates to client pixels"
    foreach($bad in @('','a|','|b')) {
      $r=Call $p.Id @('wait-text','--any',$bad,'--timeout','0')
      Check (-not $r.ok) "$exe rejects empty --any alternative [$bad]"
    }
    $r=Call $p.Id @('ocr','--preprocess','unknown')
    Check (-not $r.ok) "$exe rejects unknown preprocessing mode"
    Stop-Process -Id $p.Id -Force -Confirm:$false
    $p=StartTarget $exe "$exe.wide" 0 2560
    $r=Call $p.Id @('ocr','--scale','5','--tiles')
    Check ($r.ok -and $r.w -eq 2560 -and $r.text -match 'BATTLE') "$exe oversized 5x full-frame OCR falls back to bounded tiles"
    $r=Call $p.Id @('ocr','--scale','5','--tiles','--region','0,30,2560,50')
    Check ($r.ok -and $r.text -match 'MAIN MENU') "$exe oversized thin text region is actually analyzed by tiles"
    $r=Call $p.Id @('assert-text','ABSENT','--scale','5','--preprocess','none','--no-tiles','--gone','--timeout','0')
    Check (-not $r.ok -and $r.error -match 'MaxImageDimension') "$exe skipped oversized OCR never proves text absent"
    $evidence += [pscustomobject]@{target=$exe;test='oversized absence refusal';reply=$r}
    Stop-Process -Id $p.Id -Force -Confirm:$false
    $improved=0; $baselineImproved=0
    foreach($style in @(1,2)) {
      $p=StartTarget $exe "$exe.style$style" $style
      $cap=Call $p.Id @('capture',"$Out\$exe.style$style.png")
      Check $cap.ok "$exe stylised $style fixture capture"
      foreach($query in @('BATTLE','RESUME GAME','OK','Tap any of your territories')) {
        $plain=Call $p.Id @('assert-text',$query,'--timeout','0','--preprocess','none','--scale','1','--no-tiles')
        $robust=Call $p.Id @('assert-text',$query,'--timeout','0','--no-tiles')
        if($query -eq 'OK'){$okMiss=$robust}
        $old=$null
        if($BaselineDir) { $old=Call $p.Id @('assert-text',$query,'--timeout','0','--no-tiles') (Join-Path $BaselineDir 'vinput.exe') }
        if(-not $plain.ok -and $robust.ok){$improved++}
        if($null -ne $old -and -not $old.ok -and $robust.ok){$baselineImproved++}
        $evidence += [pscustomobject]@{target=$exe;style=$style;query=$query;plain=$plain;robust=$robust;baseline=$old}
      }
      Check (-not $okMiss.ok -and $null -ne $okMiss.nearest_candidates -and $okMiss.nearest_candidates[0].text -eq '0K' -and $okMiss.nearest_candidates[0].edit_distance -eq 1) "$exe stylised $style short-label miss names actual 0K reading without accepting it"
      $fuzzy=Call $p.Id @('assert-text','OK','--region','40,210,240,80','--preprocess','contrast','--scale','2','--fuzzy','1','--no-tiles','--timeout','0')
      Check ($fuzzy.ok -and $fuzzy.match.text -eq '0K' -and $fuzzy.match.match_score -lt 1 -and $fuzzy.match.center[1] -gt 220 -and $fuzzy.match.center[1] -lt 280) "$exe stylised $style explicit regional fuzzy retry resolves 0K with honest textual score"
      $evidence += [pscustomobject]@{target=$exe;style=$style;query='OK regional fuzzy retry';robust=$fuzzy}
      Stop-Process -Id $p.Id -Force -Confirm:$false
    }
    Check ($improved -gt 0) "$exe fallback preprocessing/upscale recovers at least one stylised label missed by raw 1x"
    if($BaselineDir){Check ($baselineImproved -gt 0) "$exe new OCR recovers at least one stylised label missed by baseline CLI auto scales"}
  }
} finally {
  foreach($p in $owned){if(-not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false}}
  $checks | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$Out\checks.json"
  $evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$Out\evidence.json"
}
$failed=@($checks | Where-Object {-not $_.pass}).Count
if($failed){throw "FAIL OCR robustness: $failed / $($checks.Count) checks"}
"PASS OCR robustness $($checks.Count) checks, x64+x86; no real foreground/input"
$global:LASTEXITCODE=0
