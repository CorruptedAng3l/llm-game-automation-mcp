# Native frame capture on synthetic targets that draw a known, changing pattern (test\vi_pattern.h):
# frame-counter bits + 8 colour bars. For every case: inject, capture, decode the PNG (pixel-exact bars, frame
# bits), check capture_path, then capture again with --after-frames 10 and require the decoded frame counter
# AND vinput's captured_present to advance by >= 10. Cases with Leak=$true also run 50 resize+capture cycles
# and compare the target's handle count / private bytes before and after. Every target is background,
# started and stopped by this script (by PID).
param([string]$Out = "$PSScriptRoot\..\build\test\nativecapture", [string[]]$Only = @(), [string]$Vinput = '')
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = if ($Vinput) { (Resolve-Path $Vinput).Path } else { "$D\vinput.exe" }
. "$PSScriptRoot\vi_patterncheck.ps1"
. "$PSScriptRoot\vi_capturetiming.ps1"
. "$PSScriptRoot\vi_resizecycle.ps1"
. "$PSScriptRoot\vi_logwindow.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$log = "$Out\nativecapture_log.txt"; Set-Content -LiteralPath $log -Value ''
function Note($m) { ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m) | Tee-Object -Append $log }
function Require($c, $d) { if (-not $c) { throw "FAIL $d" }; Note "PASS $d" }
if (-not ('ViNc.W' -as [type])) {
Add-Type -Namespace ViNc -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
'@
}
function FgPid { $p = [uint32]0; [void][ViNc.W]::GetWindowThreadProcessId([ViNc.W]::GetForegroundWindow(), [ref]$p); return $p }
function Cmd([int]$TargetPid, [string[]]$C) { $r = & $V --pid $TargetPid @C | ConvertFrom-Json; return $r }
function TimingControl($Process,$Status,$TargetLog,[long]$Token,[bool]$Start){
 $h=[IntPtr]::new([Convert]::ToInt64($Status.hwnd,16));[uint32]$owner=0;[ViNc.W]::GetWindowThreadProcessId($h,[ref]$owner)|Out-Null
 if($owner-ne$Process.Id){throw 'timing control HWND is not owned'}
 $message=if($Start){0x8000+117}else{0x8000+118};$label=if($Start){'START'}else{'END'}
 if(-not[ViNc.W]::PostMessage($h,$message,[IntPtr]::new($Token),[IntPtr]::Zero)){throw 'owned timing control post failed'}
 $until=[Environment]::TickCount64+3000
 do{if((Get-Content -LiteralPath $TargetLog -Raw)-match"CAPTURE_TIMING_$label token=$Token "){return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 throw "timing $label acknowledgement unavailable"
}

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
  @{ n='vk_bgra_x86';   exe='vi_vktest32.exe'; a='--fmt bgra';        api='vk'; leak=$true; early=$true },
  # negative: injected after the device exists -> native capture refuses with a clear reason, auto falls back to
  # PrintWindow, and the driver's internal DXGI swapchain is not counted as a second (d3d12) frame source
  @{ n='vk_late_inject_x64'; exe='vi_vktest.exe'; a='--fmt bgra'; api='vk'; leak=$false; late=$true }
)
if ($Only.Count) { foreach($name in $Only){if($name-notin$cases.n){throw "Unknown native capture case: $name"}};$cases = @($cases | Where-Object { $Only -contains $_.n }) }
$nativeResult=[ordered]@{schema=1;status='running';started_utc=[DateTime]::UtcNow.ToString('o');finished_utc=$null;expected_resize_cycles=50;cycle_indices_are_zero_based=$true;cases=[Collections.Generic.List[object]]::new()}
$resultPath=Join-Path $Out 'result.json';Write-ViNativeCaptureResult $resultPath $nativeResult
$caseRecord=$null
$owned = @()
$fgBefore = FgPid
try {
  foreach ($c in $cases) {
    $caseRecord=[ordered]@{name=$c.n;expected_api=$c.api;expected_resize_cycles=$(if($c.leak){50}else{0});status='running';pid=$null;resize_cycles=[Collections.Generic.List[object]]::new();failed_cycle_indices=[Collections.Generic.List[int]]::new()};$nativeResult.cases.Add($caseRecord);Write-ViNativeCaptureResult $resultPath $nativeResult
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
      $p = Start-Process -FilePath "$D\$($c.exe)" -ArgumentList "`"$tl`" -28000 600 820 480 $($c.a)$extra" -WindowStyle Hidden -PassThru
    }
    $owned += $p;$caseRecord.pid=$p.Id;$caseRecord.target_log=$tl
    Start-Sleep -Milliseconds $(if ($c.early) { 300 } elseif ($c.late) { 2500 } else { 700 })
    if ($p.HasExited) {
      $startup = Get-Content -LiteralPath $tl -Tail 12 -ErrorAction SilentlyContinue
      throw "FAIL $($c.n) fixture exited before injection (exit=$($p.ExitCode)): $($startup -join '; ')"
    }
    $r = & $V inject --pid $p.Id | ConvertFrom-Json
    $r | ConvertTo-Json -Depth 8 | Set-Content "$Out\$($c.n)_inject.json"
    Require $r.ok "$($c.n) inject"
    if ($c.optional) {
      Start-Sleep -Milliseconds 2500
      $tlog0 = Get-Content -LiteralPath $tl -Raw
      if ($tlog0 -match 'FAIL surface does not offer format') { $caseRecord.status='skipped'; Note "$($c.n) SKIP (not a vinput result): $(($tlog0 -split "`n" | Select-String 'FAIL').Line)"; Stop-Process -Id $p.Id -Force -Confirm:$false; continue }
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
      Require (-not $p.HasExited) "$($c.n) target alive";$caseRecord.status='passed'
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
      $nested = "$noac\missing_$([guid]::NewGuid().ToString('N'))\child\capture.png"
      $n2 = Cmd $p.Id @('capture', $nested, '--method', 'native')
      Require ($n2.ok -and $n2.path -eq $nested -and (Test-Path -LiteralPath $nested) -and (Test-VinputPattern $nested).ok) "$($c.n) CLI creates missing destination parents outside AppContainer and moves staged pixels"
    }
    $timingToken=Get-Random -Minimum 1 -Maximum 1000000000
    TimingControl $p $s $tl $timingToken $true
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
        $cw=700+($i%5)*60;$chh=420+($i%3)*50;$raced=$c.api-eq'vk'-and$i%10-eq0
        $cycle=[ordered]@{index=$i;number=$i+1;status='running';classification='in_progress';requested_outer_w=$cw;requested_outer_h=$chh;race_attempted=$raced;race_observed=$false;resize_ok=$false;capture=$null;pattern=$null;elapsed_ms=0;target_alive=$true}
        $caseRecord.resize_cycles.Add($cycle);Write-ViNativeCaptureResult $resultPath $nativeResult;$cycleWatch=[Diagnostics.Stopwatch]::StartNew()
        try{
          [uint32]$windowOwner=0;[ViNc.W]::GetWindowThreadProcessId($hwnd,[ref]$windowOwner)|Out-Null
          if($p.HasExited-or$windowOwner-ne$p.Id){$cycle.classification='target_window_not_owned';$cycle.status='failed';$bad++;break}
          if($raced){
            $token=$i+1;$packed=$cw-bor($chh-shl16);$cycle.resize_ok=[ViNc.W]::PostMessage($hwnd,0x8000+120,[IntPtr]$token,[IntPtr]$packed)
            $until=[Environment]::TickCount64+3000
            do{$snapshot=[ViLogWindow]::Read($tl);$text=if($snapshot.valid){[Text.Encoding]::UTF8.GetString($snapshot.bytes)}else{''};if($text-match"RACE_AFTER_ACQUIRE token=$token .*resized=1 changed=1"){$cycle.race_observed=$true;break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
          }else{$cycle.resize_ok=[ViNc.W]::SetWindowPos($hwnd,[IntPtr]::Zero,0,0,$cw,$chh,0x0002-bor0x0004-bor0x0010)}
          Start-Sleep -Milliseconds 40
          if($p.HasExited){$cycle.status='failed';$cycle.classification='target_exited_before_capture';$bad++;break}
          $k=Cmd $p.Id @('capture',"$Out\$($c.n)_loop.png",'--after-frames','2','--method','native');$cycle.capture=$k
          $pk=$null;if($k.ok){$pk=Test-VinputPattern "$Out\$($c.n)_loop.png"};$cycle.pattern=$pk
          $cycle.classification=Get-ViResizeCycleClass $cycle.resize_ok $raced $cycle.race_observed $k $c.api $pk
          $cycle.status=if($cycle.classification-eq'passed'){'passed'}else{'failed'}
          if($cycle.status-eq'failed'){
            $bad++;Note "loop $i failed: $($cycle.classification) error=$($k.error) pattern=$($pk.detail)"
            if($k.ok-and(Test-Path "$Out\$($c.n)_loop.png")){$failureImage="$Out\$($c.n)_bad_cycle_$('{0:D2}'-f$i).png";try{Copy-Item -LiteralPath "$Out\$($c.n)_loop.png" -Destination $failureImage;$cycle.failure_image=$failureImage}catch{$cycle.image_copy_error=$_.Exception.Message}}
            if(!$p.HasExited){try{$cycle.diagnostic_status=Cmd $p.Id @('status')}catch{$cycle.diagnostic_error=$_.Exception.Message}}
          }
          $cycle.size_lag=$k.ok-and($k.w-ne$k.client_w-or$k.h-ne$k.client_h);if($cycle.size_lag){$mismatch++}
        }catch{$cycle.status='failed';$cycle.classification='exception';$cycle.error=$_.Exception.Message;$bad++;throw}
        finally{$cycleWatch.Stop();$cycle.elapsed_ms=$cycleWatch.ElapsedMilliseconds;$cycle.target_alive=!$p.HasExited;if(!$cycle.target_alive){$cycle.exit_code=$p.ExitCode};if($cycle.status-eq'failed'){$caseRecord.failed_cycle_indices.Add($i)};Write-ViNativeCaptureResult $resultPath $nativeResult}
        if($p.HasExited){break}
      }
      Start-Sleep -Milliseconds 500
      $p.Refresh(); $h1 = $p.HandleCount; $m1 = $p.PrivateMemorySize64
      $st = Cmd $p.Id @('status');$caseRecord.after_resize_status=$st
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
    TimingControl $p $s $tl $timingToken $false
    $tlog = Get-Content -LiteralPath $tl
    $timing=Get-ViCaptureTiming ($tlog-join"`n") $timingToken;$timing|ConvertTo-Json|Set-Content "$Out\$($c.n)_timing.json"
    $worst=$timing.worst_ms
    Note "$($c.n) worst frame interval in acknowledged capture phase: $worst ms ($($timing.samples) samples)"
    Require ($timing.samples-ge1-and$worst-lt250) "$($c.n) no capture-phase stall >= 250 ms (startup/injection measured separately)"
    Require (-not (($tlog -join "`n") -match 'STATE_CHANGED')) "$($c.n) game-visible API state unchanged by captures (target self-check)"
    Require ((FgPid) -ne $p.Id) "$($c.n) target never became the real foreground"
    $caseRecord.status='passed';Write-ViNativeCaptureResult $resultPath $nativeResult
    Stop-Process -Id $p.Id -Force -Confirm:$false
  }
  $nativeResult.status='passed';Note 'PASS native capture suite'
} catch {$nativeResult.status='failed';$nativeResult.error=$_.Exception.Message;if($caseRecord-and$caseRecord.status-eq'running'){$caseRecord.status='failed';$caseRecord.error=$_.Exception.Message};throw} finally {
  $nativeResult.finished_utc=[DateTime]::UtcNow.ToString('o');Write-ViNativeCaptureResult $resultPath $nativeResult
  foreach ($p in $owned) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
  Note "owned targets stopped by PID; real foreground pid before=$fgBefore after=$(FgPid)"
}
