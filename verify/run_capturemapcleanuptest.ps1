param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\mapcleanup",[switch]$ExpectRed)
$ErrorActionPreference='Stop';$CliDir=(Resolve-Path $CliDir).Path;$TargetDir=(Resolve-Path $TargetDir).Path;$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out|Out-Null;$checks=@();$records=@()
foreach($suffix in @('','32')){
 $v=Join-Path $CliDir "vinput$suffix.exe";$p=$null
 try{
  $p=Start-Process -FilePath "$TargetDir\vi_channelprobe$suffix.exe" -ArgumentList "`"$Out\target$suffix.log`"" -WindowStyle Hidden -PassThru
  $inject=&$v --json inject --pid $p.Id|ConvertFrom-Json;if(-not$inject.ok){throw 'owned fixture injection failed'}
  & $v --json --pid $p.Id down rbutton|Out-Null;$before=&$v --json --pid $p.Id trace key rbutton|ConvertFrom-Json
  $up=&$v --json --pid $p.Id mouseup right 1 1 --capture-space|ConvertFrom-Json;$exit=$LASTEXITCODE;$after=&$v --json --pid $p.Id trace key rbutton|ConvertFrom-Json
  $records+=@{arch=$suffix;pid=$p.Id;before=$before;reply=$up;exit=$exit;after=$after}
  foreach($entry in @(@{name="map$suffix actual right button held before cleanup";pass=[bool]$before.virtual_down},@{name="map$suffix invalid capture-space mouseup still releases with no saved image";pass=(-not$up.ok-and$exit-ne0-and$up.button_released-and-not$after.virtual_down)})){
   $checks+=$entry;"$(if($entry.pass){'PASS'}else{'FAIL'}) $($entry.name)"
  }
  & $v --json --pid $p.Id down rbutton|Out-Null
  $heldAgain=&$v --json --pid $p.Id trace key rbutton|ConvertFrom-Json
  $invalid=&$v --json --pid $p.Id mouseup right 80 70 --hover-timeout-ms 0|ConvertFrom-Json;$invalidExit=$LASTEXITCODE
  $afterInvalid=&$v --json --pid $p.Id trace key rbutton|ConvertFrom-Json
  $records+=@{arch=$suffix;phase='invalid_hover_timing';before=$heldAgain;reply=$invalid;exit=$invalidExit;after=$afterInvalid}
  $entry=@{name="map$suffix malformed positioned mouseup reports error after actual release";pass=($heldAgain.virtual_down-and-not$invalid.ok-and$invalidExit-ne0-and$invalid.button_released-and-not$afterInvalid.virtual_down)}
  $checks+=$entry;"$(if($entry.pass){'PASS'}else{'FAIL'}) $($entry.name)"
 }finally{if($p-and-not$p.HasExited){&$v --json --pid $p.Id up rbutton|Out-Null;$p.Kill();$p.WaitForExit()};@{checks=$checks;replies=$records}|ConvertTo-Json -Depth 10|Set-Content "$Out\result.json"}
}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL"
if($ExpectRed){if($failed-ne4){throw 'expected exactly four actual cleanup failures'};$global:LASTEXITCODE=0}else{if($failed){throw 'capture map cleanup failed'};$global:LASTEXITCODE=0}
