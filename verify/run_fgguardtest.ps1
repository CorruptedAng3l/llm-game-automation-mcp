# Delayed foreground re-grab control (seen on a real Steam game: it regained the REAL foreground ~1.5 s
# after `launch` reported foreground_restored:true, when the launcher's dialog closed).
# The synthetic target asks a NON-injected child to make it really foreground 1.5 s after vinput's pipe appears
# (AttachThreadInput + SetForegroundWindow from another process = an external activation vinput cannot block).
# PASS = the steal really happened (positive control, independent of the code under test: the injected DLL's log
# records "REAL foreground GAINED"; the thief's own return value is advisory, SetForegroundWindow can return FALSE and
# still activate) AND launch handed it back and reported it AND, after the launch returned, the
# real foreground is not the target AND launch reported the re-grab. RED on builds without the guard.
# It touches the REAL foreground for a few ms, so it holds a lock file for ~15 s: -Lock, else $env:VINPUT_LOCK, else
# build\foreground.lock. Point every tool on the machine that may take the real foreground at the same file.
param([string]$CliDir = "$PSScriptRoot\..\dist", [string]$Out = "$PSScriptRoot\..\build\test\fgguard", [int]$LockWaitSec = 300, [string]$Lock = $env:VINPUT_LOCK)
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = (Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
if (-not ('ViFgNative' -as [type])) {
  Add-Type 'using System; using System.Runtime.InteropServices; public static class ViFgNative { [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow(); [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid); }'
}
function FgPid { $h = [ViFgNative]::GetForegroundWindow(); [uint32]$p = 0; [ViFgNative]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null; [pscustomobject]@{hwnd=$h; pid=[int]$p} }
if (-not $Lock) { New-Item -ItemType Directory -Force "$PSScriptRoot\..\build" | Out-Null; $Lock = "$PSScriptRoot\..\build\foreground.lock" }
$liveLock = [IO.Path]::GetFullPath($Lock)
$lockStream = $null; $t0 = Get-Date
while (-not $lockStream) {
  try { $lockStream = [IO.File]::Open($liveLock, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read) }
  catch [IO.IOException] { if (((Get-Date) - $t0).TotalSeconds -gt $LockWaitSec) { throw "foreground lock busy for $LockWaitSec s: $(Get-Content -Raw $liveLock -ErrorAction SilentlyContinue)" }; Start-Sleep 2 }
}
$body = [Text.Encoding]::UTF8.GetBytes((@{owner='vinput run_fgguardtest'; pid=$PID; time=(Get-Date).ToString('o'); purpose='synthetic delayed foreground steal, ~15 s'} | ConvertTo-Json -Compress))
$lockStream.Write($body, 0, $body.Length); $lockStream.Flush(); $lockStream.Dispose()
$results = @(); $owned = @()
try {
  foreach ($name in @('vi_testinput.exe', 'vi_testinput32.exe')) {
    $log = "$Out\$name.log"; Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
    $before = FgPid
    $privLock = "$Out\launch_$([guid]::NewGuid().ToString('N')).lock"
    $raw = & $V --json launch --exe "$D\$name" --args "`"$log`" -28000 600 420 260 --steal-fg-after-pipe 1500" --lock $privLock --settle 0 --timeout 10 2> "$Out\$name.stderr.txt"
    $reply = $raw | Where-Object { $_ -like '{*' } | Select-Object -Last 1 | ConvertFrom-Json
    if ($reply.pid) { $owned += Get-Process -Id $reply.pid -ErrorAction SilentlyContinue }
    $launchTick = [Environment]::TickCount
    # wait for the thief's verdict, then sample the real foreground
    $deadline = (Get-Date).AddSeconds(8); $thief = $null
    while ((Get-Date) -lt $deadline -and -not $thief) { Start-Sleep -Milliseconds 100; $thief = Select-String -LiteralPath $log -Pattern 'STEAL_FG thief_pid=\d+ thief_exit=(\d+)' -ErrorAction SilentlyContinue | Select-Object -First 1 }
    Start-Sleep -Milliseconds 400
    $after = FgPid
    $targetHolds = $reply.pid -and $after.pid -eq [int]$reply.pid
    $thiefExit = if ($thief) { [int]$thief.Matches[0].Groups[1].Value } else { -1 }
    # target log lines start with its GetTickCount: >0 = the steal came after launch returned (builds without the guard cannot see it)
    $stealVsReturn = if ($thief) { [long][uint32]$thief.Line.Split(' ')[0] - [long][uint32]$launchTick } else { $null }
    $regrabs = @($reply.foreground_regrabs | Where-Object { $_.restored })
    # independent positive control: the injected DLL's own log records the real-foreground transition
    $dllLog = if ($reply.pid) { "$(Split-Path $V)\run\logs\vinput_$($reply.pid).log" }
    $gained = if ($dllLog -and (Test-Path -LiteralPath $dllLog)) { @(Select-String -LiteralPath $dllLog -Pattern 'REAL foreground GAINED').Count } else { 0 }
    $r = [pscustomobject]@{
      target=$name; pid=$reply.pid; launch_ok=$reply.ok; reply_restored=$reply.foreground_restored; fg_guard_ms=$reply.fg_guard_ms
      regrabs=$reply.foreground_regrabs; thief_exit=$thiefExit; steal_ms_after_launch_returned=$stealVsReturn; fg_before_pid=$before.pid; fg_after_pid=$after.pid; target_holds_fg_after=$targetHolds
      dll_log_fg_gained=$gained
      pass = ($reply.ok -and $thief -and $gained -ge 1 -and -not $targetHolds -and $regrabs.Count -ge 1)
    }
    $results += $r
    if ($targetHolds) {
      # cleanup for the RED case: the target (injected, really foreground) hands the foreground back itself
      $hb = '0x{0:X}' -f $before.hwnd.ToInt64()
      & $V --pid $reply.pid json "{`"cmd`":`"restore-foreground`",`"hwnd`":`"$hb`"}" | Out-Null
      Start-Sleep -Milliseconds 300
      $r | Add-Member fg_after_cleanup_pid (FgPid).pid
    }
    foreach ($p in $owned) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }; $owned = @()
    Remove-Item -LiteralPath $privLock -ErrorAction SilentlyContinue
  }
} finally {
  foreach ($p in $owned) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
  $held = Get-Content -Raw -LiteralPath $liveLock -ErrorAction SilentlyContinue
  if ($held -and $held.Contains("`"pid`":$PID")) { Remove-Item -LiteralPath $liveLock -Force }
}
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$Out\result.json"
foreach ($r in $results) { "$(if ($r.pass) {'PASS'} else {'FAIL'}) $($r.target): thief_exit=$($r.thief_exit) target_holds_fg_after=$($r.target_holds_fg_after) regrabs=$(@($r.regrabs | Where-Object { $_ }).Count) launch_ok=$($r.launch_ok) restored=$($r.reply_restored)" }
if (@($results | Where-Object { -not $_.pass }).Count) { throw "FAIL delayed foreground guard; see $Out\result.json" }
'PASS delayed foreground re-grab is handed back and reported (x64 + x86)'
