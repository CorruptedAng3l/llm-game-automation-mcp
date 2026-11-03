# Future external host recorded before adoption must not become its own restore destination.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\hosthistory",[int]$LockWaitSec=300,[switch]$NoInjectLaunch)
$ErrorActionPreference='Stop';$D=(Resolve-Path $TargetDir).Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
. "$PSScriptRoot\_focus_sentinel.ps1"
if(-not ('ViHistoryNative' -as [type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViHistoryNative{[StructLayout(LayoutKind.Sequential)]struct Last{public uint size,time;}[DllImport("user32.dll")]static extern bool GetLastInputInfo(ref Last value);[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);public static uint Idle(){Last value=new Last();value.size=8;return GetLastInputInfo(ref value)?unchecked((uint)Environment.TickCount-value.time):0;}}'}
$owned=@();$sentinel=$null;$checks=@();$evidence=@();$lock=$(if($env:VINPUT_LOCK){$env:VINPUT_LOCK}else{New-Item -ItemType Directory -Force "$PSScriptRoot\..\build"|Out-Null;"$PSScriptRoot\..\build\foreground.lock"});$token=[guid]::NewGuid().ToString('N');$stream=$null;$start=[Environment]::TickCount64
function Check($ok,$name){$script:checks+=[pscustomobject]@{pass=[bool]$ok;name=$name};"$(if($ok){'PASS'}else{'FAIL'}) $name"}
function Vi([int]$GamePid,[string[]]$Words){$raw=& $V --json --pid $GamePid @Words;$reply=$raw|ConvertFrom-Json;$reply}
function Json([int]$GamePid,[string]$Body){Vi $GamePid @('json',$Body)}
function RestoreBody([IntPtr]$Window){'{"cmd":"restore-foreground","hwnd":"0x'+$Window.ToInt64().ToString('X')+'"}'}
function Quiet { $until=[Environment]::TickCount64+30000;while([ViHistoryNative]::Idle() -lt 1000){if([Environment]::TickCount64 -gt $until){throw 'Physical input stayed active; synthetic foreground setup aborted'};Start-Sleep -Milliseconds 100} }
function Start-HistoryCli([string]$File,[string[]]$Words){$info=[Diagnostics.ProcessStartInfo]::new($File);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;foreach($word in $Words){$info.ArgumentList.Add($word)};$p=[Diagnostics.Process]::new();$p.StartInfo=$info;[void]$p.Start();$script:owned+=$p;[pscustomobject]@{process=$p;stdout=$p.StandardOutput.ReadToEndAsync();stderr=$p.StandardError.ReadToEndAsync()}}
function Handle([string]$Log,[string]$Label){$until=[Environment]::TickCount64+3000;do{$text=Get-Content -LiteralPath $Log -Raw -ErrorAction SilentlyContinue;if($text -match "$Label hwnd=0x([0-9A-F]+)"){return [IntPtr]::new([Convert]::ToInt64($Matches[1],16))};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "No fresh $Label"}
while(-not $stream){try{$stream=[IO.File]::Open($lock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)}catch [IO.IOException]{if([Environment]::TickCount64-$start -gt $LockWaitSec*1000){throw 'canonical LIVE lock busy; never removed'};Start-Sleep -Seconds 1}}
$bytes=[Text.Encoding]::UTF8.GetBytes((@{owner='vinput host-history controls';pid=$PID;token=$token;time=(Get-Date -Format o)}|ConvertTo-Json -Compress));$stream.Write($bytes,0,$bytes.Length);$stream.Flush();$stream.Dispose()
try{
 $sentinel=New-FocusSentinel "$D\vi_focusprobe.exe" "$Out\sentinel"
 foreach($arch in @('','32')){
  $hostProcess=$null;$child=$null
  try{
   Quiet;Set-FocusSentinel $sentinel|Out-Null
   $hostLog="$Out\host$arch.log";$childLog="$Out\child$arch.log"
   foreach($file in @($hostLog,$childLog)){if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file}}
   $hostProcess=Start-Process "$D\vi_focusprobe$arch.exe" -ArgumentList "`"$hostLog`" --host --normal-sentinel" -WindowStyle Hidden -PassThru;$owned+=$hostProcess
   $hostRoot=Handle $hostLog 'HOST_ROOT';$intermediate=Handle $hostLog 'HOST_INTERMEDIATE'
   # Ordinary uninjected host: stable after external helper detaches its input queue.
   $style=[ViSentinelNative]::GetWindowLongPtrW($hostRoot,-20).ToInt64();[ViSentinelNative]::SetWindowLongPtrW($hostRoot,-20,[IntPtr]($style -band (-bnot 0x08000000)))|Out-Null
   if($NoInjectLaunch){
    $cliPath=if($arch){Join-Path (Split-Path $V) 'vinput32.exe'}else{$V}
    $launch=Start-HistoryCli $cliPath @('--json','launch','--exe',"$D\vi_focusprobe$arch.exe",'--args',"`"$childLog`" --core-window",'--no-inject','--fg-guard','3','--settle','0','--timeout','10')
    $childWindow=Handle $childLog 'WINDOW';[uint32]$childPid=0;[ViHistoryNative]::GetWindowThreadProcessId($childWindow,[ref]$childPid)|Out-Null;$child=Get-Process -Id $childPid;$owned+=$child
    Quiet;Set-FocusSentinel $sentinel|Out-Null
    Check ([ViSentinelNative]::GetForegroundWindow() -eq $sentinel.hwnd -and -not $launch.process.HasExited) "$arch uninjected CLI child starts with witnessed older sentinel"
    $witness=Invoke-SentinelWitness $sentinel $hostRoot "cli_future_host$arch";$held=Measure-FocusHwnd $hostRoot 220 1000;$finished=$witness.process.WaitForExit(2000);$raw=Get-Content -LiteralPath $witness.path -Raw
    $heldValid=$finished -and $witness.process.ExitCode -eq 0 -and $raw -match 'snapshot_seen=1|event_seen=1' -and $held.max_streak_ms -ge 100 -and $held.ended_target
    Check $heldValid "$arch CLI future host really holds foreground before adoption";if(-not $heldValid){throw 'CLI future host positive invalid'}
    [ViHistoryNative]::PostMessage($childWindow,0x8005,$intermediate,[IntPtr]::Zero)|Out-Null;$reportedRoot=Handle $childLog 'HOSTED_ROOT'
    Check ($reportedRoot -eq $hostRoot) "$arch CLI child adopted by previously external host"
    Start-Sleep -Milliseconds 350;$actual=[ViSentinelNative]::GetForegroundWindow()
    Check ($actual -eq $sentinel.hwnd) "$arch CLI skips its own hosted root and restores older sentinel without DLL guard"
    if(-not $launch.process.WaitForExit(8000)){throw 'CLI no-inject guard never finished'}
    $stdout=$launch.stdout.GetAwaiter().GetResult();$stderr=$launch.stderr.GetAwaiter().GetResult();$stdout|Set-Content "$Out\cli$arch.json";$stderr|Set-Content "$Out\cli$arch.stderr.log";$reply=$stdout|ConvertFrom-Json
    $regrabs=@($reply.foreground_regrabs|Where-Object{$_.restored})
    Check ($reply.ok -and $reply.foreground_restored -and $reply.injected -eq $false -and $reply.pid -eq $child.Id -and $regrabs.Count -gt 0) "$arch CLI reports actual hosted handback with no injection"
    $evidence+=[pscustomobject]@{arch=$arch;no_inject=$true;host_root=('0x{0:X}'-f$hostRoot.ToInt64());held=$held;held_witness=$raw;actual_after=('0x{0:X}'-f$actual.ToInt64());launch=$reply;stderr=$stderr}
    continue
   }
   $child=Start-Process "$D\vi_focusprobe$arch.exe" -ArgumentList "`"$childLog`" --core-window" -WindowStyle Hidden -PassThru;$owned+=$child
   $inject=Vi $child.Id @('inject');if(-not $inject.ok){throw 'own CoreWindow injection failed'}
   $until=[Environment]::TickCount64+3000
   do{$initial=Vi $child.Id @('status');if($initial.hwnd -ne '0x0' -and $initial.foreground_guard.armed -and $initial.foreground_guard.restore_hwnd -eq ('0x{0:X}'-f$sentinel.hwnd.ToInt64())){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
   # Injection takes ~2s. Establish the exact older destination afterwards so a
   # newer external foreground choice during injection is not misclassified as a failure.
   Quiet;Set-FocusSentinel $sentinel|Out-Null;Start-Sleep -Milliseconds 40;$initial=Vi $child.Id @('status')
   $olderValid=$initial.foreground_guard.armed -and -not $initial.really_foreground -and $initial.foreground_guard.restore_hwnd -eq ('0x{0:X}'-f$sentinel.hwnd.ToInt64())
   Check $olderValid "$arch standalone injected child learns older sentinel"
   if(-not $olderValid){throw 'Fresh older sentinel history precondition failed'}
   $childWindow=[IntPtr]::new([Convert]::ToInt64($initial.hwnd.Substring(2),16))
   $witness=Invoke-SentinelWitness $sentinel $hostRoot "future_host$arch";$held=Measure-FocusHwnd $hostRoot 220 1000;$finished=$witness.process.WaitForExit(2000);$raw=Get-Content -LiteralPath $witness.path -Raw
   $outside=Vi $child.Id @('status');$heldValid=$finished -and $witness.process.ExitCode -eq 0 -and $raw -match 'snapshot_seen=1|event_seen=1' -and $held.max_streak_ms -ge 100 -and $held.ended_target
   Check ($heldValid -and -not $outside.really_foreground -and $outside.foreground_guard.restore_hwnd -eq ('0x{0:X}'-f$hostRoot.ToInt64())) "$arch future host really holds foreground and is recorded outside target"
   if(-not $heldValid){throw 'Future host positive invalid; no restoration claim is possible'}
   [ViHistoryNative]::PostMessage($childWindow,0x8005,$intermediate,[IntPtr]::Zero)|Out-Null;$reportedRoot=Handle $childLog 'HOSTED_ROOT'
   Check ($reportedRoot -eq $hostRoot) "$arch child adopted under previously external host root"
   Start-Sleep -Milliseconds 400;$after=Vi $child.Id @('status');$actual=[ViSentinelNative]::GetForegroundWindow()
   Check (-not $after.really_foreground -and $actual -eq $sentinel.hwnd -and $after.foreground_guard.restored_count -gt $outside.foreground_guard.restored_count) "$arch guard skips current own host and restores older sentinel with counter proof"
   # Revoke policy temporarily to exercise explicit self-host destination rejection.
   Json $child.Id '{"cmd":"foreground-lease","ms":5000}'|Out-Null
   $witness2=Invoke-SentinelWitness $sentinel $hostRoot "explicit_host$arch";$explicitHeld=Measure-FocusHwnd $hostRoot 150 1000;$witness2.process.WaitForExit(2000)|Out-Null
   $self=Json $child.Id (RestoreBody $hostRoot)
   Check (-not $self.ok -and $self.error -match 'outside the target') "$arch explicit restore rejects current own hosted root"
   $clean=Json $child.Id (RestoreBody $sentinel.hwnd);Start-Sleep -Milliseconds 60
   Check ($clean.ok -and [ViSentinelNative]::GetForegroundWindow() -eq $sentinel.hwnd) "$arch explicit valid sentinel restore still works"
   $evidence+=[pscustomobject]@{arch=$arch;initial=$initial;host_root=('0x{0:X}'-f$hostRoot.ToInt64());held=$held;held_witness=$raw;outside=$outside;adopted_root=('0x{0:X}'-f$reportedRoot.ToInt64());after=$after;actual_after=('0x{0:X}'-f$actual.ToInt64());self_restore=$self;explicit_held=$explicitHeld;cleanup=$clean}
  }finally{
   if(-not $NoInjectLaunch -and $child -and -not $child.HasExited){try{Json $child.Id (RestoreBody $sentinel.hwnd)|Out-Null}catch{}}
   if($sentinel){Set-FocusSentinel $sentinel|Out-Null}
   foreach($process in @($child,$hostProcess)){if($process -and -not $process.HasExited){Stop-Process -Id $process.Id -Force}}
  }
 }
}finally{
 foreach($process in $owned){if(-not $process.HasExited){Stop-Process -Id $process.Id -Force}}
 Close-FocusSentinel $sentinel
 $held=[IO.File]::ReadAllText($lock);if($held.Contains($token)){Remove-Item -LiteralPath $lock}
 $checks|ConvertTo-Json -Depth 5|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 15|Set-Content "$Out\evidence.json"
}
$failed=@($checks|Where-Object{-not $_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
