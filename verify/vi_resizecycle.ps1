function Get-ViResizeCycleClass($ResizeOk,$RaceAttempted,$RaceObserved,$Capture,$ExpectedApi,$Pattern){
 if(!$ResizeOk){return 'resize_failed'}
 if($RaceAttempted-and!$RaceObserved){return 'race_not_observed'}
 if(!$Capture-or!$Capture.ok){return 'capture_failed'}
 if($Capture.method-ne'swapchain'-or$Capture.capture_path-ne$ExpectedApi){return 'non_native'}
 if(!$Pattern-or!$Pattern.ok){return 'pattern_mismatch'}
 return 'passed'
}
function Write-ViNativeCaptureResult($Path,$Result){
 $absolute=[IO.Path]::GetFullPath($Path);$temporary=$absolute+'.tmp'
 $json=$Result|ConvertTo-Json -Depth 28
 [IO.File]::WriteAllText($temporary,$json,[Text.UTF8Encoding]::new($false))
 [IO.File]::Move($temporary,$absolute,$true)
}
