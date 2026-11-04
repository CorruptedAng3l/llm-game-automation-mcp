param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\key_sequence")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_ownedprocess.ps1"
$root=(Resolve-Path "$PSScriptRoot\..").Path;$v=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;New-Item -ItemType Directory -Force "$Out\tools\temp"|Out-Null
$checks=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$owned=@();$targetId=0;$log=''
function Check($ok,$name){$checks.Add(@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not$ok){throw $name}}
function Call([string[]]$argv){$watch=[Diagnostics.Stopwatch]::StartNew();$raw=@(& $v --json --pid $script:targetId @argv);$code=$LASTEXITCODE;$watch.Stop();$reply=$raw|ConvertFrom-Json;$calls.Add(@{args=$argv;exit=$code;elapsed_ms=$watch.ElapsedMilliseconds;reply=$reply});return $reply}
function Bits { $lines=@(Get-Content -LiteralPath $script:log -ErrorAction SilentlyContinue|Where-Object {$_-match'OBS bits='});if($lines.Count-and$lines[-1]-match'bits=(\d+)'){return [int]$Matches[1]};return -1 }
function AwaitBits([int]$bits,[int]$timeout=2500){$end=[Environment]::TickCount64+$timeout;do{if((Bits)-eq$bits){return $true};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end);return $false}
function AwaitDone([string]$id){$end=[Environment]::TickCount64+5000;do{$job=(Call @('job-status',$id)).job;if($job.job_complete){return $job};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$end);throw 'sequence did not complete within5seconds'}
try{
 foreach($arch in @('x64','x86')){
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$tools="$Out\tools"
  $batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_keysequencetest.cpp" /Fo:"$tools\parser$suffix.obj" /Fe:"$tools\parser$suffix.exe" || exit /b 2
"$tools\parser$suffix.exe" || exit /b 3
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_ready_jsontest.cpp" /Fo:"$tools\protocol$suffix.obj" /Fe:"$tools\protocol$suffix.exe" || exit /b 2
"$tools\protocol$suffix.exe" || exit /b 3
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_keysequenceconsumer.cpp" /Fo:"$tools\consumer$suffix.obj" /Fe:"$tools\consumer$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch sequence fixtures and parser zero warnings"
  $script:log="$Out\consumer_$arch.log";$process=Start-Process "$tools\consumer$suffix.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned+=$process;$script:targetId=$process.Id
  $inject=& $v --json inject --pid $targetId|ConvertFrom-Json;Check $inject.ok "$arch observer injected";Check (AwaitBits 0) "$arch observer neutral";$status=Call @('status');Check (-not$status.really_foreground) "$arch only background input"
  $file="$Out\flight_$arch.vin";[IO.File]::WriteAllText($file,"hold w+space 1500`nwait 100`nkey d 300`n")
  $accepted=Call @('seq',$file);Check ($accepted.ok-and$accepted.accepted-and$accepted.job_id) "$arch timed sequence accepted as background job"
  Check ($calls[-1].elapsed_ms-lt1000-and-not$accepted.job_complete) "$arch sequence returns before first1500ms hold completes"
  Check (AwaitBits 3) "$arch actual target reads W and Space together";$job=(Call @('job-status',$accepted.job_id)).job;Check ($job.job_kind-eq'sequence'-and$job.sequence_steps-eq3-and$job.sequence_step-eq0) "$arch sequence progress and kind reported"
  $busy=Call @('hold','d','100','--async');Check (-not$busy.ok-and$busy.error-match'resource busy') "$arch future step D reserved before its press"
  $notReplaced=Call @('seq',$file,'--replace=false');$stillRunning=(Call @('job-status',$accepted.job_id)).job;Check (-not$notReplaced.ok-and$notReplaced.error-match'resource busy'-and-not$notReplaced.delivery_unknown-and-not$stillRunning.job_complete) "$arch replace=false preserves original and reports definite conflict"
  $cancel=Call @('job-cancel',$accepted.job_id);Check ($cancel.ok-and(AwaitBits 0)) "$arch cancellation releases complete active chord";$cancelLine=@(Get-Content $log).Count;Start-Sleep -Milliseconds 2000;$afterCancel=@(Get-Content $log|Select-Object -Skip $cancelLine);Check ((Bits)-eq0-and($afterCancel-join"`n")-notmatch'bits=[4-7](?:\s|$)') "$arch cancelled sequence never presses later D"
  $start=@(Get-Content $log).Count;$complete=Call @('sequence',$file);$done=AwaitDone $complete.job_id;$observed=@(Get-Content $log|Select-Object -Skip $start);Check ($done.ok-and$done.job_state-eq'completed'-and$done.sequence_step-eq3) "$arch complete ordered sequence reports every step"
  Check (($observed-join"`n")-match'bits=3'-and($observed-join"`n")-match'bits=4'-and(AwaitBits 0)) "$arch actual reads show chord then D then released state"
  [IO.File]::WriteAllText($file,"key a 20`nhold ctrl+a 1500`n");$modifier=Call @('seq',$file);Check (AwaitBits 40) "$arch second chord modifier and A are actually held";$releaseLine=@(Get-Content $log).Count;Call @('job-cancel',$modifier.job_id)|Out-Null;Check (AwaitBits 0) "$arch modifier chord cancellation reaches neutral";Start-Sleep -Milliseconds 100;$releases=@(Get-Content $log|Select-Object -Skip $releaseLine|Where-Object {$_-match'KEY message=257 vk='});Check ($releases.Count-ge2-and$releases[0]-match'vk=65$'-and$releases[1]-match'vk=17$') "$arch cancellation releases active A before Control regardless of union order"
  [IO.File]::WriteAllText($file,"hold w 500`nunknown d 50`n");$before=(Call @('trace','key','w')).injected_down;$invalid=Call @('seq',$file);$after=(Call @('trace','key','w')).injected_down;Check (-not$invalid.ok-and$before-eq$after-and(Bits)-eq0) "$arch invalid final line refuses before valid prefix injects"
  [IO.File]::WriteAllText($file,"wait 350`nkey d 500`n");$waiting=Call @('seq',$file);$manual=Call @('down','d');Start-Sleep -Milliseconds 450;$cancelled=(Call @('job-status',$waiting.job_id)).job;Check ($manual.ok-and$cancelled.job_state-eq'cancelled'-and(Bits)-eq4) "$arch manual future-key takeover cancels wait and keeps newer generation";Call @('up','d')|Out-Null;Check (AwaitBits 0) "$arch explicit newer key release works"
  Stop-ViOwnedProcess $process
 }
 $peerFile="$Out\peer.vin";[IO.File]::WriteAllText($peerFile,"key w 100`n")
 foreach($mode in @('disconnect','partial','refuse','invalid_bool','nested_id')){
  $peerLog="$Out\peer_$mode.log";$shell=(Get-Command pwsh).Source;$peer=Start-Process $shell -ArgumentList "-NoProfile -File `"$PSScriptRoot\vi_sequence_ack_peer.ps1`" -Log `"$peerLog`" -Mode $mode" -WindowStyle Hidden -PassThru;$owned+=$peer;$script:targetId=$peer.Id
  $deadline=[Environment]::TickCount64+3000;do{if((Get-Content $peerLog -Raw -ErrorAction SilentlyContinue)-match'READY'){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$deadline)
  $reply=Call @('seq',$peerFile);$peer.WaitForExit(3000)|Out-Null
  if($mode-eq'refuse'){Check (-not$reply.ok-and$reply.input_injected-eq$false-and-not$reply.delivery_unknown-and$reply.error-eq'synthetic definite refusal') 'definite valid refusal remains definite and preserves error'}
  else{Check (-not$reply.ok-and$reply.delivery_unknown-and$reply.may_have_started-and$null-eq$reply.input_injected) "$mode scheduling acknowledgement becomes valid uncertain-input JSON"}
  Check (@(Get-Content $peerLog|Where-Object {$_-match'REQUEST .*"cmd":"sequence"'}).Count-eq1) "$mode scheduling request is never retried"
  Check (@(Get-Content $peerLog|Where-Object {$_-match'REQUEST .*"cmd":"readiness"'}).Count-eq1-and@(Get-Content $peerLog|Where-Object {$_-match'REQUEST .*"cmd":"status"'}).Count-eq1) "$mode synthetic armed-cursor readiness and sequence support are each probed once"
  Stop-ViOwnedProcess $peer
 }
}finally{foreach($process in $owned){Stop-ViOwnedProcess $process};@{checks=$checks;calls=$calls}|ConvertTo-Json -Depth 10|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
