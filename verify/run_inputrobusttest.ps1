# Synthetic render-sampled drags, directory creation, GDI capture latency and unambiguous pad holds.
# Use -CliDir with preserved baseline DLL/exe siblings for the same red control.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\inputrobust",[switch]$KeepGoing)
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null;$Out=(Resolve-Path $Out).Path
$checks=[Collections.Generic.List[object]]::new();$owned=@();$evidence=@()
function Check($ok,$name) { $checks.Add([pscustomobject]@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not $ok -and -not $KeepGoing){throw $name} }
function Vi($targetPid,[string[]]$a) { & $V --json --pid $targetPid @a | ConvertFrom-Json }
function Start-Target($exe,$log,$extra='') {
  $p=Start-Process -FilePath "$D\$exe" -ArgumentList "`"$log`" $extra" -WindowStyle Hidden -PassThru
  $script:owned+=$p
  $i=& $V --json inject --pid $p.Id | ConvertFrom-Json
  if(-not $i.ok){throw "inject $exe failed"}
  for($n=0;$n -lt 40;$n++){ $s=Vi $p.Id @('status');if($s.hwnd -ne '0x0'){break};Start-Sleep -Milliseconds 100 }
  Check (-not $s.really_foreground) "$exe stays background" | Out-Host
  return $p
}
try {
  foreach($arch in @('', '32')) {
    $gdi=Start-Target "vi_channelprobe$arch.exe" "$Out\gdi$arch.log"
    $clock=[Diagnostics.Stopwatch]::StartNew();$cap=Vi $gdi.Id @('capture',"$Out\gdi$arch.png");$clock.Stop()
    Check ($cap.ok -and $cap.method -eq 'printwindow') "GDI$arch auto capture PrintWindow positive"
    Check ($clock.ElapsedMilliseconds -lt 1000) "GDI$arch auto capture avoids absent Present timeout ($($clock.ElapsedMilliseconds)ms)"
    $nested="$Out\missing_$([guid]::NewGuid().ToString('N'))\child\shot.png"
    $missing=Vi $gdi.Id @('capture',$nested,'--method','printwindow')
    Check ($missing.ok -and (Test-Path -LiteralPath $nested)) "GDI$arch capture creates nested parent"
    $fileParent="$Out\file_parent$arch";Set-Content -LiteralPath $fileParent -Value 'not a directory'
    $invalid=Vi $gdi.Id @('capture',"$fileParent\shot.png",'--method','printwindow')
    Check (-not $invalid.ok -and $invalid.error -match 'parent directory') "GDI$arch file parent is a clear error"
    $noFrames=Vi $gdi.Id @('capture',"$Out\no_frame$arch.png",'--after-frames','1','--timeout-ms','100')
    Check (-not $noFrames.ok -and $noFrames.error -match 'Present') "GDI$arch after-frames still requires rendering"
    $native=Vi $gdi.Id @('capture',"$Out\native$arch.png",'--method','native','--timeout-ms','100')
    Check (-not $native.ok) "GDI$arch explicit native cannot silently fall back"
    $lastCapture=Vi $gdi.Id @('status')
    Check ($lastCapture.capture_path -eq 'printwindow') "GDI$arch status preserves last successful PrintWindow path after failures"
    $fallbackDrag=Vi $gdi.Id @('drag','80','100','320','100','--ms','0')
    Check ($fallbackDrag.ok -and $fallbackDrag.pacing -eq 'time_fallback') "GDI$arch drag explicitly reports timed fallback when no native frames exist"
    $ambiguous=Vi $gdi.Id @('pad','--lx','30000','--hold','1500')
    Vi $gdi.Id @('pad','none','1')|Out-Null
    Check (-not $ambiguous.ok -and $ambiguous.error -match 'hold') "pad$arch rejects duration after indefinite --hold"
    $raw=Vi $gdi.Id @('json','{"cmd":"pad","lx":30000,"hold":true,"ms":1500}')
    Vi $gdi.Id @('pad','none','1')|Out-Null
    Check (-not $raw.ok -and $raw.error -match 'hold') "pad$arch JSON rejects hold plus ms"
    # A rejection must also leave the previous state intact; the return value alone
    # would miss a parser that reports an error after updating the stick.
    $padLog="$Out\pad$arch.log"
    $padTarget=Start-Target "vi_xinputtest$arch.exe" $padLog '-28000 600 320 200 --dll xinput1_4.dll --ex'
    $held=Vi $padTarget.Id @('pad','--lx','7000','--hold')
    for($n=0;$n -lt 50;$n++){ $states=@(Select-String -LiteralPath $padLog -Pattern 'STATE.* lx=').Line;if($states.Count -and $states[-1] -match ' lx=7000 '){break};Start-Sleep -Milliseconds 50 }
    Check ($held.ok -and $states.Count -and $states[-1] -match ' lx=7000 ') "pad$arch reader sees positive held-stick control at7000"
    $badRaw=Vi $padTarget.Id @('json','{"cmd":"pad","lx":30000,"hold":true,"ms":1500}')
    Start-Sleep -Milliseconds 150;$states=@(Select-String -LiteralPath $padLog -Pattern 'STATE.* lx=').Line
    Check (-not $badRaw.ok -and $states.Count -and $states[-1] -match ' lx=7000 ') "pad$arch rejected raw duration preserves previous held stick"
    $badCli=Vi $padTarget.Id @('pad','--lx','30000','--hold','1500')
    Start-Sleep -Milliseconds 150;$states=@(Select-String -LiteralPath $padLog -Pattern 'STATE.* lx=').Line
    Check (-not $badCli.ok -and $states.Count -and $states[-1] -match ' lx=7000 ') "pad$arch rejected CLI duration preserves previous held stick"
    $booleanCases=@()
    foreach($inline in @('--hold=1500','--hold=1')) {
      $badInline=Vi $padTarget.Id @('pad','--lx','30000',$inline)
      Start-Sleep -Milliseconds 80;$states=@(Select-String -LiteralPath $padLog -Pattern 'STATE.* lx=').Line
      Check (-not $badInline.ok -and $states.Count -and $states[-1] -match ' lx=7000 ') "pad$arch rejects numeric $inline without changing held stick"
      $booleanCases+=[pscustomobject]@{form=$inline;reply=$badInline;reader_last=$states[-1]}
    }
    foreach($numeric in @('1500','1')) {
      $badNumeric=Vi $padTarget.Id @('json',('{"cmd":"pad","lx":30000,"hold":'+$numeric+'}'))
      Start-Sleep -Milliseconds 80;$states=@(Select-String -LiteralPath $padLog -Pattern 'STATE.* lx=').Line
      Check (-not $badNumeric.ok -and $states.Count -and $states[-1] -match ' lx=7000 ') "pad$arch rejects raw numeric hold=$numeric without changing held stick"
      $booleanCases+=[pscustomobject]@{form="raw hold=$numeric";reply=$badNumeric;reader_last=$states[-1]}
    }
    $falseFlag=Vi $padTarget.Id @('pad','--lx','30000','--hold=false','30')
    Check ($falseFlag.ok -and -not $falseFlag.hold) "pad$arch --hold=false accepts the timed positional duration"
    $evidence+=[pscustomobject]@{arch=$arch;strict_boolean_cases=$booleanCases;explicit_false_timed=$falseFlag}
    Vi $padTarget.Id @('pad','none','1')|Out-Null;Vi $padTarget.Id @('pad','off')|Out-Null
    foreach($key in @('lbutton','rbutton','mbutton','xbutton1','xbutton2')) {
      $down=Vi $gdi.Id @('down',$key);$state=Vi $gdi.Id @('status');$up=Vi $gdi.Id @('up',$key)
      Check ($down.ok -and $up.ok -and $state.virtual_down.Count -gt 0) "$arch named mouse $key remains held between commands"
    }
    # Cache coverage is behavioral: hold an extended and ordinary key, then observe the exact DI slots.
    Vi $gdi.Id @('down','w+insert+rctrl')|Out-Null;Start-Sleep -Milliseconds 150
    $traceW=Vi $gdi.Id @('trace','key','w');$traceInsert=Vi $gdi.Id @('trace','key','insert');$traceCtrl=Vi $gdi.Id @('trace','key','rctrl')
    Vi $gdi.Id @('up','w+insert+rctrl')|Out-Null
    Check ($traceW.channels.DirectInputState.down_reads -gt 0 -and $traceInsert.channels.DirectInputState.down_reads -gt 0 -and $traceCtrl.channels.DirectInputState.down_reads -gt 0) "DI$arch cached ordinary/extended mappings are read down"
    $evidence+=[pscustomobject]@{arch=$arch;auto_capture=$cap;elapsed_ms=$clock.ElapsedMilliseconds;missing_parent=$missing;invalid_parent=$invalid;no_frames=$noFrames;native=$native;pad=$ambiguous;pad_json=$raw;trace_w=$traceW;trace_insert=$traceInsert;trace_rctrl=$traceCtrl}
    # Red control runs once the new fixture has been built; it needs no new product DLL functionality.
    if(Test-Path -LiteralPath "$D\vi_dragtarget$arch.exe") {
      $dragLog="$Out\drag$arch.log";$drag=Start-Target "vi_dragtarget$arch.exe" $dragLog
      for($n=0;$n -lt 40;$n++){$s=Vi $drag.Id @('status');if($s.frames -gt 3){break};Start-Sleep -Milliseconds 100}
      $r=Vi $drag.Id @('drag','80','100','320','100','--ms','0')
      Start-Sleep -Milliseconds 120
      $samples=@(Get-Content -LiteralPath $dragLog | Where-Object {$_ -match 'FRAME_SAMPLE (160|240|320) 100'})
      $positions=@($samples | ForEach-Object {if($_ -match 'FRAME_SAMPLE (\d+)'){[int]$Matches[1]}} | Sort-Object -Unique)
      Check ($r.ok -and $r.pacing -eq 'frames' -and $positions.Count -ge 3) "drag$arch three fast steps each sampled at a rendered frame ($($positions -join ','))"
      $stallLog="$Out\stall$arch.log";$stall=Start-Target "vi_dragtarget$arch.exe" $stallLog '--pause-on-down'
      for($n=0;$n -lt 40;$n++){$s=Vi $stall.Id @('status');if($s.frames -gt 3){break};Start-Sleep -Milliseconds 100}
      $watch=[Diagnostics.Stopwatch]::StartNew();$stalled=Vi $stall.Id @('drag','80','100','320','100','--ms','0','--frame-timeout-ms','250');$watch.Stop()
      $s=Vi $stall.Id @('status')
      Check (-not $stalled.ok -and $watch.ElapsedMilliseconds -lt 1800 -and 1 -notin $s.virtual_down) "drag$arch stalled render errors promptly and releases left button ($($watch.ElapsedMilliseconds)ms)"
      $evidence+=[pscustomobject]@{arch=$arch;drag=$r;sampled_positions=$positions;stalled=$stalled;stall_elapsed_ms=$watch.ElapsedMilliseconds;after_stall=$s}
    } else { Check $false "drag fixture missing for $arch; build both arches" }
  }
} finally {
  foreach($p in $owned){if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false}}
  $checks|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$Out\checks.json"
  $evidence|ConvertTo-Json -Depth 14|Set-Content -LiteralPath "$Out\evidence.json"
}
$failed=@($checks | Where-Object {-not $_.pass}).Count
"$($checks.Count-$failed) PASS / $failed FAIL"
if($failed){exit 1}
$global:LASTEXITCODE=0
