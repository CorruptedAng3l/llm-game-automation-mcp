# Application-active loop controls. All targets are offscreen/NOACTIVATE; no physical input.
param([string]$Out="$PSScriptRoot\..\build\test\activation",[switch]$Baseline)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out|Out-Null
$env:TEMP=Join-Path $root 'build\temp';$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null
if(-not('ViAutoActNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViAutoActNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);}'}
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=@()
function Check($ok,$name){$checks.Add(@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name"}
. "$PSScriptRoot\vi_logwindow.ps1"
function Text($p){if(!(Test-Path -LiteralPath $p.log)){return ''};$snapshot=[ViLogWindow]::Read($p.log);if(!$snapshot.valid){throw "owned activation log unavailable: $($snapshot.reason)"};$text=[Text.Encoding]::UTF8.GetString($snapshot.bytes);if($p.readyMarker){$at=$text.IndexOf($p.readyMarker,[StringComparison]::Ordinal);if($at-lt0){throw 'current activation READY marker disappeared'};return $text.Substring($at)};return $text}
function WaitText($p,$pattern,[int]$ms=3000){$until=[Environment]::TickCount64+$ms;do{if((Text $p)-match$pattern){return $true};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until);return $false}
function Vi($p,[string[]]$arguments){$raw=&$p.cli --json --pid $p.process.Id @arguments;$code=$LASTEXITCODE;$json=$raw|ConvertFrom-Json;if($code-ne0-or-not$json.ok){throw "CLI failed $raw"};return $json}
function StartTarget($arch,$name,$flags){
 $log=Join-Path $Out "$arch.$name.log";$exe=Join-Path "$root\dist" $(if($arch-eq'x64'){'vi_dinput1test.exe'}else{'vi_dinput1test32.exe'})
 # These two paths belong to this exact fixture case. Never consume a prior
 # run's sentinel or READY/ACTIVATEAPP lines while a new process is starting.
 $resume=$log+'.resume';$absolute=[IO.Path]::GetFullPath($resume)
 if(!$absolute.StartsWith($Out.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'activation sentinel escaped output directory'}
 $hadResume=Test-Path -LiteralPath $resume;$priorBytes=if(Test-Path -LiteralPath $log){(Get-Item -LiteralPath $log).Length}else{0}
 if($hadResume){Remove-Item -LiteralPath $resume -Force}
 [IO.File]::WriteAllText($log,'')
 $process=Start-Process -FilePath $exe -ArgumentList "`"$log`" --gated $flags" -WindowStyle Hidden -PassThru
 $script:owned+=,$process;$p=@{process=$process;log=$log;cli=(Join-Path "$root\dist" $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'}))}
 if(-not(WaitText $p '(?m)^\d{2}:\d{2}:\d{2}\.\d{3} READY ')){throw 'fixture not ready'}
 $p.readyMarker=[regex]::Match((Text $p),'(?m)^\d{2}:\d{2}:\d{2}\.\d{3} READY [^\r\n]*').Value
 $p.resume_removed=$hadResume;$p.previous_log_bytes=$priorBytes
 if(!$p.readyMarker){throw 'current activation READY marker unavailable'}
 Check ((Text $p)-notmatch'ACTIVATEAPP 1') "$arch $name never activated before injection" | Out-Host
 $inject=Vi $p @('inject');return $p
}
try{foreach($arch in @('x64','x86')){
 $p=StartTarget $arch 'default' ''
 Check (WaitText $p 'ACTIVATEAPP 1') "$arch existing guarded adoption reaches application-active flag"
 Vi $p @('key','a','--ms','150')|Out-Null;Vi $p @('down','lbutton')|Out-Null;Start-Sleep -Milliseconds 150;Vi $p @('up','lbutton')|Out-Null
 Check (WaitText $p 'KEY dik=0x1E down') "$arch gated application consumes virtual keyboard"
 Check (WaitText $p 'MOUSE b0 down') "$arch gated application consumes virtual mouse"
 $status=Vi $p @('status');$evidence.Add(@{arch=$arch;mode='default';ready_marker=$p.readyMarker;resume_removed=$p.resume_removed;previous_log_bytes=$p.previous_log_bytes;status=$status})
 Check (-not$status.really_foreground) "$arch default remains actual background"
 Check ($status.autoactivate-eq$true-and$status.app_active-eq1-and$status.counters.act_synth-ge1) "$arch active state and synthetic-attempt diagnostics present"
 $p.process.Kill();$p.process.WaitForExit()
 $p=StartTarget $arch 'paused' '--pause-pump'
 $disabled=Vi $p @('config','autoactivate=false');[IO.File]::WriteAllText($p.log+'.resume','resume')
 Start-Sleep -Milliseconds 350
 Check ((Text $p)-notmatch'ACTIVATEAPP 1') "$arch disabling autoactivation cancels queued adoption callback"
 Check ($disabled.autoactivate-eq$false) "$arch config reports disabled autoactivation"
 Vi $p @('config','autoactivate=true')|Out-Null
 Check (WaitText $p 'ACTIVATEAPP 1' 3500) "$arch bounded monitor recovers after opt-in re-enable"
 $status=Vi $p @('status');$evidence.Add(@{arch=$arch;mode='reenable';ready_marker=$p.readyMarker;resume_removed=$p.resume_removed;previous_log_bytes=$p.previous_log_bytes;status=$status})
 Check (-not$status.really_foreground-and-not$status.foreground_guard.user_took_foreground) "$arch synthetic callback is not a physical user selection"
 Check ($status.app_active-eq1-and$status.counters.act_synth-ge2-and$status.counters.act_synth-le3) "$arch recovery counts bounded attempts and actual active delivery"
 $p.process.Kill();$p.process.WaitForExit()
 if(-not$Baseline){
  $p=StartTarget $arch 'budget' '--pause-pump';Start-Sleep -Milliseconds 4500
  $stalled=Vi $p @('status')
  Check ($stalled.counters.act_synth-eq3-and$stalled.autoactivate_attempts_left-eq0-and$stalled.app_active-eq-1) "$arch unresponsive posted queue exhausts exactly three attempts"
  Check (-not$stalled.really_foreground-and-not$stalled.user_took_foreground) "$arch stalled synthetic queue does not select foreground"
  Vi $p @('config','autoactivate=false')|Out-Null;[IO.File]::WriteAllText($p.log+'.resume','resume')
  Check ((WaitText $p PUMP_RESUMED)-and(Text $p)-notmatch'ACTIVATEAPP 1') "$arch disabled queue drains without application activation"
  Vi $p @('config','autoactivate=true')|Out-Null;Start-Sleep -Milliseconds 300;$exhausted=Vi $p @('status')
  Check ($exhausted.counters.act_synth-eq3-and(Text $p)-notmatch'ACTIVATEAPP 1') "$arch re-enable alone does not refill an exhausted budget"
  Vi $p @('config','fakefocus=false','autoactivate=false')|Out-Null
  $h=[IntPtr]::new([Convert]::ToInt64($exhausted.hwnd.Substring(2),16));[uint32]$windowOwner=0;[ViAutoActNative]::GetWindowThreadProcessId($h,[ref]$windowOwner)|Out-Null
  if($windowOwner-ne$p.process.Id){throw 'refusing post to non-owned HWND'}
  Check ([ViAutoActNative]::PostMessage($h,0x1C,[IntPtr]::Zero,[IntPtr]::Zero)-and(WaitText $p 'ACTIVATEAPP 0')) "$arch explicit owned deactivation reaches application while fakefocus disabled"
  $inactive=Vi $p @('status');Check ($inactive.app_active-eq0-and$inactive.autoactivate_attempts_left-eq3) "$arch forwarded deactivation refills bounded recovery"
  Vi $p @('config','fakefocus=true','autoactivate=true')|Out-Null
  Check (WaitText $p 'ACTIVATEAPP 1') "$arch recovery after genuine forwarded inactive state succeeds"
  $recovered=Vi $p @('status');Check ($recovered.counters.act_synth-eq4-and$recovered.app_active-eq1-and-not$recovered.really_foreground-and-not$recovered.user_took_foreground) "$arch recovery counts one new guarded attempt without human selection"
  $evidence.Add(@{arch=$arch;mode='budget';ready_marker=$p.readyMarker;resume_removed=$p.resume_removed;previous_log_bytes=$p.previous_log_bytes;stalled=$stalled;exhausted=$exhausted;inactive=$inactive;recovered=$recovered})
  $p.process.Kill();$p.process.WaitForExit()
 }
}}
finally{
 @{checks=@($checks.ToArray());evidence=@($evidence.ToArray());baseline=[bool]$Baseline}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json"
 foreach($process in $owned){if(-not$process.HasExited){$process.Kill();$process.WaitForExit()}}
}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL"
if($Baseline){if($failed-lt2){throw 'expected baseline diagnostics/optout failures missing'};$global:LASTEXITCODE=0}else{if($failed){throw "$failed activation failures"};$global:LASTEXITCODE=0}
