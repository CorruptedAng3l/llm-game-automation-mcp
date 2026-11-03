# Startup and post-launch foreground policy. Every deliberate activation is under the CANONICAL lock.
# Independent foreground sampler + blockfocus=false positive control prevent a false "no foreground" result.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\focusrobust",[int]$LockWaitSec=300,[string[]]$Targets=@('vi_focusprobe.exe','vi_focusprobe32.exe'))
$ErrorActionPreference='Stop'
$V=(Resolve-Path "$CliDir\vinput.exe").Path; $D=(Resolve-Path $TargetDir).Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
if(-not ('ViFocusNative' -as [type])) { Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public static class ViFocusNative {
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@ }
. "$PSScriptRoot\_focus_sentinel.ps1"
$sentinelState=$null
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[Diagnostics.Process]]::new()
function Check($ok,$name,$detail='') { $checks.Add([pscustomobject]@{pass=[bool]$ok;name=$name;detail=$detail}); "$(if($ok){'PASS'}else{'FAIL'}) $name $detail" }
function Fg { $h=[ViFocusNative]::GetForegroundWindow();[uint32]$p=0;[ViFocusNative]::GetWindowThreadProcessId($h,[ref]$p)|Out-Null;[pscustomobject]@{hwnd=$h;pid=$p} }
function IsProbe { $f=Fg;if($sentinelState -and $f.pid -eq $sentinelState.process.Id){return $false};$s=[Text.StringBuilder]::new(128);[ViFocusNative]::GetClassName($f.hwnd,$s,128)|Out-Null;return ($s.ToString() -eq 'vi_focusprobe') }
function Vi($id,[string[]]$a) { & $V --json --pid $id @a | ConvertFrom-Json }
function Json($id,$j) { Vi $id @('json',$j) }
function Handoff($id,$h) { if($id){ $hex='0x{0:X}' -f $h.ToInt64(); $script:lastHandoff=Json $id ('{"cmd":"restore-foreground","hwnd":"'+$hex+'"}') } }
function Start-Cli([string[]]$a) {
 $si=[Diagnostics.ProcessStartInfo]::new($V);$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
 foreach($arg in $a){$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;[void]$p.Start();$owned.Add($p)
 return [pscustomobject]@{p=$p;stdout=$p.StandardOutput.ReadToEndAsync();stderr=$p.StandardError.ReadToEndAsync()}
}
function Sample($id,$duration,[int]$AwaitMs=0) {
 $awaitStart=[Environment]::TickCount64
 while($AwaitMs -gt 0 -and (Fg).pid -ne $id -and [Environment]::TickCount64-$awaitStart -lt $AwaitMs){Start-Sleep -Milliseconds 5}
 $t=[Environment]::TickCount64;$run=-1;$max=0;$count=0
 while([Environment]::TickCount64-$t -lt $duration){$f=Fg;$now=[Environment]::TickCount64;if($f.pid -eq $id){if($run -lt 0){$run=$now};$count++;$max=[math]::Max($max,$now-$run)}else{$run=-1};Start-Sleep -Milliseconds 5}
 return [pscustomobject]@{count=$count;max_streak_ms=$max;ended_target=((Fg).pid -eq $id)}
}
function Steal($exe,$h,[switch]$ObserveOnly) {
 $helperLog="$Out\helper_$([guid]::NewGuid().ToString('N')).log"
 $mode=if($ObserveOnly){'--observe-only'}else{'--steal'}
 $p=Start-Process -FilePath $exe -ArgumentList ('{0} 0x{1:X} "{2}"' -f $mode,$h.ToInt64(),$helperLog) -WindowStyle Hidden -PassThru
 $p|Add-Member -MemberType NoteProperty -Name WitnessLog -Value $helperLog
 $owned.Add($p);return $p
}
function Witness($p) {
 if(-not $p.WaitForExit(2000)){return [pscustomobject]@{observed=$false;timeout=$true;exit_code=$null;raw=''}}
 $raw=Get-Content -LiteralPath $p.WitnessLog -Raw -ErrorAction SilentlyContinue
 $snapshot=$raw -match 'snapshot_seen=1';$event=$raw -match 'event_seen=1'
 return [pscustomobject]@{observed=($p.ExitCode -eq 0 -and ($snapshot -or $event));timeout=$false;exit_code=$p.ExitCode;hook_installed=($raw -match 'event_hook=1');snapshot_seen=$snapshot;event_seen=$event;log=$p.WitnessLog;raw=$raw}
}
function ProbeHandle($log,$label) {
 $until=[Environment]::TickCount64+3000
 do {
  $text=Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
  if($text -match ($label+' hwnd=0x([0-9A-F]+)')){return [IntPtr][long]::Parse($Matches[1],[Globalization.NumberStyles]::HexNumber)}
  Start-Sleep -Milliseconds 30
 } while([Environment]::TickCount64 -lt $until)
 throw "probe missing $label in $log"
}
$lockPath=$(if($env:VINPUT_LOCK){$env:VINPUT_LOCK}else{New-Item -ItemType Directory -Force "$PSScriptRoot\..\build"|Out-Null;"$PSScriptRoot\..\build\foreground.lock"});$token=[guid]::NewGuid().ToString('N');$stream=$null;$wait=[Environment]::TickCount64
while(-not $stream){try{$stream=[IO.File]::Open($lockPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)}catch [IO.IOException]{if([Environment]::TickCount64-$wait -gt $LockWaitSec*1000){throw 'canonical LIVE lock busy; never removed'};Start-Sleep -Seconds 1}}
$lease=[Text.Encoding]::UTF8.GetBytes((@{owner='vinput focus robustness';pid=$PID;token=$token;time=(Get-Date -Format o)}|ConvertTo-Json -Compress));$stream.Write($lease,0,$lease.Length);$stream.Flush();$stream.Dispose()
try {
 $sentinelState=New-FocusSentinel "$D\vi_focusprobe.exe" "$Out\sentinel"
 foreach($name in $Targets) {
  $targetPid=0;$nestedPid=0;$hostPid=0;Set-FocusSentinel $sentinelState|Out-Null;$before=$sentinelState.hwnd
  try {
   $log="$Out\$name.startup.log"
   $l=Start-Cli @('--json','launch','--exe',"$D\$name",'--args',"`"$log`" --steal-at-window",'--timeout','12','--settle','0','--fg-guard','0')
   $t=[Environment]::TickCount64;$streak=-1;$max=0;$seen=0
   while(-not $l.p.HasExited -and [Environment]::TickCount64-$t -lt 18000) {
    $now=[Environment]::TickCount64;if(IsProbe){if($streak -lt 0){$streak=$now};$seen++;$max=[math]::Max($max,$now-$streak)}else{$streak=-1};Start-Sleep -Milliseconds 5
   }
   if(-not $l.p.HasExited){throw 'launch timed out'}
   $stdout=$l.stdout.GetAwaiter().GetResult();$stderr=$l.stderr.GetAwaiter().GetResult();$stderr|Set-Content "$Out\$name.startup.stderr"
   $r=$stdout|ConvertFrom-Json;if($r.pid){$targetPid=[int]$r.pid;$owned.Add((Get-Process -Id $targetPid))}
   Check $r.ok "$name launch succeeds";Check ($max -le 500) "$name startup exposure <=500ms" "sampled_peak=$max ms, samples=$seen"
   # --settle 0/--fg-guard 0 can reply between window adoption and the monitor's first tick.
   # Wait for this test's actual protection precondition instead of assuming instant readiness.
   $readyUntil=[Environment]::TickCount64+2000
   do{$s=Vi $targetPid @('status');$g=$s.foreground_guard;if($g.armed){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$readyUntil)
   Check ($g.enabled -and $g.armed) "$name continuous guard armed"
   $h=[IntPtr][long]::Parse($s.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
   # Observe-only installs the same witness but performs no activation. A fresh background
   # target must not produce a historical/false exact-HWND event or a positive helper exit.
   Handoff $targetPid $before
   $observer=Steal "$D\$name" $h -ObserveOnly;$negativeWitness=Witness $observer
   Check ($negativeWitness.hook_installed -and $negativeWitness.exit_code -eq 1 -and -not $negativeWitness.event_seen -and -not $negativeWitness.snapshot_seen) "$name observe-only helper cannot invent foreground witness"
   # Independent positive control: same external activation is allowed when policy is deliberately disabled.
   Set-FocusSentinel $sentinelState|Out-Null
   Vi $targetPid @('config','blockfocus=false')|Out-Null
   $positive=Steal "$D\$name" $h; $pSample=Sample $targetPid 200 1000;$positiveWitness=Witness $positive
   Check ($pSample.max_streak_ms -ge 100 -and $positiveWitness.observed) "$name positive external activation really holds foreground" "held=$($pSample.max_streak_ms)ms, event=$($positiveWitness.event_seen)"
   Handoff $targetPid $before
   Vi $targetPid @('config','blockfocus=true')|Out-Null;Start-Sleep -Milliseconds 80
   Set-FocusSentinel $sentinelState|Out-Null
   $old=Vi $targetPid @('status')
   $thief=Steal "$D\$name" $h;$sample=Sample $targetPid 600;$gainWitness=Witness $thief
   $after=Vi $targetPid @('status')
   Check ($gainWitness.observed -and -not $sample.ended_target -and $sample.max_streak_ms -le 200 -and $after.foreground_guard.restored_count -gt $old.foreground_guard.restored_count) "$name post-launch external gain handed back <=200ms" "peak=$($sample.max_streak_ms) ms, event=$($gainWitness.event_seen)"
   Handoff $targetPid $before
   # Target-side activation APIs that previously bypassed hooks.
   $blocked=$after.counters.setfg_blocked
   foreach($message in @(0x8001,0x8002,0x8003,0x8004)) { [ViFocusNative]::PostMessage($h,$message,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 60 }
   $apis=Vi $targetPid @('status')
   Check ($apis.counters.setfg_blocked -ge $blocked+4 -and -not $apis.really_foreground) "$name SwitchToThisWindow/ShowWindowAsync/SetForegroundWindow/AllowSetForegroundWindow blocked"
   Set-FocusSentinel $sentinelState|Out-Null
   $busy=Start-Cli @('--json','--pid',"$targetPid",'json','{"cmd":"wait","ms":900}')
   Start-Sleep -Milliseconds 80
   $thief=Steal "$D\$name" $h;$duringWait=Sample $targetPid 450;$busyWitness=Witness $thief
   Check ($busyWitness.observed -and -not $duringWait.ended_target -and $duringWait.max_streak_ms -le 200) "$name guard works while command mutex is busy" "peak=$($duringWait.max_streak_ms) ms, event=$($busyWitness.event_seen)"
   Handoff $targetPid $before
   $busy.p.WaitForExit();$busy.stdout.GetAwaiter().GetResult() | Set-Content "$Out\$name.busy.json"
   # Explicit bounded lease is the exception for intentional locked verification. At expiry the guard resumes.
   Set-FocusSentinel $sentinelState|Out-Null
   $grant=Json $targetPid '{"cmd":"foreground-lease","ms":5000}'
   $held=$null;$expired=$null
   if($grant.ok) {
    $thief=Steal "$D\$name" $h;$held=Sample $targetPid 200 1000;$leaseWitness=Witness $thief;Check ($held.max_streak_ms -ge 100 -and $leaseWitness.observed) "$name explicit lease permits foreground"
    # Time expiry from an already-active target, separating it from helper startup/queue overhead.
    Json $targetPid '{"cmd":"foreground-lease","ms":500}'|Out-Null
    Start-Sleep -Milliseconds 650;$expired=Vi $targetPid @('status');Check (-not $expired.really_foreground -and $expired.foreground_guard.lease_remaining_ms -eq 0) "$name lease expiry restores background guard"
   } else {Check $false "$name explicit lease supported"}
   # An explicit hand-back ends its lease immediately, not only when its old deadline arrives.
   Set-FocusSentinel $sentinelState|Out-Null
   $cancelGrant=Json $targetPid '{"cmd":"foreground-lease","ms":5000}'
   $thief=Steal "$D\$name" $h;$cancelHeld=Sample $targetPid 200 1000;$cancelLeaseWitness=Witness $thief
   Check ($cancelGrant.ok -and $cancelHeld.max_streak_ms -ge 100 -and $cancelLeaseWitness.observed) "$name cancellation control really held foreground under lease"
   Handoff $targetPid $before
   $cancelled=Vi $targetPid @('status')
   Check ($cancelled.foreground_guard.lease_remaining_ms -eq 0 -and -not $cancelled.really_foreground) "$name explicit restore cancels foreground lease"
   Set-FocusSentinel $sentinelState|Out-Null
   $thief=Steal "$D\$name" $h;$afterCancel=Sample $targetPid 450;$cancelWitness=Witness $thief
   Check ($cancelWitness.observed -and -not $afterCancel.ended_target -and $afterCancel.max_streak_ms -le 200) "$name foreground regrab after explicit restore is guarded" "peak=$($afterCancel.max_streak_ms) ms, event=$($cancelWitness.event_seen)"
   Json $targetPid '{"cmd":"foreground-lease","ms":0}'|Out-Null;Handoff $targetPid $before
   # Cross-process host -> intermediate -> CoreWindow target: GetParent(target) is NOT the real
   # foreground root. The same topology is discoverable by the CLI's UWP child enumeration.
   $hostLog="$Out\$name.host.log"
   $hostProcess=Start-Process -FilePath "$D\$name" -ArgumentList "`"$hostLog`" --host --normal-sentinel" -WindowStyle Hidden -PassThru
   $owned.Add($hostProcess);$hostPid=$hostProcess.Id
   $hostRoot=ProbeHandle $hostLog 'HOST_ROOT';$intermediate=ProbeHandle $hostLog 'HOST_INTERMEDIATE'
   $nestedLog="$Out\$name.nested.log"
   $nestedProcess=Start-Process -FilePath "$D\$name" -ArgumentList "`"$nestedLog`" --core-window" -WindowStyle Hidden -PassThru
   $owned.Add($nestedProcess);$nestedPid=$nestedProcess.Id
   $inject=& $V --json inject --pid $nestedPid | ConvertFrom-Json
   if(-not $inject.ok){throw 'nested target injection failed'}
   for($n=0;$n -lt 40;$n++){$nestedStatus=Vi $nestedPid @('status');if($nestedStatus.hwnd -ne '0x0'){break};Start-Sleep -Milliseconds 50}
   $nestedWindow=[IntPtr][long]::Parse($nestedStatus.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
   [ViFocusNative]::PostMessage($nestedWindow,0x8005,$intermediate,[IntPtr]::Zero)|Out-Null
   $reportedRoot=ProbeHandle $nestedLog 'HOSTED_ROOT'
   Check ($reportedRoot -eq $hostRoot) "$name CoreWindow attached through intermediate child to external host root"
   Set-FocusSentinel $sentinelState|Out-Null
   $nestedGrant=Json $nestedPid '{"cmd":"foreground-lease","ms":5000}'
   $nestedBefore=$sentinelState.hwnd
   $thief=Steal "$D\$name" $hostRoot;$nestedHeld=Sample $hostPid 200 1000;$nestedWitness=Witness $thief
   $nestedStatus=Vi $nestedPid @('status')
   Check ($nestedGrant.ok -and $nestedHeld.max_streak_ms -ge 100 -and $nestedHeld.ended_target -and $nestedWitness.observed) "$name hosted root really foreground under lease" "held=$($nestedHeld.max_streak_ms)ms, ended=$($nestedHeld.ended_target), witness=$($nestedWitness.observed)"
   Check $nestedStatus.really_foreground "$name status recognizes nested hosted target as really foreground"
   Handoff $nestedPid $nestedBefore
   $nestedRestore=$script:lastHandoff
   $nestedCancelled=Vi $nestedPid @('status');$nestedAfter=Sample $hostPid 250
   Check ($nestedRestore.ok -and $nestedRestore.foreground_restored -and $nestedCancelled.foreground_guard.lease_remaining_ms -eq 0 -and -not $nestedAfter.ended_target) "$name hosted restore cancels lease and relinquishes external root"
   # Always restore before destroying a still-foreground external host, including a red predicate control.
   Json $nestedPid '{"cmd":"foreground-lease","ms":0}'|Out-Null
   if((Fg).pid -eq $hostPid){$cleanup=Steal "$D\$name" $before;$cleanup.WaitForExit(1500)|Out-Null}
   $evidence.Add([pscustomobject]@{target=$name;cancel_grant=$cancelGrant;cancel_held=$cancelHeld;cancelled=$cancelled;after_cancel=$afterCancel;host_pid=$hostPid;host_root=('0x{0:X}' -f $hostRoot.ToInt64());nested_pid=$nestedPid;nested_grant=$nestedGrant;nested_witness=$nestedWitness;nested_held=$nestedHeld;nested_status=$nestedStatus;nested_restore=$nestedRestore;nested_cancelled=$nestedCancelled;nested_after=$nestedAfter})
   $evidence.Add([pscustomobject]@{target=$name;lease_witness=$leaseWitness;cancel_lease_witness=$cancelLeaseWitness;negative_witness=$negativeWitness;positive_witness=$positiveWitness;gain_witness=$gainWitness;busy_witness=$busyWitness;cancel_witness=$cancelWitness})
   $evidence.Add([pscustomobject]@{target=$name;launch=$r;startup_peak_ms=$max;startup_samples=$seen;initial=$s;positive=$pSample;unrequested=$sample;after=$after;api_status=$apis;busy=$duringWait;lease=$grant;held=$held;expired=$expired})
  } finally {
   foreach($id in @($nestedPid,$targetPid)){if($id){try{Json $id '{"cmd":"foreground-lease","ms":0}'|Out-Null;Handoff $id $before}catch{}}}
   if((Fg).pid -in @($targetPid,$nestedPid,$hostPid)) {try{$cleanup=Steal "$D\$name" $before;$cleanup.WaitForExit(1500)|Out-Null}catch{}}
   foreach($id in @($nestedPid,$hostPid)){if($id){Stop-Process -Id $id -Force -Confirm:$false -ErrorAction SilentlyContinue}}
   if($targetPid){try{Json $targetPid '{"cmd":"foreground-lease","ms":0}'|Out-Null;Handoff $targetPid $before}catch{};Stop-Process -Id $targetPid -Force -Confirm:$false -ErrorAction SilentlyContinue}
  }
 }
} finally {
 foreach($p in $owned){if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction SilentlyContinue}}
 Close-FocusSentinel $sentinelState
 $held=[IO.File]::ReadAllText($lockPath);if($held.Contains($token)){Remove-Item -LiteralPath $lockPath}
 $checks|ConvertTo-Json -Depth 7|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 12|Set-Content "$Out\evidence.json"
}
$failed=@($checks|Where-Object {-not $_.pass}).Count
"$($checks.Count-$failed) PASS / $failed FAIL"
if($failed){exit 1}
