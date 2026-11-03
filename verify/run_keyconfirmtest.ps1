# Independent readiness control: delivery/read counts stay positive even when the consumer ignores INSERT.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\keyconfirm",[switch]$KeepGoing)
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null;$Out=(Resolve-Path $Out).Path
if(-not ('ViKeyConfirmNative' -as [type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViKeyConfirmNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}' }
$checks=[Collections.Generic.List[object]]::new();$evidence=@();$owned=@()
function Check($ok,$name){$checks.Add([pscustomobject]@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not $ok -and -not $KeepGoing){throw $name}}
function Vi($id,[string[]]$a){$raw=& $V --json --pid $id @a;$code=$LASTEXITCODE;[pscustomobject]@{reply=($raw|ConvertFrom-Json);exit=$code}}
function DownCount($log){@((Get-Content -LiteralPath $log)|Where-Object{$_ -match ' INSERT_DOWN '}).Count}
try{
 foreach($suffix in @('','32')){
  $log="$Out\target$suffix.log";$p=Start-Process -FilePath "$D\vi_keyready$suffix.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned+=$p
  $inject=& $V --json inject --pid $p.Id|ConvertFrom-Json;$inject|ConvertTo-Json -Depth 8|Set-Content "$Out\inject$suffix.json";if(-not $inject.ok){throw 'inject failed'}
  for($i=0;$i -lt 40;$i++){$s=(Vi $p.Id @('status')).reply;if($s.hwnd -ne '0x0'){break};Start-Sleep -Milliseconds 50}
  Check (-not $s.really_foreground) "$suffix target starts in background"
  $h=[IntPtr][long]::Parse($s.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
  $early=Vi $p.Id @('key','insert','100','--expect-text','OVERLAY PANEL','--expect-timeout-ms','350','--expect-region','0,0,620,240')
  $early|ConvertTo-Json -Depth 12|Set-Content "$Out\early$suffix.json"
  $trace=(Vi $p.Id @('trace','key','insert')).reply;Start-Sleep -Milliseconds 50;$t=Get-Content -LiteralPath $log -Raw
  Check (-not $early.reply.ok -and $early.exit -ne 0 -and $early.reply.input_injected -and -not $early.reply.expectation_met) "$suffix early key fails observable postcondition despite accepted input"
  Check ($trace.message_down -gt 0 -and $trace.channels.GetAsyncKeyState.down_reads -gt 0 -and $trace.channels.GetKeyboardState.down_reads -gt 0 -and $t -match 'CONSUMER_NOT_READY') "$suffix delivered messages and API reads do not imply consumption"
  Check ((DownCount $log) -eq 1) "$suffix failed postcondition never retries the key"
  [ViKeyConfirmNative]::PostMessage($h,0x8001,[IntPtr]1,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 60
  $opened=Vi $p.Id @('key','insert','100','--expect-text','OVERLAY PANEL','--expect-timeout-ms','1500','--expect-region','0,0,620,240')
  $opened|ConvertTo-Json -Depth 12|Set-Content "$Out\opened$suffix.json"
  Check ($opened.exit -eq 0 -and $opened.reply.ok -and $opened.reply.input_injected -and $opened.reply.expectation_met -and $opened.reply.expectation.match.line -match 'OVERLAY PANEL') "$suffix exactly one ready press opens a panel proven by OCR"
  Check ((DownCount $log) -eq 2) "$suffix success uses exactly one additional key"
  $closed=Vi $p.Id @('hold','insert','100','--expect-text','OVERLAY PANEL','--expect-timeout-ms','350')
  Check (-not $closed.reply.ok -and $closed.exit -ne 0 -and $closed.reply.input_injected -and -not $closed.reply.expectation_met -and $closed.reply.ui_settle_ms -eq 100) "$suffix next press closes panel and cannot confirm the old GDI frame"
  Check ((DownCount $log) -eq 3) "$suffix hold expectation also never repeats input"
  [ViKeyConfirmNative]::PostMessage($h,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 30
  $beforeBad=DownCount $log
  foreach($bad in @(
    @('key','insert','--expect-text'),@('key','insert','--expect-timeout-ms','10'),
    @('key','insert','--expect-text','PANEL','--expect-timeout-ms','0'),@('key','insert','--expect-text','PANEL','--expect-timeout-ms','30001'),
    @('key','insert','--expect-text','PANEL','--expect-timeout-ms','1junk'),@('key','insert','--expect-text','PANEL','--expect-timeout-ms','-1'),
    @('key','insert','--expect-text','PANEL','--expect-region','0,0,0,20'),@('key','insert','--expect-text','PANEL','--expect-region','0,0,9999,20'),
    @('key','insert','--expect-text','PANEL','--expect-region','1,2,3'),@('key','insert','--expect-text','PANEL','--expect-text','OTHER'),
    @('key','insert','--expect-typo','PANEL'),@('key','insert','--expect-text','PANEL','--count','2'),@('down','insert','--expect-text','PANEL'),@('keys','--expect-text','PANEL')
  )){
   $badResult=Vi $p.Id $bad;Check (-not $badResult.reply.ok -and $badResult.exit -ne 0 -and $null -ne $badResult.reply.input_injected -and -not $badResult.reply.input_injected) "$suffix malformed/unsupported expectation rejected: $($bad -join ' ')"
  }
  Start-Sleep -Milliseconds 50;Check ((DownCount $log) -eq $beforeBad) "$suffix validation happens before injecting any key"
  $watch=[Diagnostics.Stopwatch]::StartNew();& $V --json --pid $p.Id key insert 80 --expect-text 'OVERLAY PANEL' --expect-timeout-ms 20|Out-Null;$pipeCode=$LASTEXITCODE;$watch.Stop()
  Check ($pipeCode -ne 0 -and $watch.ElapsedMilliseconds -lt 800) "$suffix tiny postcondition deadline stays bounded and failure survives Out-Null"
  $watch.Restart();$plain=Vi $p.Id @('key','q','1');$watch.Stop()
  Check ($plain.reply.ok -and $plain.exit -eq 0 -and $null -eq $plain.reply.expectation_met -and $watch.ElapsedMilliseconds -lt 400) "$suffix unconfirmed key retains original response and latency"
  $final=(Vi $p.Id @('status')).reply;Check (-not $final.really_foreground) "$suffix all key checks stay background"
  $evidence+=[pscustomobject]@{arch=$suffix;early=$early;trace=$trace;opened=$opened;closed=$closed;pipe_exit=$pipeCode;final=$final}
  $nativeLog="$Out\native$suffix.log";$native=Start-Process -FilePath "$D\vi_testinput$suffix.exe" -ArgumentList "`"$nativeLog`" -28000 600 640 360" -WindowStyle Hidden -PassThru;$owned+=$native
  $in=& $V --json inject --pid $native.Id|ConvertFrom-Json;if(-not $in.ok){throw 'native inject failed'}
  for($n=0;$n -lt 40;$n++){$ns=(Vi $native.Id @('status')).reply;if($ns.frames -gt 3){break};Start-Sleep -Milliseconds 50}
  $fenced=Vi $native.Id @('key','q','1','--expect-text','UNMATCHED LABEL','--expect-timeout-ms','200','--expect-region','0,0,100,100')
  Check ($fenced.reply.input_injected -and -not $fenced.reply.ok -and $fenced.reply.native_present_after-$fenced.reply.native_present_before -ge 2 -and $fenced.reply.ui_settle_ms -eq 0) "$suffix native expectation waits two real target Presents after input"
  Start-Sleep -Milliseconds 40;$nativeDowns=@(Get-Content -LiteralPath $nativeLog|Where-Object{$_ -match ' WM_KEYDOWN vk=0x51 '}).Count
  Check ($nativeDowns -eq 1) "$suffix native expectation still injects exactly one press"
  $stallLog="$Out\stalled$suffix.log";$stall=Start-Process -FilePath "$D\vi_dragtarget$suffix.exe" -ArgumentList "`"$stallLog`"" -WindowStyle Hidden -PassThru;$owned+=$stall
  $in=& $V --json inject --pid $stall.Id|ConvertFrom-Json;if(-not $in.ok){throw 'stall inject failed'}
  for($n=0;$n -lt 40;$n++){$ss=(Vi $stall.Id @('status')).reply;if($ss.frames -gt 3){break};Start-Sleep -Milliseconds 50}
  $sh=[IntPtr][long]::Parse($ss.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
  [ViKeyConfirmNative]::PostMessage($sh,0x8001,[IntPtr]1,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 30
  $watch.Restart();$stalled=Vi $stall.Id @('key','q','1','--expect-text','UNMATCHED LABEL','--expect-timeout-ms','40');$watch.Stop()
  Check ($stalled.reply.input_injected -and -not $stalled.reply.ok -and $stalled.exit -ne 0 -and $stalled.reply.expectation.polls -eq 0 -and $watch.ElapsedMilliseconds -lt 800) "$suffix stalled native renderer fails bounded freshness check before OCR"
  $evidence+=[pscustomobject]@{arch=$suffix;native_fence=$fenced;native_key_downs=$nativeDowns;stalled=$stalled}
 }
}finally{
 foreach($p in $owned){if(-not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false}}
 $checks|ConvertTo-Json -Depth 6|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 14|Set-Content "$Out\evidence.json"
}
$failed=@($checks|Where-Object{-not $_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1}
$global:LASTEXITCODE=0 # the final native command is deliberately a failing postcondition
