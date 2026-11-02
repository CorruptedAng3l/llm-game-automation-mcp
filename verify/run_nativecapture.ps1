# Native frame capture on synthetic targets that draw a known, changing pattern (test\vi_pattern.h):
# frame-counter bits + 8 colour bars. For every case: inject, capture, decode the PNG (pixel-exact bars, frame
# bits), check capture_path, then capture again with --after-frames 10 and require the decoded frame counter
# AND vinput's captured_present to advance by >= 10. Cases with Leak=$true also run 50 resize+capture cycles
# and compare the target's handle count / private bytes before and after. Every target is background,
# started and stopped by this script (by PID).
param([string]$Out = "$PSScriptRoot\..\build\test\nativecapture", [string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = "$D\vinput.exe"
. "$PSScriptRoot\vi_patterncheck.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$log = "$Out\nativecapture_log.txt"; Set-Content -LiteralPath $log -Value ''
function Note($m) { ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m) | Tee-Object -Append $log }
function Require($c, $d) { if (-not $c) { throw "FAIL $d" }; Note "PASS $d" }
if (-not ('ViNc.W' -as [type])) {
Add-Type -Namespace ViNc -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
'@
}
function FgPid { $p = [uint32]0; [void][ViNc.W]::GetWindowThreadProcessId([ViNc.W]::GetForegroundWindow(), [ref]$p); return $p }
function Cmd([int]$TargetPid, [string[]]$C) { $r = & $V --pid $TargetPid @C | ConvertFrom-Json; return $r }

# n, exe, args, api (expected capture_path), leak, pre (vinput commands before the first capture), inject ('early'
# = target waits for the vinput pipe before creating its device)
$cases = @(
  @{ n='d3d12_bgra_x64'; exe='vi_d3d12test.exe';   a='--fmt bgra'; api='d3d12'; leak=$true },
  @{ n='d3d12_rgba_x86'; exe='vi_d3d12test32.exe'; a='--fmt rgba'; api='d3d12'; leak=$false },
  @{ n='d3d12_r10_x64';  exe='vi_d3d12test.exe';   a='--fmt r10';  api='d3d12'; leak=$false },
  @{ n='d3d12_f16_x64';  exe='vi_d3d12test.exe';   a='--fmt f16';  api='d3d12'; leak=$false },
  @{ n='d3d12_appcontainer_x64'; exe='vi_d3d12test.exe'; a='--fmt bgra'; api='d3d12'; leak=$false; ac=$true },
  @{ n='gl_swapbuffers_x64'; exe='vi_gltest.exe';   a='';       api='gl'; leak=$true },
  @{ n='gl_wglswap_x64';     exe='vi_gltest.exe';   a='--wgl';  api='gl'; leak=$false },
  @{ n='gl_swapbuffers_x86'; exe='vi_gltest32.exe'; a='';       api='gl'; leak=$false },
  # Vulkan: the target waits for vinput's pipe before creating its device (vinput must see vkCreateDevice /
  # vkGetDeviceQueue / vkCreateSwapchainKHR); its swapchain asks for TRANSFER_DST only
  @{ n='vk_bgra_x64';   exe='vi_vktest.exe';   a='--fmt bgra';        api='vk'; leak=$true;  early=$true },
  @{ n='vk_gdpa_x64';   exe='vi_vktest.exe';   a='--fmt bgra --gdpa'; api='vk'; leak=$false; early=$true },
  @{ n='vk_rgba_x86';   exe='vi_vktest32.exe'; a='--fmt rgba';        api='vk'; leak=$false; early=$true; optional=$true },
  @{ n='vk_r10_x64';    exe='vi_vktest.exe';   a='--fmt r10';         api='vk'; leak=$false; early=$true; optional=$true },
  @{ n='vk_bgra_x86';   exe='vi_vktest32.exe'; a='--fmt bgra';        api='vk'; leak=$false; early=$true },
  # negative: injected after the device exists -> native capture refuses with a clear reason, auto falls back to
  # PrintWindow, and the driver's internal DXGI swapchain is not counted as a second (d3d12) frame source
  @{ n='vk_late_inject_x64'; exe='vi_vktest.exe'; a='--fmt bgra'; api='vk'; leak=$false; late=$true }
)
if ($Only.Count) { $cases = $cases | Where-Object { $Only -contains $_.n } }
$owned = @()
$fgBefore = FgPid
try {
  foreach ($c in $cases) {
    $tl = "$Out\$($c.n)_target.log"
    if ($c.ac) {
      # same sandbox as a UWP / Xbox app game: AppContainer with no capabilities (test\vi_aclaunch.cpp)
      icacls $Out /grant '*S-1-15-2-1:(OI)(CI)M' | Out-Null
      icacls "$D\$($c.exe)" /grant '*S-1-15-2-1:(RX)' | Out-Null
      $j = & "$D\vi_aclaunch.exe" "$D\$($c.exe)" $tl -28000 600 820 480 @($c.a -split ' ' | Where-Object { $_ }) | ConvertFrom-Json
      Require $j.ok "$($c.n) started inside AppContainer $($j.appcontainer_sid)"
      $p = Get-Process -Id $j.pid
    } else {
      $extra = if ($c.early) { ' --wait-vinput 20000' } else { '' }
      $p = Start-Process -FilePath "$D\$($c.exe)" -ArgumentList "`"$tl`" -28000 600 820 480 $($c.a)$extra" -PassThru
    }
    $owned += $p
    Start-Sleep -Milliseconds $(if ($c.early) { 300 } elseif ($c.late) { 2500 } else { 700 })
    $r = & $V inject --pid $p.Id | ConvertFrom-Json
    Require $r.ok "$($c.n) inject"
    if ($c.optional) {
      Start-Sleep -Milliseconds 2500
      $tlog0 = Get-Content -LiteralPath $tl -Raw
      if ($tlog0 -match 'FAIL surface does not offer format') { Note "$($c.n) SKIP (not a vinput result): $(($tlog0 -split "`n" | Select-String 'FAIL').Line)"; Stop-Process -Id $p.Id -Force -Confirm:$false; continue }
    }
    if ($c.late) {
      $s = $null
      for ($i = 0; $i -lt 60; $i++) { $s = Cmd $p.Id @('status'); if ($s.frames -gt 30) { break }; Start-Sleep -Milliseconds 100 }
      $f0 = $s.frames_by_api
      Require ($s.frame_api -eq 'vk' -and $s.vk.hooked -and $s.vk.swapchains -eq 0) "$($c.n) frames counted as vk; vinput saw no device/swapchain (created before injection)"
      Start-Sleep -Milliseconds 1000
      $s2 = Cmd $p.Id @('status')
      Require ($s2.frames_by_api.d3d12 -eq $f0.d3d12 -and $s2.frames_by_api.foreign_dxgi -gt $f0.foreign_dxgi -and $s2.frames_by_api.vk -gt $f0.vk) "$($c.n) the driver's DXGI presents are foreign, not d3d12 frames (d3d12 $($f0.d3d12)->$($s2.frames_by_api.d3d12), foreign $($f0.foreign_dxgi)->$($s2.frames_by_api.foreign_dxgi))"
      $n = Cmd $p.Id @('capture', "$Out\$($c.n)_native.png", '--method', 'native')
      Require (-not $n.ok -and $n.error -match 'before vinput was injected') "$($c.n) native capture refuses with a clear reason: $($n.error)"
      $a = Cmd $p.Id @('capture', "$Out\$($c.n)_auto.png")
      Require ($a.ok -and $a.capture_path -eq 'printwindow' -and $a.note -match 'before vinput was injected') "$($c.n) auto capture falls back to PrintWindow and says why"
      $pa = Test-VinputPattern "$Out\$($c.n)_auto.png"
      Note "$($c.n) INFO PrintWindow of a Vulkan window: pattern ok=$($pa.ok) $($pa.detail)"
      Require (-not $p.HasExited) "$($c.n) target alive"
      Stop-Process -Id $p.Id -Force -Confirm:$false
      continue
    }
    $s = $null
    for ($i = 0; $i -lt 60; $i++) { $s = Cmd $p.Id @('status'); if ($s.frames -gt 5 -and $s.frame_api -eq $c.api) { break }; Start-Sleep -Milliseconds 100 }
    Require ($s.frame_api -eq $c.api) "$($c.n) frames counted as $($c.api) (frame_api=$($s.frame_api), frames=$($s.frames))"
    if ($c.api -eq 'gl' -or $c.api -eq 'vk') {
      Require ($s.frames_by_api.d3d12 -eq 0 -and $s.frames_by_api.d3d11 -eq 0) "$($c.n) no double counting: the driver's DXGI presents are not d3d11/d3d12 frames ($($s.frames_by_api | ConvertTo-Json -Compress))"
    }
    if ($c.ac) {
      Require ($s.app_container -eq $true) "$($c.n) vinput runs inside the AppContainer, pipe reached through the container namespace"
      $l = & $V list | ConvertFrom-Json
      Require (@($l | Where-Object { $_.pid -eq $p.Id }).Count -eq 1) "$($c.n) vinput list shows the AppContainer pipe"
      # an output folder the container cannot write (inheritance cut, only the current user): the CLI must stage the
      # PNG in dist\run\logs and move it (a UWP game failed here with "WIC png encode failed hr=0x80070005")
      $noac = "$Out\$($c.n)_noac"; New-Item -ItemType Directory -Force $noac | Out-Null
      icacls $noac /inheritance:r /grant:r "$($env:USERNAME):(OI)(CI)F" '*S-1-5-32-544:(OI)(CI)F' | Out-Null
      $acl = (icacls $noac) -join ' '
      Require ($acl -notmatch 'APPLICATION PACKAGE|S-1-15-2') "$($c.n) control: $noac grants no AppContainer access"
      $n = Cmd $p.Id @('capture', "$noac\capture.png", '--method', 'native')
      Require ($n.ok -and $n.path -eq "$noac\capture.png" -and (Test-Path "$noac\capture.png") -and (Test-VinputPattern "$noac\capture.png").ok) "$($c.n) capture to a folder the container cannot write: staged + moved, pixels exact (err=$($n.error))"
    }
    $a = Cmd $p.Id @('capture', "$Out\$($c.n)_a.png", '--method', 'native')
    $a | ConvertTo-Json -Compress | Set-Content "$Out\$($c.n)_a.json"
    Require ($a.ok -and $a.capture_path -eq $c.api -and $a.method -eq 'swapchain') "$($c.n) native capture A path=$($a.capture_path) err=$($a.error)"
    $pa = Test-VinputPattern "$Out\$($c.n)_a.png"
    Require ($pa.ok -and $pa.w -eq $a.client_w -and $pa.h -eq $a.client_h) "$($c.n) capture A pixels: bars exact, frame bits decode (frame=$($pa.frame), $($pa.w)x$($pa.h)) $($pa.detail)"
    $pw = Cmd $p.Id @('capture', "$Out\$($c.n)_printwindow.png", '--method', 'printwindow')
    if ($pw.ok) { $pp = Test-VinputPattern "$Out\$($c.n)_printwindow.png"; Note "$($c.n) INFO PrintWindow comparison: pattern ok=$($pp.ok) frame=$($pp.frame) (native frame=$($pa.frame)) $($pp.detail)" } else { Note "$($c.n) INFO PrintWindow failed: $($pw.error)" }
    $b = Cmd $p.Id @('capture', "$Out\$($c.n)_b.png", '--after-frames', '10', '--method', 'native')
    $b | ConvertTo-Json -Compress | Set-Content "$Out\$($c.n)_b.json"
    $pb = Test-VinputPattern "$Out\$($c.n)_b.png"
    Require ($b.ok -and $pb.ok -and ($pb.frame - $pa.frame) -ge 10 -and ($b.captured_present - $a.captured_present) -ge 10 -and $b.frame -eq $b.captured_present) "$($c.n) --after-frames 10: target frame $($pa.frame)->$($pb.frame), vinput frame $($a.frame)->$($b.frame)"
    if ($c.api -eq 'vk' -and $c.n -eq 'vk_bgra_x86') {
      $off = Cmd $p.Id @('config', 'vk=off'); $k = Cmd $p.Id @('capture', "$Out\$($c.n)_vkoff.png", '--method', 'native')
      $on = Cmd $p.Id @('config', 'vk=on');  $k2 = Cmd $p.Id @('capture', "$Out\$($c.n)_vkon.png", '--method', 'native')
      Require ($off.ok -and -not $k.ok -and $k.error -match 'vk=off' -and $on.ok -and $k2.ok -and $k2.capture_path -eq 'vk') "$($c.n) config vk=off disables Vulkan capture ($($k.error)); vk=on restores it"
    }
    if ($c.leak) {
      $hwnd = [IntPtr][Convert]::ToInt64($s.hwnd, 16)
      for ($i = 0; $i -lt 5; $i++) { $w = Cmd $p.Id @('capture', "$Out\$($c.n)_warm.png", '--method', 'native'); if (-not $w.ok) { throw "warm capture failed: $($w.error)" } }
      $p.Refresh(); $h0 = $p.HandleCount; $m0 = $p.PrivateMemorySize64
      $bad = 0; $mismatch = 0
      for ($i = 0; $i -lt 50; $i++) {
        $cw = 700 + ($i % 5) * 60; $chh = 420 + ($i % 3) * 50
        [void][ViNc.W]::SetWindowPos($hwnd, [IntPtr]::Zero, 0, 0, $cw, $chh, 0x0002 -bor 0x0004 -bor 0x0010)   # NOMOVE|NOZORDER|NOACTIVATE
        Start-Sleep -Milliseconds 40
        $k = Cmd $p.Id @('capture', "$Out\$($c.n)_loop.png", '--after-frames', '2', '--method', 'native')
        if (-not $k.ok -or $k.capture_path -ne $c.api) { $bad++; Note "loop $i capture failed: $($k.error)"; continue }
        $pk = Test-VinputPattern "$Out\$($c.n)_loop.png"
        if (-not $pk.ok) { $bad++; Note "loop $i pattern: $($pk.detail)" }
        if ($k.w -ne $k.client_w -or $k.h -ne $k.client_h) { $mismatch++ }
      }
      Start-Sleep -Milliseconds 500
      $p.Refresh(); $h1 = $p.HandleCount; $m1 = $p.PrivateMemorySize64
      $st = Cmd $p.Id @('status')
      Note ("{0} 50 resize+capture cycles: handles {1}->{2}, private MB {3:N1}->{4:N1}, size-lag captures {5}, d3d12={6}" -f $c.n, $h0, $h1, ($m0/1MB), ($m1/1MB), $mismatch, ($st.d3d12 | ConvertTo-Json -Compress))
      Require ($bad -eq 0) "$($c.n) all 50 resize+capture cycles native and pattern-exact"
      Require (-not $p.HasExited) "$($c.n) target alive after 50 resizes"
      Require (($h1 - $h0) -le 20) "$($c.n) handle growth <= 20 over 50 cycles ($h0 -> $h1)"
      Require ((($m1 - $m0) / 1MB) -le 64) "$($c.n) private-bytes growth <= 64 MB over 50 cycles"
      $tlog = Get-Content -LiteralPath $tl -Raw
      $resizes = ([regex]::Matches($tlog, 'RESIZE \d+x\d+ hr=0x00000000')).Count
      if ($c.api -eq 'd3d12' -or $c.api -eq 'vk') { Require ($resizes -ge 10 -and $tlog -notmatch 'FAIL') "$($c.n) target resized/recreated its swapchain $resizes times with no failure" }
      else { Require ($tlog -notmatch 'FAIL') "$($c.n) target log has no failure after 50 resizes" }
      if ($c.api -eq 'd3d12') { Require ($st.d3d12.releases -ge 1 -and $st.d3d12.leaks -eq 0) "$($c.n) ResizeBuffers released our D3D12 objects ($($st.d3d12.releases)x), no leaks" }
      if ($c.api -eq 'vk') { Require ($st.vk.swapchains -eq 1 -and $st.vk.usage_added -ge 10) "$($c.n) old swapchains forgotten (tracked now: $($st.vk.swapchains)), TRANSFER_SRC added to every recreated one ($($st.vk.usage_added))" }
    }
    $tlog = Get-Content -LiteralPath $tl
    $worst = ($tlog | Select-String 'maxms=([\d.]+)' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value } | Measure-Object -Maximum).Maximum
    if ($null -ne $worst) { Note "$($c.n) worst per-second max frame time while injected/capturing: $worst ms"; Require ($worst -lt 250) "$($c.n) no capture stall >= 250 ms" }
    Require (-not (($tlog -join "`n") -match 'STATE_CHANGED')) "$($c.n) game-visible API state unchanged by captures (target self-check)"
    Require ((FgPid) -ne $p.Id) "$($c.n) target never became the real foreground"
    Stop-Process -Id $p.Id -Force -Confirm:$false
  }
  Note 'PASS native capture suite'
} finally {
  foreach ($p in $owned) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
  Note "owned targets stopped by PID; real foreground pid before=$fgBefore after=$(FgPid)"
}
