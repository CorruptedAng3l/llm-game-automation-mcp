# An outer hook (overlay or mod) WndProc consumes mouse/key events before vinput's subclass.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\overlay",[switch]$KeepGoing)
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null;$Out=(Resolve-Path $Out).Path
if(-not ('ViOverlayNative' -as [type])) {Add-Type 'using System;using System.Runtime.InteropServices;public static class ViOverlayNative{[DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);public static int[] Fill(IntPtr h){int n=0;for(;n<20000;n++){if(!PostMessage(h,0x8000+200,IntPtr.Zero,IntPtr.Zero))return new int[]{n,Marshal.GetLastWin32Error()};}return new int[]{n,0};}}'}
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=@()
function Check($ok,$name){$checks.Add(@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not $ok -and -not $KeepGoing){throw $name}}
function Vi($targetPid,[string[]]$a){& $V --json --pid $targetPid @a | ConvertFrom-Json}
function Text($t){Get-Content -LiteralPath $t.log -Raw}
function Count($t,$pattern){[regex]::Matches((Text $t),$pattern).Count}
function Control($t,$n,$w,$pattern){
  $before=Count $t $pattern
  if(-not [ViOverlayNative]::PostMessage($t.h,0x8000+$n,[IntPtr]$w,[IntPtr]::Zero)){throw 'control post failed'}
  for($i=0;$i -lt 60;$i++){if((Count $t $pattern) -gt $before){return};Start-Sleep -Milliseconds 25}
  throw "control not received: $pattern"
}
function StartTarget($exe,$name,$render=$false,$ansi=$false){
  $log="$Out\$name.log";$extra=if($render){' --render'}else{''};if($ansi){$extra+=' --ansi'}
  $p=Start-Process -FilePath "$D\$exe" -ArgumentList "`"$log`"$extra" -WindowStyle Hidden -PassThru
  $script:owned+=$p
  $inj=& $V --json inject --pid $p.Id | ConvertFrom-Json;if(-not $inj.ok){throw 'inject failed'}
  for($i=0;$i -lt 50;$i++){$s=Vi $p.Id @('status');if($s.hwnd -ne '0x0' -and (-not $render -or $s.counters.present_ours -gt 5)){break};Start-Sleep -Milliseconds 100}
  if($s.really_foreground){throw 'target gained real foreground'}
  return @{p=$p;pid=$p.Id;h=[IntPtr][long]::Parse($s.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber);log=$log}
}
try {
foreach($arch in @('','32')){
  $t=StartTarget "vi_channelprobe$arch.exe" "gdi$arch"
  $normal=Vi $t.pid @('click','left','50','60','--hover-timeout-ms','250')
  Check ($normal.ok -and -not $normal.hover_unconfirmed) "GDI$arch own subclass click stays confirmed"
  Control $t 5 1 'OVERLAY_MODE 1'
  $clicks=Count $t 'OVERLAY_CLICK_ACCEPT'
  $click=Vi $t.pid @('click','left','75','65','--hover-timeout-ms','250')
  Start-Sleep -Milliseconds 100
  Check ($click.ok -and $click.hover_unconfirmed -and $click.queue_progress_confirmed -and $click.delivery_warning) "GDI$arch swallowed hover gets honest responsive-queue fallback"
  Check ((Count $t 'OVERLAY_CLICK_ACCEPT') -eq $clicks+1 -and (Text $t) -match 'OVERLAY_CLICK_ACCEPT lp=75,65 cursor=75,65') "GDI$arch exactly one overlay click with virtual cursor"
  $down=Vi $t.pid @('mousedown','left','90','80','--hover-timeout-ms','250')
  $held=Vi $t.pid @('status');$up=Vi $t.pid @('mouseup','left')
  Check ($down.ok -and $down.hover_unconfirmed -and 1 -in $held.virtual_down -and $up.ok) "GDI$arch swallowed hover mousedown holds until explicit up"
  $before=Vi $t.pid @('trace','key','insert')
  Vi $t.pid @('key','insert','160') | Out-Null;Start-Sleep -Milliseconds 100
  $after=Vi $t.pid @('trace','key','insert')
  Check (($after.post_attempt_down-$before.post_attempt_down) -eq 1 -and ($after.post_accepted_down-$before.post_accepted_down) -eq 1 -and ($after.post_accepted_up-$before.post_accepted_up) -eq 1) "GDI$arch trace reports actual PostMessage attempts and acceptance"
  Check ($after.message_down -eq $before.message_down -and (Text $t) -match 'OVERLAY_KEY_CONSUMED msg=0x100 vk=0x2D') "GDI$arch accepted post remains distinct from missing subclass dispatch"
  $evidence.Add(@{target=$arch;click=$click;down=$down;trace_before=$before;trace_after=$after})
  Control $t 5 2 'OVERLAY_MODE 2'
  $prior=Count $t 'OVERLAY_CLICK_ACCEPT'
  $blockedBarrier=Vi $t.pid @('click','left','110','100','--hover-timeout-ms','100')
  Check (-not $blockedBarrier.ok -and (Count $t 'OVERLAY_CLICK_ACCEPT') -eq $prior) "GDI$arch swallowed queue barrier never triggers blind click"
  Control $t 5 1 'OVERLAY_MODE 1'
  Control $t 9 900 'UI_BLOCK_BEGIN'
  $hung=Vi $t.pid @('click','left','130','110','--hover-timeout-ms','100')
  $hungExit=$LASTEXITCODE
  Check (-not $hung.ok -and $hungExit -ne 0) "GDI$arch hung UI is still a nonzero failure"
  Start-Sleep -Milliseconds 950
  Check ((Count $t 'OVERLAY_CLICK_ACCEPT') -eq $prior) "GDI$arch no delayed button queued during hung UI"
  # Out-Null does not turn a failure into exit code zero.
  Control $t 9 700 'UI_BLOCK_BEGIN'
  & $V --json --pid $t.pid click left 140 120 --hover-timeout-ms 100 | Out-Null
  Check ($LASTEXITCODE -ne 0) "GDI$arch piped Out-Null preserves failing native exit code"
  Start-Sleep -Milliseconds 750
  Control $t 7 0 'OVERLAY_REMOVED'
  $own=Vi $t.pid @('click','left','150','130','--hover-timeout-ms','250')
  Check ($own.ok -and -not $own.hover_unconfirmed) "GDI$arch removing overlay restores confirmed normal route"
  Control $t 5 1 'OVERLAY_MODE 1'
  $barrierBefore=Count $t 'OVERLAY_REGISTERED_FORWARDED'
  $oldBaseClicks=Count $t 'WM_LBUTTONDOWN lp='
  $raceOutput="$Out\outer_changed$arch.json";$raceError="$Out\outer_changed$arch.stderr"
  $race=Start-Process -FilePath $V -ArgumentList "--json --pid $($t.pid) click left 150 130 --settle-ms 400 --hover-timeout-ms 250" -WindowStyle Hidden -RedirectStandardOutput $raceOutput -RedirectStandardError $raceError -PassThru
  $script:owned+=$race
  for($i=0;$i -lt 100;$i++){if((Count $t 'OVERLAY_REGISTERED_FORWARDED') -gt $barrierBefore){break};Start-Sleep -Milliseconds 5}
  Check ((Count $t 'OVERLAY_REGISTERED_FORWARDED') -gt $barrierBefore -and -not $race.HasExited) "GDI$arch outer-change control reaches a queue barrier while click is still preparing"
  Control $t 7 0 'OVERLAY_REMOVED'
  if(-not $race.WaitForExit(4000)){throw 'outer-change CLI did not finish'}
  $changed=Get-Content -LiteralPath $raceOutput -Raw | ConvertFrom-Json
  Check (-not $changed.ok -and $race.ExitCode -ne 0 -and (Count $t 'WM_LBUTTONDOWN lp=') -eq $oldBaseClicks) "GDI$arch outer chain removed after barrier is rejected before pressing"
  $evidence.Add(@{target="outer_changed$arch";reply=$changed})
  $ownClicks=Count $t 'WM_LBUTTONDOWN lp='
  Control $t 9 700 'UI_BLOCK_BEGIN'
  $ownHung=Vi $t.pid @('click','left','160','140','--hover-timeout-ms','100')
  $ownHungExit=$LASTEXITCODE;Start-Sleep -Milliseconds 750
  Check (-not $ownHung.ok -and $ownHungExit -ne 0 -and (Count $t 'WM_LBUTTONDOWN lp=') -eq $ownClicks) "GDI$arch own hung subclass cannot use overlay fallback"
  # Fill only our own sleeping UI queue to get a real safe PostMessage failure, not a mock result.
  Control $t 9 4500 'UI_BLOCK_BEGIN'
  $full=[ViOverlayNative]::Fill($t.h)
  Check ($full[1] -ne 0) "GDI$arch owned queue quota is a real failed-post positive control"
  $pfBefore=Vi $t.pid @('trace','key','f7')
  Vi $t.pid @('down','f7') | Out-Null;Vi $t.pid @('up','f7') | Out-Null
  $pf=Vi $t.pid @('trace','key','f7')
  Check (($pf.post_attempt_down-$pfBefore.post_attempt_down) -eq 1 -and ($pf.post_failed_down-$pfBefore.post_failed_down) -eq 1 -and ($pf.post_failed_up-$pfBefore.post_failed_up) -eq 1 -and ($pf.post_accepted_down-$pfBefore.post_accepted_down) -eq 0 -and $pf.last_post_error -eq $full[1]) "GDI$arch failed PostMessage is not counted as accepted"
  Check (-not $pf.virtual_down -and $pf.message_down -eq $pfBefore.message_down) "GDI$arch failed-post key is released and never claimed dispatched"
  $evidence.Add(@{target=$arch;barrier_blocked=$blockedBarrier;hung=$hung;queue_fill=$full;failed_post_before=$pfBefore;failed_post_after=$pf})
  Stop-Process -Id $t.pid -Force -Confirm:$false
  $r=StartTarget "vi_channelprobe$arch.exe" "render$arch" $true
  Control $r 5 1 'OVERLAY_MODE 1'
  $renderClick=Vi $r.pid @('click','left','75','65','--hover-timeout-ms','500')
  Check ($renderClick.ok -and $renderClick.hover_unconfirmed -and $renderClick.hover_frames -ge 3) "DXGI$arch overlay fallback still waits rendered hover frames"
  Control $r 10 1 'RENDER_PAUSED 1'
  Start-Sleep -Milliseconds 100
  $renderPrior=Count $r 'OVERLAY_CLICK_ACCEPT'
  $stopped=Vi $r.pid @('click','left','90','80','--hover-timeout-ms','100')
  Check (-not $stopped.ok -and $stopped.error -match 'rendered' -and $stopped.error.Contains('--hover-frames 0') -and (Count $r 'OVERLAY_CLICK_ACCEPT') -eq $renderPrior) "DXGI$arch stalled render cannot bypass the frame gate and gives render-only opt-out hint"
  $evidence.Add(@{target="render$arch";render_click=$renderClick;stopped_render=$stopped})
  Stop-Process -Id $r.pid -Force -Confirm:$false
  $deadWindow=StartTarget "vi_channelprobe$arch.exe" "destroyed$arch"
  $pressed=Vi $deadWindow.pid @('mousedown','left')
  Control $deadWindow 12 0 'WINDOW_DESTROYED_ALIVE'
  $released=Vi $deadWindow.pid @('mouseup','left')
  $destroyedTrace=Vi $deadWindow.pid @('trace','key','lbutton')
  Check ($pressed.ok -and $released.ok -and -not $destroyedTrace.virtual_down) "GDI$arch destroyed window does not prevent virtual mouse-up cleanup"
  $destroyedStatus=Vi $deadWindow.pid @('status')
  Check ($destroyedTrace.post_attempt_up -eq 0 -and $destroyedTrace.post_accepted_up -eq 0 -and $destroyedTrace.post_failed_up -eq 0 -and $destroyedStatus.counters.post_target_rejected -ge 1 -and $destroyedStatus.counters.last_post_target_error -eq 1400) "GDI$arch destroyed HWND is explicitly rejected before a native post attempt"
  $evidence.Add(@{target="destroyed$arch";released=$released;trace=$destroyedTrace;status=$destroyedStatus})
  Stop-Process -Id $deadWindow.pid -Force -Confirm:$false
  $a=StartTarget "vi_channelprobe$arch.exe" "ansi$arch" $false $true
  $ansiStatus=Vi $a.pid @('status')
  Check $ansiStatus.subclassed "ANSI$arch own subclass is identified without an A/W thunk false positive"
  Control $a 13 1 'FILTER_MOVES 1'
  $ansiCount=Count $a 'WM_LBUTTONDOWN lp='
  $ansiFiltered=Vi $a.pid @('click','left','50','60','--hover-timeout-ms','100')
  Check (-not $ansiFiltered.ok -and (Count $a 'WM_LBUTTONDOWN lp=') -eq $ansiCount) "ANSI$arch own loop-consumed hover cannot qualify as an external overlay"
  Control $a 13 0 'FILTER_MOVES 0'
  $ansiNormal=Vi $a.pid @('click','left','50','60','--hover-timeout-ms','250')
  Check ($ansiNormal.ok -and -not $ansiNormal.hover_unconfirmed) "ANSI$arch own hover confirmation works"
  Control $a 5 1 'OVERLAY_MODE 1'
  $ansiOuter=Vi $a.pid @('click','left','75','65','--hover-timeout-ms','250')
  Check ($ansiOuter.ok -and $ansiOuter.hover_unconfirmed -and $ansiOuter.queue_progress_confirmed) "ANSI$arch real outer overlay still qualifies via its queued barrier"
  $evidence.Add(@{target="ansi$arch";status=$ansiStatus;filtered=$ansiFiltered;normal=$ansiNormal;outer=$ansiOuter})
  Stop-Process -Id $a.pid -Force -Confirm:$false
}
$fail=@($checks | Where-Object {-not $_.pass}).Count
"SUMMARY $($checks.Count-$fail) PASS / $fail FAIL"
if($fail){throw 'overlay controls failed'}
$global:LASTEXITCODE=0
} finally {
foreach($p in $owned){if(-not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false}}
@{checks=$checks;evidence=$evidence} | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath "$Out\result.json"
}
