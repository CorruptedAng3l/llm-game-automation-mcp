# Exact same permanent-NOACTIVATE destination and synthetic hosted topology for paired builds.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\hosted_noactivate",[int]$LockWaitSec=300)
$ErrorActionPreference='Stop';$V=(Resolve-Path "$CliDir\vinput.exe").Path;$D=(Resolve-Path $TargetDir).Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
. "$PSScriptRoot\_focus_sentinel.ps1"
if(-not('ViHostedNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViHostedNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'}
$owned=[Collections.Generic.List[Diagnostics.Process]]::new();$results=[Collections.Generic.List[object]]::new();$sentinel=$null
function Vi($GamePid,[string[]]$Arguments){& $V --json --pid $GamePid @Arguments|ConvertFrom-Json}
function Start-Probe($Exe,$Log,$Mode){$info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.ArgumentList.Add($Log);$info.ArgumentList.Add($Mode);$worker=[Diagnostics.Process]::new();$worker.StartInfo=$info;[void]$worker.Start();$owned.Add($worker);return $worker}
function Handle($Log,$Label){$until=[Environment]::TickCount64+3000;do{$text=Get-Content -LiteralPath $Log -Raw -ErrorAction SilentlyContinue;if($text-match($Label+' hwnd=0x([0-9A-F]+)')){return [IntPtr][long]::Parse($Matches[1],[Globalization.NumberStyles]::HexNumber)};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until);throw "missing $Label"}
$lock=$(if($env:VINPUT_LOCK){$env:VINPUT_LOCK}else{New-Item -ItemType Directory -Force "$PSScriptRoot\..\build"|Out-Null;"$PSScriptRoot\..\build\foreground.lock"});$token=[guid]::NewGuid().ToString('N');$stream=$null;$began=[Environment]::TickCount64
while(-not$stream){try{$stream=[IO.File]::Open($lock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)}catch [IO.IOException]{if([Environment]::TickCount64-$began-gt$LockWaitSec*1000){throw 'canonical LIVE lock busy; never removed'};Start-Sleep -Seconds 1}}
$bytes=[Text.Encoding]::UTF8.GetBytes((@{owner='vinput paired hosted NOACTIVATE';pid=$PID;token=$token;time=(Get-Date -Format o)}|ConvertTo-Json -Compress));$stream.Write($bytes,0,$bytes.Length);$stream.Dispose()
try{
 $sentinel=New-FocusSentinel "$D\vi_focusprobe.exe" "$Out\sentinel" $true
 foreach($arch in @('','32')){
  $hostProcess=$null;$nested=$null
  try{
   $exe="$D\vi_focusprobe$arch.exe";$hostLog="$Out\host$arch.log";$nestedLog="$Out\nested$arch.log"
   $hostProcess=Start-Probe $exe $hostLog '--host';$root=Handle $hostLog 'HOST_ROOT';$middle=Handle $hostLog 'HOST_INTERMEDIATE'
   $nested=Start-Probe $exe $nestedLog '--core-window';$injected=& $V --json inject --pid $nested.Id|ConvertFrom-Json;if(-not$injected.ok){throw 'nested injection failed'}
   for($i=0;$i-lt50;$i++){$status=Vi $nested.Id @('status');if($status.hwnd-ne'0x0'){break};Start-Sleep -Milliseconds 50}
   $child=[IntPtr][long]::Parse($status.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
   [ViHostedNative]::PostMessage($child,0x8005,$middle,[IntPtr]::Zero)|Out-Null
   if((Handle $nestedLog 'HOSTED_ROOT')-ne$root){throw 'nested host topology was not established'}
   $sentinelPositive=Set-FocusSentinel $sentinel
   $grant=Vi $nested.Id @('json','{"cmd":"foreground-lease","ms":5000}');if(-not$grant.ok){throw 'hosted lease failed'}
   $witness=Invoke-SentinelWitness $sentinel $root 'hostpositive';$held=Measure-FocusHwnd $root 200 1000
   $finished=$witness.process.WaitForExit(2000);$witnessRaw=Get-Content -LiteralPath $witness.path -Raw
   $observed=$finished-and$witness.process.ExitCode-eq0-and($witnessRaw-match'snapshot_seen=1|event_seen=1')
   $before=Vi $nested.Id @('status')
   if(-not($observed-and$held.max_streak_ms-ge100-and$held.ended_target-and$before.really_foreground)){throw "hosted positive failed: held=$($held.max_streak_ms), ended=$($held.ended_target), witness=$observed"}
   $reply=Vi $nested.Id @('json',('{"cmd":"restore-foreground","hwnd":"'+('0x{0:X}'-f$sentinel.hwnd.ToInt64())+'"}'))
   $immediate=('0x{0:X}'-f([ViSentinelNative]::GetForegroundWindow()).ToInt64());$after=Measure-FocusHwnd $root 250
   $afterStatus=Vi $nested.Id @('status')
   $result=[pscustomobject]@{arch=if($arch){32}else{64};sentinel_noactivate=([ViSentinelNative]::GetWindowLongPtrW($sentinel.hwnd,-20).ToInt64()-band0x08000000)-ne0;sentinel_positive=$sentinelPositive;host_root=('0x{0:X}'-f$root.ToInt64());host_positive=$held;host_witness=$witnessRaw;host_observed=$observed;really_foreground_before=$before.really_foreground;reply=$reply;immediate_hwnd=$immediate;host_after=$after;really_foreground_after=$afterStatus.really_foreground;pass=($reply.ok-and$reply.foreground_restored-and-not$after.ended_target-and-not$afterStatus.really_foreground)}
   $results.Add($result);"$(if($result.pass){'PASS'}else{'FAIL'}) hosted$arch NOACTIVATE destination: held=$($held.max_streak_ms) witness=$observed reply=$($reply|ConvertTo-Json -Compress) after_host=$($after.ended_target)"
   if($status.log_path){Copy-Item -LiteralPath $status.log_path -Destination "$Out\nested$arch.dll.log" -ErrorAction SilentlyContinue}
  }finally{
   if($nested-and-not$nested.HasExited){try{Vi $nested.Id @('json','{"cmd":"foreground-lease","ms":0}')|Out-Null}catch{}}
   if($sentinel){$cleanup=Invoke-SentinelWitness $sentinel $sentinel.hwnd 'casecleanup';$cleanup.process.WaitForExit(1500)|Out-Null}
   foreach($worker in @($nested,$hostProcess)){if($worker-and-not$worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false}}
  }
 }
}finally{foreach($worker in $owned){if(-not$worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false}};Close-FocusSentinel $sentinel;$heldLock=Get-Content -LiteralPath $lock -Raw;if($heldLock.Contains($token)){Remove-Item -LiteralPath $lock};$results|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json"}
$failed=@($results|Where-Object {-not$_.pass}).Count;"$($results.Count-$failed) PASS / $failed FAIL";if($failed){exit 1}
