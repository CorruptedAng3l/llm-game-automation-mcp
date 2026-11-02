# CLI-output/launch regression; launches only the nonactivating synthetic test target.
# Baseline/current CLI directories may differ; TargetDir must contain the new no-activate target.
param(
  [string]$CliDir = "$PSScriptRoot\..\dist",
  [string]$TargetDir = "$PSScriptRoot\..\dist",
  [string]$Out = "$PSScriptRoot\..\build\test\launchtest"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$Out = [IO.Path]::GetFullPath($Out)
if (-not $Out.StartsWith("$root\", [StringComparison]::OrdinalIgnoreCase)) { throw 'Launch-test artifacts must stay within this repository' }
New-Item -ItemType Directory -Force -Path $Out, "$Out\temp" | Out-Null
$Out = (Resolve-Path $Out).Path
$V = (Resolve-Path "$CliDir\vinput.exe").Path
$oldTemp = $env:TEMP; $oldTmp = $env:TMP
$env:TEMP = "$Out\temp"; $env:TMP = "$Out\temp"
if (-not ('ViLaunchNative' -as [type])) {
  Add-Type 'using System; using System.Runtime.InteropServices; public static class ViLaunchNative { [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow(); [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid); }'
}
$checks = [Collections.Generic.List[object]]::new()
$foregroundEvidence = [Collections.Generic.List[object]]::new()
$owned = [Collections.Generic.List[Diagnostics.Process]]::new()
$runId = [guid]::NewGuid().ToString('N')
function Check([bool]$condition, [string]$name, [string]$detail = '') {
  $checks.Add([pscustomobject]@{pass=$condition; name=$name; detail=$detail})
  "$(if ($condition) {'PASS'} else {'FAIL'}) $name$(if ($detail) {': '+$detail})"
}
function Check-Foreground([IntPtr]$before, [string]$name) {
  $after = [ViLaunchNative]::GetForegroundWindow()
  [uint32]$beforePid = 0; [uint32]$afterPid = 0
  [ViLaunchNative]::GetWindowThreadProcessId($before, [ref]$beforePid) | Out-Null
  [ViLaunchNative]::GetWindowThreadProcessId($after, [ref]$afterPid) | Out-Null
  $isOurs = @($owned | Where-Object Id -eq $afterPid).Count -gt 0
  $equality = if ($before -eq $after) { 'UNCHANGED' } elseif ($isOurs) { 'TARGET_TOOK_FOREGROUND' } else { 'INCONCLUSIVE_EXTERNALLY_CHANGED' }
  $evidence = [pscustomobject]@{name=$name; before_hwnd=('0x{0:X}' -f $before.ToInt64()); before_pid=$beforePid; after_hwnd=('0x{0:X}' -f $after.ToInt64()); after_pid=$afterPid; target_took_foreground=$isOurs; original_window_equality=$equality}
  $foregroundEvidence.Add($evidence)
  Check (-not $isOurs) $name ($evidence | ConvertTo-Json -Compress)
}
function Quote-NativeArgument([string]$value) {
  # Standard CommandLineToArgvW quoting, including embedded quotes/trailing backslashes.
  if ($value.Length -gt 0 -and $value -notmatch '[\s"]') { return $value }
  $value = [regex]::Replace($value, '(\\*)"', '$1$1\"')
  $value = [regex]::Replace($value, '(\\+)$', '$1$1')
  return '"' + $value + '"'
}
function Invoke-Cli([string]$name, [string[]]$arguments, [string]$expectedTarget = '') {
  $startedAt = [DateTime]::UtcNow
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $V
  $psi.Arguments = ($arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
  $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
  $proc = [Diagnostics.Process]::Start($psi)
  try {
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit(45000)) {
      # This is our CLI PID only. No preexisting process is stopped.
      Stop-Process -Id $proc.Id -Force -Confirm:$false
      throw "$name timed out; our CLI stopped by PID"
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText("$Out\$name.stdout.txt", $stdout)
    [IO.File]::WriteAllText("$Out\$name.stderr.txt", $stderr)
    $lines = @($stdout -split '\r?\n' | Where-Object { $_.Trim().Length -gt 0 })
    $json = [Collections.Generic.List[object]]::new()
    foreach ($line in $lines) {
      try { $obj = $line | ConvertFrom-Json }
      catch { $json.Add($null); continue }
      $json.Add($obj)
      if ($expectedTarget -and $obj.pid) {
        $targetPid = [int]$obj.pid
        if (-not @($owned | Where-Object Id -eq $targetPid).Count) {
          $target = Get-Process -Id $targetPid -ErrorAction SilentlyContinue
          if ($target -and $target.Path -eq $expectedTarget -and $target.StartTime.ToUniversalTime() -ge $startedAt.AddSeconds(-2)) { $owned.Add($target) }
          elseif ($target) { throw "Returned PID $targetPid does not identify our newly launched target" }
        }
      }
    }
    return [pscustomobject]@{exit_code=$proc.ExitCode; stdout=$stdout; stderr=$stderr; lines=$lines; json=$json.ToArray()}
  } finally { $proc.Dispose() }
}
function Last-Reply($result) { @($result.json | Where-Object { $null -ne $_ }) | Select-Object -Last 1 }
function Check-Json($result, [string]$name, [bool]$expectOk) {
  $reply = Last-Reply $result
  Check ($result.lines.Count -eq 1 -and $result.json.Count -eq 1 -and $null -ne $reply) "$name one JSON stdout line" "lines=$($result.lines.Count)"
  Check ($null -ne $reply -and [bool]$reply.ok -eq $expectOk -and (($result.exit_code -eq 0) -eq $expectOk)) "$name exit/result agree" "exit=$($result.exit_code)"
}
function Stop-OwnedTargets {
  foreach ($target in $owned) { if (-not $target.HasExited) { Stop-Process -Id $target.Id -Force -Confirm:$false }; $target.Dispose() }
  $owned.Clear()
}
try {
  # A global flag before the command must select the command's own validation.
  foreach ($position in @('prefix','suffix')) {
    $arguments = if ($position -eq 'prefix') { @('--json','launch') } else { @('launch','--json') }
    $r = Invoke-Cli "invalid_$position" $arguments
    Check-Json $r "--json $position validation" $false
    $reply = Last-Reply $r
    Check ($null -ne $reply -and $reply.error -like '*launch needs*') "--json $position selects launch" ($reply | ConvertTo-Json -Compress)
  }
  $r = Invoke-Cli 'invalid_exe' @('--json','launch','--exe',"$Out\never_exists_$runId.exe",'--timeout','2','--settle','0')
  Check-Json $r 'invalid executable' $false
  $reply = Last-Reply $r
  Check ($null -ne $reply -and $reply.error -like '*CreateProcess*') 'invalid executable reaches process creation' ($reply | ConvertTo-Json -Compress)
  foreach ($arch in @('x64','x86')) {
    $targetName = if ($arch -eq 'x64') { 'vi_testinput.exe' } else { 'vi_testinput32.exe' }
    $target = (Resolve-Path "$TargetDir\$targetName").Path
    foreach ($mode in @('noinject','injected')) {
      foreach ($position in @('prefix','suffix')) {
        $name = "${arch}_${mode}_${position}"
        $fgBefore = [ViLaunchNative]::GetForegroundWindow()
        $arguments = @('launch','--exe',$target,'--args',"`"$Out\${name}_${runId}.log`" -28000 600 420 260",'--timeout','8','--settle','0')
        if ($mode -eq 'noinject') { $arguments += '--no-inject' }
        if ($position -eq 'prefix') { $arguments = @('--json') + $arguments } else { $arguments += '--json' }
        try {
          $r = Invoke-Cli $name $arguments $target
          Check-Json $r $name $true
          $reply = Last-Reply $r
          Check ($null -ne $reply -and $reply.pid -gt 0 -and [bool]$reply.injected -eq ($mode -eq 'injected')) "$name returns owned PID and injection mode"
          Check-Foreground $fgBefore "$name never takes real foreground"
          if ($position -eq 'suffix') { Check ($r.stderr.Contains('[vinput]')) "$name progress is on stderr" }
        } finally { Stop-OwnedTargets }
      }
    }
  }
  # Failure inside injection must still emit a single result containing our target PID.
  $target = (Resolve-Path "$TargetDir\vi_testinput.exe").Path
  $fgBefore = [ViLaunchNative]::GetForegroundWindow()
  foreach ($position in @('prefix','suffix')) {
    try {
      $arguments = @('launch','--exe',$target,'--args',"`"$Out\missing_dll_${position}_${runId}.log`" -28000 600 420 260",'--dll',"$Out\absent_${runId}.dll",'--timeout','8','--settle','0')
      if ($position -eq 'prefix') { $arguments = @('--json') + $arguments } else { $arguments += '--json' }
      $r = Invoke-Cli "missing_dll_$position" $arguments $target
      Check-Json $r "missing DLL launch $position" $false
      $reply = Last-Reply $r
      Check ($null -ne $reply -and $reply.pid -gt 0 -and $reply.error -match 'inject|dll') "missing DLL $position reaches injection" ($reply | ConvertTo-Json -Compress)
      Check-Foreground $fgBefore "failed injection $position never takes real foreground"
    } finally { Stop-OwnedTargets }
  }
  # This is our fixture containing a different owner; no global/shared lock is accessed.
  $foreign = "$Out\foreign_${runId}.lock"
  [IO.File]::WriteAllText($foreign, 'foreign synthetic fixture; leave untouched')
  try {
    $before = (Get-FileHash -LiteralPath $foreign -Algorithm SHA256).Hash
    $fgBefore = [ViLaunchNative]::GetForegroundWindow()
    $r = Invoke-Cli 'foreign_lock' @('launch','--exe',$target,'--lock',$foreign,'--owner','other-launchtest','--lock-retry','1','--lock-max-wait','1','--timeout','2','--settle','0','--json') $target
    Check-Json $r 'foreign lock refusal' $false
    $reply = Last-Reply $r
    Check ($null -ne $reply -and $reply.error -match 'lock') 'foreign lock reaches lock validation' ($reply | ConvertTo-Json -Compress)
    Check ((Get-FileHash -LiteralPath $foreign -Algorithm SHA256).Hash -eq $before) 'foreign lock exact bytes retained'
    Check-Foreground $fgBefore 'foreign lock refusal never takes real foreground'
  } finally { Remove-Item -LiteralPath $foreign -Force }
  $failed = @($checks | Where-Object { -not $_.pass })
  [pscustomobject]@{ok=($failed.Count -eq 0); cli=$V; target_dir=(Resolve-Path $TargetDir).Path; checks=$checks.ToArray(); foreground_evidence=$foregroundEvidence.ToArray(); failures=$failed.Count} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$Out\result.json"
  if ($failed.Count) { throw "$($failed.Count) launch regression checks failed; see $Out\result.json" }
  "PASS launch JSON/foreground/lock controls: $($checks.Count) checks"
} finally {
  Stop-OwnedTargets
  $env:TEMP = $oldTemp; $env:TMP = $oldTmp
}
