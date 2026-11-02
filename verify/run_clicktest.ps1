param(
  [string]$CliDir = "$PSScriptRoot\..\dist",
  [string]$TargetDir = "$PSScriptRoot\..\dist",
  [string]$Out = "$PSScriptRoot\..\build\test\clicktest"
)
$ErrorActionPreference = 'Stop'
$V = (Resolve-Path "$CliDir\vinput.exe").Path
$T = (Resolve-Path "$TargetDir\vi_testinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
if (-not ('ViClickNative' -as [type])) {
  Add-Type 'using System; using System.Runtime.InteropServices; public static class ViClickNative { [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow(); [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid); }'
}
$fg = [ViClickNative]::GetForegroundWindow()
$proc = $null
try {
  $targetLog = "$Out\target.log"
  $proc = Start-Process -FilePath $T -ArgumentList "`"$targetLog`" -28000 600 640 360 --hover-gated" -WindowStyle Hidden -PassThru
  $injected = & $V inject --pid $proc.Id | ConvertFrom-Json
  if (-not $injected.ok) { throw "Injection failed: $($injected | ConvertTo-Json -Compress)" }
  $ready = $false
  for ($i=0; $i -lt 50; $i++) {
    $s = & $V --pid $proc.Id status | ConvertFrom-Json
    if ($s.hwnd -ne '0x0' -and $s.counters.present_ours -gt 0) { $ready=$true; break }
    Start-Sleep -Milliseconds 100
  }
  if (-not $ready) { throw 'Target not ready' }
  [uint32]$foregroundPid=0
  [ViClickNative]::GetWindowThreadProcessId([ViClickNative]::GetForegroundWindow(),[ref]$foregroundPid) | Out-Null
  if ($s.really_foreground -or $foregroundPid -eq $proc.Id) { throw 'Synthetic target took foreground during startup' }
  $reply = & $V --pid $proc.Id click left 50 40 | ConvertFrom-Json
  Start-Sleep -Milliseconds 150
  $first = Get-Content -LiteralPath $targetLog -Raw
  $result = [ordered]@{
    ok = ($reply.ok -and $reply.cursor -eq 'virtual' -and $first.Contains('CLICK_ACCEPT') -and -not $first.Contains('CLICK_REJECT'))
    first_click_accepted = $first.Contains('CLICK_ACCEPT')
    cursor_reported = $reply.cursor
    foreground_preserved = ([ViClickNative]::GetForegroundWindow() -eq $fg)
    target_background = ($foregroundPid -ne $proc.Id)
    reply = $reply
  }
  $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$Out\result.json"
  $result | ConvertTo-Json -Depth 6 -Compress
  [ViClickNative]::GetWindowThreadProcessId([ViClickNative]::GetForegroundWindow(),[ref]$foregroundPid) | Out-Null
  if (-not $result.ok -or $foregroundPid -eq $proc.Id) { throw 'Click regression failed (see result.json and target.log)' }
  # The same single-command click must work after inactivity too.
  Start-Sleep -Milliseconds 1100
  $again = & $V --pid $proc.Id click left 80 70 | ConvertFrom-Json
  Start-Sleep -Milliseconds 150
  $all = Get-Content -LiteralPath $targetLog -Raw
  if (-not $again.ok -or ([regex]::Matches($all, 'CLICK_ACCEPT')).Count -ne 2 -or $all.Contains('CLICK_REJECT')) { throw 'Idle click regression failed' }
  if($result.foreground_preserved) { 'PASS first click and idle click; virtual cursor; original foreground unchanged' }
  else { 'PASS first click and idle click; virtual cursor; target stayed background (external foreground window changed)' }
} finally {
  if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -Confirm:$false }
}
