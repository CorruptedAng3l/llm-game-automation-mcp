param([string]$Out="$PSScriptRoot\..\build\test\captureoutputtest",[string]$LegacyDllDir='',[switch]$ExpectRed)
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$D="$repo\dist";$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force -Path $Out,"$repo\build\temp"|Out-Null;$env:TEMP="$repo\build\temp";$env:TMP=$env:TEMP
Add-Type -AssemblyName System.Drawing
$checks=[Collections.Generic.List[object]]::new();$proofs=[Collections.Generic.List[object]]::new()
function Check([bool]$Condition,[string]$Name){$checks.Add([pscustomobject]@{pass=$Condition;name=$Name});if($Condition){"PASS $Name"}else{"FAIL $Name"}}
function ImageInfo([string]$Path){$image=[Drawing.Bitmap]::new($Path);try{$pixel=$image.GetPixel(5,5);[pscustomobject]@{w=$image.Width;h=$image.Height;r=$pixel.R;g=$pixel.G;b=$pixel.B}}finally{$image.Dispose()}}
foreach($arch in @('','32')){
 $dir="$Out\case$arch";New-Item -ItemType Directory -Force -Path $dir|Out-Null;$v="$D\vinput$arch.exe";$target=$null;$burst=$null
 try{
  $target=Start-Process "$D\vi_uitarget$arch.exe" -ArgumentList "`"$dir\target.log`" -28000 600" -WindowStyle Hidden -PassThru
  $gamePid=$target.Id;$injected=& $v --json inject --pid $gamePid|ConvertFrom-Json;if(-not $injected.ok){throw 'Own target injection failed'}
  if(-not $ExpectRed){
   & $v --json --pid $gamePid down rbutton|Out-Null
   $held=& $v --json --pid $gamePid trace key rbutton|ConvertFrom-Json
   $up=& $v --json --pid $gamePid mouseup right 1 1 --capture-space|ConvertFrom-Json
   $released=& $v --json --pid $gamePid trace key rbutton|ConvertFrom-Json
   & $v --json --pid $gamePid up rbutton|Out-Null
   Check ($held.virtual_down -and -not $up.ok -and $up.button_released -and -not $released.virtual_down) "capture$arch invalid capture-space mouseup still releases without moving"
   $firstBurst=& $v --json --pid $gamePid capture-burst "$dir\first.png" --frames 1 --max-width 160|ConvertFrom-Json
   $unsetMap=& $v --json --pid $gamePid mouse 1 1 --capture-space|ConvertFrom-Json
   Check ($firstBurst.ok -and -not $unsetMap.ok -and $unsetMap.error -match 'single-frame') "capture$arch first-ever burst does not silently create a single-frame coordinate map"
  }
  $plain=& $v --json --pid $gamePid capture "$dir\plain.png"|ConvertFrom-Json
  if(-not $ExpectRed){
   $rawHash=& $v --json --pid $gamePid json '{"cmd":"frame-hash"}'|ConvertFrom-Json
   Check ($rawHash.ok -and $rawHash.hash -eq $plain.hash) "capture$arch default PNG and untransformed frame-hash retain source hash semantics"
   $motionHash=& $v --json --pid $gamePid json '{"cmd":"frame-hash","motion_sample":true}'|ConvertFrom-Json;$proofs.Add($motionHash)
   Check ($motionHash.ok-and$motionHash.motion_sample_version-eq1-and$motionHash.motion_sample_w-eq16-and$motionHash.motion_sample_h-eq16-and$motionHash.motion_sample_rgb-match'^[0-9A-F]{1536}$'-and$motionHash.capture_space_updated-eq$false-and$motionHash.hash-eq$rawHash.hash) "capture$arch optional native motion grid preserves original hash and coordinate map semantics"
   $keptPath=Join-Path $dir 'matched_hash_frame.png';$kept=& $v --json --pid $gamePid json (@{cmd='frame-hash';path=$keptPath;retain_changed_from='0000000000000000'}|ConvertTo-Json -Compress)|ConvertFrom-Json;$proofs.Add($kept)
   Check ($kept.ok-and$kept.observation_frame_saved-and$kept.observation_frame_path-eq$keptPath-and$kept.hash-eq$rawHash.hash-and(ImageInfo $keptPath).w-eq800) "capture$arch changed hash saves the exact observed pixels with its original hash/QPC receipt"
   $noFrame=Join-Path $dir ('unchanged_'+[guid]::NewGuid().ToString('N')+'.png');$unchanged=& $v --json --pid $gamePid json (@{cmd='frame-hash';path=$noFrame;retain_changed_from=$rawHash.hash}|ConvertTo-Json -Compress)|ConvertFrom-Json;$proofs.Add($unchanged)
   Check ($unchanged.ok-and-not$unchanged.observation_frame_saved-and-not(Test-Path -LiteralPath $noFrame)) "capture$arch unchanged hash never writes a matched-frame artifact"
   $ocrPath=Join-Path $dir 'observed_ocr_frame.png';$ocrFrame=& $v --json --pid $gamePid ocr --lines-only --observation-only=true --observation-capture $ocrPath|ConvertFrom-Json;$proofs.Add($ocrFrame)
   Check ($ocrFrame.ok-and$ocrFrame.observation_frame_path-eq$ocrPath-and$ocrFrame.capture_space_updated-eq$false-and(ImageInfo $ocrPath).w-eq800) "capture$arch OCR retains its observed image without recapturing or changing the coordinate map"
   $matchedText=& $v --json --pid $gamePid wait-text PLAY --region '60,110,200,60' --context-region '60,190,200,60' --timeout 1000|ConvertFrom-Json;$proofs.Add($matchedText)
   Check ($matchedText.ok-and$matchedText.found-and$matchedText.context.ok-and$matchedText.context.same_capture-and$matchedText.context.text-match'OPTIONS'-and$matchedText.context.capture_qpc_ticks-eq$matchedText.capture_qpc_ticks-and$matchedText.timing_clock-eq'QueryPerformanceCounter'-and$matchedText.capture_space_updated-eq$false) "capture$arch actual OCR matches primary PLAY and reads OPTIONS context from the exact same captured frame"
   $print=& $v --json --pid $gamePid capture "$dir\printwindow.png" --method printwindow --scale 0.5|ConvertFrom-Json
   Check ($print.ok -and $print.method -eq 'printwindow' -and $print.w -eq 400 -and (ImageInfo "$dir\printwindow.png").h -eq 225) "capture$arch optional transform also works after PrintWindow readback"
  }
  $scaled=& $v --json --pid $gamePid capture "$dir\scaled.png" --scale 0.5|ConvertFrom-Json;$proofs.Add($scaled)
  Check ($scaled.ok -and $scaled.w -eq 400 -and $scaled.h -eq 225 -and (ImageInfo "$dir\scaled.png").w -eq 400) "capture$arch scale0.5 saves400x225 PNG"
  $upscaled=& $v --json --pid $gamePid capture "$dir\upscaled.png" --scale 1.5|ConvertFrom-Json;$proofs.Add($upscaled)
  Check ($upscaled.ok -and $upscaled.w -eq 1200 -and $upscaled.h -eq 675 -and (ImageInfo "$dir\upscaled.png").h -eq 675) "capture$arch scale1.5 saves1200x675 PNG"
  $jpeg=& $v --json --pid $gamePid capture "$dir\small.jpg" --max-width 320 --format jpeg --quality 85|ConvertFrom-Json;$proofs.Add($jpeg)
  $signature=[IO.File]::ReadAllBytes("$dir\small.jpg");$image=ImageInfo "$dir\small.jpg"
  Check ($jpeg.ok -and $jpeg.w -eq 320 -and $jpeg.h -eq 180 -and $signature[0] -eq 255 -and $signature[1] -eq 216 -and $image.w -eq 320) "capture$arch JPEG has actual JPEG signature and proportional max-width dimensions"
  if(-not $ExpectRed){
   $pngPixel=ImageInfo "$dir\plain.png"
   Check ([math]::Abs($image.r-$pngPixel.r) -le 12 -and [math]::Abs($image.g-$pngPixel.g) -le 12 -and [math]::Abs($image.b-$pngPixel.b) -le 12) "capture$arch JPEG uniform patch stays within lossy colour tolerance"
  }
  $saved=& $v --json --pid $gamePid capture "$dir\roi.png" --region '40,90,240,100' --scale 0.5|ConvertFrom-Json
  & $v --json --pid $gamePid wait-change --region '500,300,100,100' --timeout-ms 100|Out-Null
  New-Item -ItemType Directory -Force "$dir\encode_failure"|Out-Null
  $failedSave=& $v --json --pid $gamePid capture "$dir\encode_failure"|ConvertFrom-Json
  $mapped=& $v --json --pid $gamePid mouse 60 25 --capture-space|ConvertFrom-Json;$proofs.Add($mapped)
  Check ($saved.ok -and -not $failedSave.ok -and $mapped.ok -and $mapped.x -eq 160 -and $mapped.y -eq 140) "capture$arch failed encode and hash-only wait preserve scaled ROI map"
  if(-not $ExpectRed){
   $clicked=& $v --json --pid $gamePid click left 60 25 --capture-space|ConvertFrom-Json
   Start-Sleep -Milliseconds 100;$text=Get-Content "$dir\target.log" -Raw
   Check ($clicked.ok -and $text -match 'CLICK 160 140 PLAY' -and $text -match 'SCREEN SELECT') "capture$arch scaled ROI coordinates hit actual owned PLAY consumer"
   $info=[Diagnostics.ProcessStartInfo]::new($v);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
   foreach($argument in @('--json','--pid',"$gamePid",'capture-burst',"$dir\sheet.jpg",'--frames','4','--interval-ms','400','--columns','2','--max-width','160','--format','jpeg')){$info.ArgumentList.Add($argument)}
   $burst=[Diagnostics.Process]::new();$burst.StartInfo=$info;if(-not $burst.Start()){throw 'Own burst CLI did not start'}
   $outputTask=$burst.StandardOutput.ReadToEndAsync();$firstLineTask=$burst.StandardError.ReadLineAsync()
   if(-not $firstLineTask.Wait(10000)){throw 'First completed burst frame was not observed'}
   $firstLine=$firstLineTask.GetAwaiter().GetResult();$errorTask=$burst.StandardError.ReadToEndAsync()
   Check ($firstLine -match 'burst frame 1/4' -and -not $burst.HasExited) "capture$arch independently observes first completed temporal frame before scene change"
   $watch=[Diagnostics.Stopwatch]::StartNew();$back=& $v --json --pid $gamePid click left 400 270 --hover-frames 0|ConvertFrom-Json;$watch.Stop()
   Check ($back.ok -and $watch.ElapsedMilliseconds -lt 1000 -and -not $burst.HasExited) "capture$arch input command completes while temporal burst remains active"
   if(-not $burst.WaitForExit(15000)){throw 'Own burst CLI did not finish'}
   [IO.File]::WriteAllText("$dir\burst.json",$outputTask.GetAwaiter().GetResult());[IO.File]::WriteAllText("$dir\burst.stderr",$firstLine+"`n"+$errorTask.GetAwaiter().GetResult())
   $sheet=Get-Content "$dir\burst.json" -Raw|ConvertFrom-Json;$proofs.Add($sheet)
   Check ($sheet.ok -and $sheet.frames.Count -eq 4 -and $sheet.w -eq 320 -and $sheet.h -eq 180 -and $sheet.coordinate_space -eq 'temporal_contact_sheet' -and -not $sheet.capture_space_updated) "capture$arch bounded sheet returns four temporal tiles without single-frame map"
   $times=@($sheet.frames|ForEach-Object{$_.actual_start_ms});$hashes=@($sheet.frames|ForEach-Object{$_.hash}|Select-Object -Unique)
   Check ($times[0] -lt $times[1] -and $times[1] -lt $times[2] -and $times[2] -lt $times[3] -and $hashes.Count -gt 1) "capture$arch burst reports real ordered times and scene change across tiles"
   Check ($null-eq$sheet.frames[0].changed_pixel_pct_previous-and@($sheet.frames|Where-Object {$_.changed_pixel_pct_previous-gt0}).Count-gt0-and@($sheet.frames|Where-Object {$_.black_pixel_pct-lt0-or$_.black_pixel_pct-gt100}).Count-eq0) "capture$arch burst reports pixel differences for the actual scene change and bounded black coverage"
   $afterBurst=& $v --json --pid $gamePid click left 60 25 --capture-space|ConvertFrom-Json
   $text=Get-Content "$dir\target.log" -Raw
   Check ($afterBurst.ok -and $afterBurst.x -eq 160 -and $afterBurst.y -eq 140 -and ([regex]::Matches($text,'CLICK 160 140 PLAY')).Count -eq 2) "capture$arch burst preserves saved ROI map and actual button hit"
   $partial=& $v --json --pid $gamePid capture-burst "$dir\must_not_complete.png" --frames 4 --interval-ms 400 --timeout-ms 250|ConvertFrom-Json;$proofs.Add($partial)
   $partialMap=& $v --json --pid $gamePid mouse 60 25 --capture-space|ConvertFrom-Json
   Check (-not $partial.ok -and $partial.completed_frames -lt 4 -and $partial.capture_space_updated -eq $false -and -not (Test-Path "$dir\must_not_complete.png") -and $partialMap.x -eq 160 -and $partialMap.y -eq 140) "capture$arch partial deadline failure is non-success and preserves the saved map"
   foreach($bad in @(@('--scale','0'),@('--scale','nan'),@('--scale','4.5'),@('--max-width','0'),@('--max-width','8193'),@('--scale','0.5','--max-width','200'),@('--format','bmp'),@('--format','png','--quality','80'),@('--format','jpeg','--quality','101'))){
    $reply=& $v --json --pid $gamePid capture "$dir\must_not_exist.png" @bad|ConvertFrom-Json
    Check (-not $reply.ok -and -not (Test-Path "$dir\must_not_exist.png")) "capture$arch invalid output options fail before writing: $($bad -join ' ')"
   }
   foreach($bad in @(@('--frames','0'),@('--frames','17'),@('--frames','1.5'),@('--interval-ms','0'),@('--interval-ms','5001'),@('--frames','16','--interval-ms','5000'),@('--frames','2','--columns','3'),@('--format','bmp'),@('--format','jpeg','--quality','0'))){
    $reply=& $v --json --pid $gamePid capture-burst "$dir\must_not_burst.png" @bad|ConvertFrom-Json
    Check (-not $reply.ok -and -not (Test-Path "$dir\must_not_burst.png")) "capture$arch burst limits fail before writing: $($bad -join ' ')"
   }
  }else{
   $sheet=& $v --json --pid $gamePid capture-burst "$dir\sheet.jpg" --frames 4 --interval-ms 400 --max-width 160 --format jpeg|ConvertFrom-Json
   Check ($sheet.ok -and $sheet.frames.Count -eq 4) "capture$arch temporal burst available"
  }
 }finally{
  if($burst -and -not $burst.HasExited){Stop-Process -Id $burst.Id -Force -Confirm:$false}
  if($target -and -not $target.HasExited){Stop-Process -Id $target.Id -Force -Confirm:$false}
 }
 if(-not $ExpectRed -and $LegacyDllDir){
  try{
   $legacyDll=Join-Path $LegacyDllDir $(if($arch -eq '32'){'vinput32.dll'}else{'vinput64.dll'})
   $target=Start-Process "$D\vi_uitarget$arch.exe" -ArgumentList "`"$dir\legacy.log`" -28000 600" -WindowStyle Hidden -PassThru
   $legacyPid=$target.Id;$injected=& $v --json inject --pid $legacyPid --dll $legacyDll|ConvertFrom-Json;if(-not $injected.ok){throw 'Own old-target injection failed'}
   $legacySaved=& $v --json --pid $legacyPid capture "$dir\legacy.png" --region '40,90,240,100'|ConvertFrom-Json
   $legacyStatus=& $v --json --pid $legacyPid status|ConvertFrom-Json
   $legacyScaled=& $v --json --pid $legacyPid capture "$dir\legacy_scaled.png" --scale 0.5|ConvertFrom-Json
   $legacyBurst=& $v --json --pid $legacyPid capture-burst "$dir\legacy_sheet.png" --frames 1|ConvertFrom-Json
   $legacyAfter=& $v --json --pid $legacyPid status|ConvertFrom-Json
   $legacyMap=& $v --json --pid $legacyPid mouse 120 50 --capture-space|ConvertFrom-Json
   Check ($legacySaved.ok -and -not $legacyScaled.ok -and -not $legacyBurst.ok -and $legacyScaled.error -match 'matching updated DLL' -and $legacyAfter.counters.captures -eq $legacyStatus.counters.captures -and $legacyMap.x -eq 160 -and $legacyMap.y -eq 140) "capture$arch old DLL rejected before new output requests can corrupt its saved map"
  }finally{if($target -and -not $target.HasExited){Stop-Process -Id $target.Id -Force -Confirm:$false}}
 }
 if(-not $ExpectRed -and -not $LegacyDllDir){"SKIPPED capture$arch old-DLL capability control: supply -LegacyDllDir with owned baseline DLLs"}
}
$failed=@($checks|Where-Object{-not $_.pass}).Count
[pscustomobject]@{ok=($failed -eq 0);checks=$checks;proofs=$proofs;failed=$failed;expected_red=[bool]$ExpectRed}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath "$Out\result.json"
if($ExpectRed){if($failed -lt 4){throw 'Baseline unexpectedly supports output transforms/burst'};'EXPECTED RED: output transform/map/burst controls fail on baseline';$global:LASTEXITCODE=0}
elseif($failed){throw "$failed capture output controls failed"}
else{$global:LASTEXITCODE=0}
