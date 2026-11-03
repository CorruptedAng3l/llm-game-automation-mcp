# Owned, non-injected, independently witnessed foreground restoration destination.
# Caller MUST hold the canonical LIVE lock from before New-FocusSentinel through Close-FocusSentinel.
if(-not ('ViSentinelNative' -as [type])) { Add-Type 'using System;using System.Runtime.InteropServices;public static class ViSentinelNative{[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);[DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);[DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int n);[DllImport("user32.dll")]public static extern IntPtr SetWindowLongPtrW(IntPtr h,int n,IntPtr value);}' }
function Measure-FocusHwnd([IntPtr]$WindowHandle,[int]$DurationMs,[int]$AwaitMs=0) {
  $waitStart=[Environment]::TickCount64
  while($AwaitMs -gt 0 -and [ViSentinelNative]::GetForegroundWindow() -ne $WindowHandle -and [Environment]::TickCount64-$waitStart -lt $AwaitMs){Start-Sleep -Milliseconds 5}
  $began=[Environment]::TickCount64;$runStart=-1L;$longest=0L;$samples=0
  do {
    $now=[Environment]::TickCount64
    if([ViSentinelNative]::GetForegroundWindow() -eq $WindowHandle){if($runStart -lt 0){$runStart=$now};$samples++;$longest=[math]::Max($longest,$now-$runStart)}else{$runStart=-1L}
    Start-Sleep -Milliseconds 5
  }while([Environment]::TickCount64-$began -lt $DurationMs)
  [pscustomobject]@{count=$samples;max_streak_ms=$longest;elapsed_ms=[Environment]::TickCount64-$began;awaited_ms=$began-$waitStart;ended_target=([ViSentinelNative]::GetForegroundWindow() -eq $WindowHandle)}
}
function Invoke-SentinelWitness($State,[IntPtr]$WindowHandle,[string]$Label) {
  $State.serial++
  $witnessPath=Join-Path $State.out ("sentinel_{0}_{1}.witness.log" -f $Label,$State.serial)
  $info=[Diagnostics.ProcessStartInfo]::new($State.exe);$info.UseShellExecute=$false;$info.CreateNoWindow=$true
  $info.ArgumentList.Add('--steal');$info.ArgumentList.Add(('0x{0:X}' -f $WindowHandle.ToInt64()));$info.ArgumentList.Add($witnessPath)
  $worker=[Diagnostics.Process]::new();$worker.StartInfo=$info;[void]$worker.Start();$State.helpers.Add($worker)
  [pscustomobject]@{process=$worker;path=$witnessPath}
}
function Set-FocusSentinel($State) {
  if(-not $State -or $State.process.HasExited -or -not [ViSentinelNative]::IsWindow($State.hwnd)){throw 'owned foreground sentinel is unavailable'}
  $witness=Invoke-SentinelWitness $State $State.hwnd 'setup'
  $sample=Measure-FocusHwnd $State.hwnd 220 1000
  $finished=$witness.process.WaitForExit(2000)
  $raw=Get-Content -LiteralPath $witness.path -Raw -ErrorAction SilentlyContinue
  $observed=$finished -and $witness.process.ExitCode -eq 0 -and ($raw -match 'snapshot_seen=1|event_seen=1')
  $result=[pscustomobject]@{pid=$State.process.Id;hwnd=('0x{0:X}' -f $State.hwnd.ToInt64());witness=$raw;witness_path=$witness.path;observed=$observed;sample=$sample;pass=($observed -and $sample.max_streak_ms -ge 100 -and $sample.ended_target)}
  $State.activations.Add($result);$State.activations | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $State.out 'sentinel_activations.json')
  if(-not $result.pass){throw "owned foreground sentinel setup FAILED (held=$($sample.max_streak_ms)ms, witness=$observed); no test/skip is counted as passing; see $($State.out)\sentinel_activations.json"}
  return $result
}
function New-FocusSentinel([string]$ProbeExe,[string]$Out,[bool]$KeepNoActivate=$false) {
  New-Item -ItemType Directory -Force $Out | Out-Null
  $original=[ViSentinelNative]::GetForegroundWindow();[uint32]$originalPid=0;[ViSentinelNative]::GetWindowThreadProcessId($original,[ref]$originalPid)|Out-Null
  $sentinelLog=Join-Path $Out 'sentinel.log'
  $info=[Diagnostics.ProcessStartInfo]::new($ProbeExe);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.ArgumentList.Add($sentinelLog)
  if(-not $KeepNoActivate){$info.ArgumentList.Add('--normal-sentinel')}
  $sentinelProcess=[Diagnostics.Process]::new();$sentinelProcess.StartInfo=$info;[void]$sentinelProcess.Start()
  $state=[pscustomobject]@{process=$sentinelProcess;hwnd=[IntPtr]::Zero;exe=$ProbeExe;out=$Out;initial_hwnd=$original;initial_pid=$originalPid;serial=0;helpers=[Collections.Generic.List[Diagnostics.Process]]::new();activations=[Collections.Generic.List[object]]::new()}
  try {
    $until=[Environment]::TickCount64+3000
    do{$text=Get-Content -LiteralPath $sentinelLog -Raw -ErrorAction SilentlyContinue;if($text -match 'WINDOW hwnd=0x([0-9A-F]+)'){$state.hwnd=[IntPtr][long]::Parse($Matches[1],[Globalization.NumberStyles]::HexNumber);break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
    if($state.hwnd -eq [IntPtr]::Zero){throw 'sentinel created no window'}
    # The ordinary sentinel is CREATED normal-style, then initially shown without
    # activation. Paired regression mode preserves the fixture's old NOACTIVATE creation.
    $style=[ViSentinelNative]::GetWindowLongPtrW($state.hwnd,-20).ToInt64()
    $noActivate=($style -band 0x08000000) -ne 0
    [pscustomobject]@{pid=$sentinelProcess.Id;hwnd=('0x{0:X}'-f$state.hwnd.ToInt64());creation_mode=$(if($KeepNoActivate){'noactivate'}else{'normal'});actual_exstyle=('0x{0:X}'-f$style);noactivate=$noActivate}|ConvertTo-Json|Set-Content (Join-Path $Out 'sentinel_identity.json')
    if($noActivate -ne $KeepNoActivate){throw 'sentinel creation style does not match requested mode'}
    Set-FocusSentinel $state | Out-Null
    return $state
  } catch { Close-FocusSentinel $state;throw }
}
function Close-FocusSentinel($State) {
  if(-not $State){return}
  try {
    # Callers stop their other owned targets before Close. Only this exact live
    # sentinel HWND/PID may hand back; a different foreground is a newer choice.
    $current=[ViSentinelNative]::GetForegroundWindow();[uint32]$currentOwner=0
    [ViSentinelNative]::GetWindowThreadProcessId($current,[ref]$currentOwner)|Out-Null
    if($current -ne $State.hwnd -or $currentOwner -ne $State.process.Id){
      [pscustomobject]@{skipped=$true;reason='foreground outside owned sentinel; newer choice preserved';actual=('0x{0:X}'-f$current.ToInt64())}|ConvertTo-Json|Set-Content (Join-Path $State.out 'sentinel_cleanup.json')
      return
    }
    [uint32]$stillOwner=0;[ViSentinelNative]::GetWindowThreadProcessId($State.initial_hwnd,[ref]$stillOwner)|Out-Null
    if([ViSentinelNative]::IsWindow($State.initial_hwnd) -and $stillOwner -eq $State.initial_pid){
      if([ViSentinelNative]::GetForegroundWindow() -ne $current){return}
      $cleanup=Invoke-SentinelWitness $State $State.initial_hwnd 'cleanup';$cleanup.process.WaitForExit(1500)|Out-Null
      [pscustomobject]@{requested=('0x{0:X}'-f$State.initial_hwnd.ToInt64());actual=('0x{0:X}'-f[ViSentinelNative]::GetForegroundWindow().ToInt64());best_effort=$true}|ConvertTo-Json|Set-Content (Join-Path $State.out 'sentinel_cleanup.json')
    }
  } catch { $_.ToString()|Set-Content (Join-Path $State.out 'sentinel_cleanup_error.txt') }
  finally {
    foreach($worker in $State.helpers){if(-not $worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false}}
    if(-not $State.process.HasExited){Stop-Process -Id $State.process.Id -Force -Confirm:$false}
  }
}
