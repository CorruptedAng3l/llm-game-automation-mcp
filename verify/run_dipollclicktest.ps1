param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\di_pollclick\after",[switch]$KeepGoing,[switch]$CleanupOnly)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools=Join-Path $Out 'tools';New-Item -ItemType Directory -Force $tools,"$tools\temp"|Out-Null
if(-not('ViDiPollNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViDiPollNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'}
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=@();$current=$null
function Check($Ok,$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Call($Target,[string[]]$Arguments){$watch=[Diagnostics.Stopwatch]::StartNew();$raw=@(& $V --json --pid $Target.pid @Arguments);$code=$LASTEXITCODE;$watch.Stop();$r=@{args=$Arguments;raw=$raw;exit=$code;elapsed_ms=$watch.ElapsedMilliseconds;reply=($raw|ConvertFrom-Json)};$script:current.calls.Add($r);return $r}
function Text($Target){Get-Content -LiteralPath $Target.log -Raw -ErrorAction SilentlyContinue}
function Count($Target,$Pattern){[regex]::Matches((Text $Target),$Pattern).Count}
function Control($Target,$Message,$Value,$Pattern){$old=Count $Target $Pattern;[ViDiPollNative]::PostMessage($Target.h,0x8000+$Message,[IntPtr]$Value,[IntPtr]::Zero)|Out-Null;$until=[Environment]::TickCount64+2000;do{if((Count $Target $Pattern)-gt$old){return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'owned control not dispatched'}
function StartTarget($Suffix,$Name,$Extra='',$Consumer=''){
 $script:current=@{arch=$Suffix;name=$Name;calls=[Collections.Generic.List[object]]::new()};$records.Add($script:current)
 $log="$Out\$Name$Suffix.log";$p=Start-Process "$tools\vi_dipollclick$Suffix.exe" -ArgumentList "`"$log`" $Extra" -WindowStyle Hidden -PassThru;$script:owned+=$p
 $inject=& $V --json inject --pid $p.Id|ConvertFrom-Json;if(-not$inject.ok){throw 'injection failed'}
 $target=@{pid=$p.Id;p=$p;h=[IntPtr]::Zero;log=$log};$until=[Environment]::TickCount64+3000
 do{$s=(Call $target @('status')).reply;$ready=$s.hwnd-ne'0x0';if($Consumer){$ready=$ready-and$s.counters.$Consumer-gt0};if($ready){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
 if(-not$ready-or$s.really_foreground){throw 'background target/consumer unavailable'};$target.h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16));$target.w=$s.client_w;$target.hgt=$s.client_h;return $target
}
function OneBufferPoll($Target,$Capacity){$old=Count $Target 'BUFFER_POLL';Control $Target 2 $Capacity 'ONE_POLL_ARMED';$until=[Environment]::TickCount64+2000;do{if((Count $Target 'BUFFER_POLL')-gt$old){return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'one buffer poll not observed'}
function WaitPoll($Target){$before=Count $Target 'STATE_POLL.*left=0';$until=[Environment]::TickCount64+1000;do{if((Count $Target 'STATE_POLL.*left=0')-gt$before){return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'up polling did not resume'}
try{
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_dipollclick.cpp" /Fo:"$tools\poll$suffix.obj" /Fd:"$tools\poll$suffix.pdb" /Fe:"$tools\vi_dipollclick$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
exit /b 0
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture /W4 /WX"}
 foreach($suffix in @('','32')){
  if($CleanupOnly){
   $t=StartTarget $suffix 'cleanup' '--stopped';$before=(Call $t @('trace','key','lbutton')).reply;$badDown=Call $t @('mousedown','left','--di-poll-timeout-ms','200');$after=(Call $t @('trace','key','lbutton')).reply
   Check (-not$badDown.reply.ok-and$after.injected_down-eq$before.injected_down-and-not$after.virtual_down) "$suffix mousedown rejects irrelevant poll timing before press"
   $down=Call $t @('mousedown','left');$held=(Call $t @('trace','key','lbutton')).reply;$badUp=Call $t @('mouseup','left','--di-poll-timeout-ms','true');$released=(Call $t @('trace','key','lbutton')).reply
   Check ($down.reply.ok-and$held.virtual_down-and-not$badUp.reply.ok-and$badUp.reply.button_released-and-not$released.virtual_down-and$released.injected_down-eq$held.injected_down) "$suffix malformed irrelevant mouseup timing still releases without another down"
   Stop-Process -Id $t.pid -Force;continue
  }
  $t=StartTarget $suffix 'state' '' 'di_mouse';$prior=Count $t 'CONSUMED_DOWN'
  $auto=Call $t @('click','left','80','70');Start-Sleep -Milliseconds 30
  Check ($auto.reply.ok-and$auto.reply.di_poll_confirmed-and$auto.reply.di_poll_channel-eq'state'-and$auto.reply.button_down_count-eq1-and$auto.reply.button_released) "$suffix auto slow state poll confirms one press and releases"
  Check ((Count $t 'CONSUMED_DOWN')-eq$prior+1-and$auto.reply.button_hold_ms-ge60-and$auto.reply.button_hold_ms-gt150) "$suffix actual DI-only consumer sees formerly missed60ms click"
  WaitPoll $t;$prior=Count $t 'CONSUMED_DOWN'
  $duration=Call $t @('json','{"cmd":"click","x":80,"y":70,"ms":400}')
  Check ($duration.reply.ok-and$duration.reply.di_poll_confirmed-and$duration.reply.button_hold_ms-ge400-and(Count $t 'CONSUMED_DOWN')-eq$prior+1) "$suffix explicit400ms remains a minimum after early DI read"
  WaitPoll $t;$prior=Count $t 'CONSUMED_DOWN'
  $bypass=Call $t @('click','left','80','70','--di-poll-timeout-ms','0');Start-Sleep -Milliseconds 400
  Check ($bypass.reply.ok-and$bypass.reply.di_poll_mode-eq'bypass'-and-not$bypass.reply.di_poll_waited-and$bypass.reply.button_hold_ms-lt200-and(Count $t 'CONSUMED_DOWN')-eq$prior) "$suffix explicit bypass keeps short dwell and independent consumer misses it"
  WaitPoll $t;Control $t 1 0 'POLLING 0';$prior=Count $t 'CONSUMED_DOWN'
  $stopped=Call $t @('click','left','80','70');$trace=(Call $t @('trace','key','lbutton')).reply
  Check ($stopped.reply.ok-and-not$stopped.reply.di_poll_confirmed-and$stopped.reply.di_poll_timed_out-and$stopped.reply.di_poll_warning-and$stopped.reply.button_down_count-eq1-and-not$trace.virtual_down-and(Count $t 'CONSUMED_DOWN')-eq$prior) "$suffix recent stopped consumer warns after bounded release without declaring WM rejection"
  $required=Call $t @('click','left','80','70','--di-poll-timeout-ms','200','--count','2');$trace=(Call $t @('trace','key','lbutton')).reply
  Check (-not$required.reply.ok-and$required.exit-ne0-and$required.reply.button_down_count-eq1-and$required.reply.button_released-and-not$trace.virtual_down-and$required.elapsed_ms-lt1000) "$suffix required observation times out with exactly one released press"
  Start-Sleep -Milliseconds 2100;$aged=Call $t @('click','left','80','70')
  Check ($aged.reply.ok-and-not$aged.reply.di_poll_waited-and-not$aged.reply.di_poll_consumer_recent-and$aged.reply.button_hold_ms-lt200) "$suffix old consumer history cannot force future WM waits"
  $down=Call $t @('mousedown','left');$held=(Call $t @('trace','key','lbutton')).reply;$up=Call $t @('mouseup','left');$released=(Call $t @('trace','key','lbutton')).reply
  Check ($down.reply.ok-and$held.virtual_down-and$down.elapsed_ms-lt500-and$up.reply.ok-and-not$released.virtual_down) "$suffix indefinite mousedown stays immediate until explicit up"
  Stop-Process -Id $t.pid -Force
  $t=StartTarget $suffix 'wm' '--wm-only';$wm=Call $t @('click','left','80','70');Start-Sleep -Milliseconds 30
  Check ($wm.reply.ok-and-not$wm.reply.di_poll_waited-and$wm.reply.button_hold_ms-lt200-and(Count $t 'WM_ACCEPT')-eq1) "$suffix WM-only consumer keeps normal latency and actually accepts click"
  Stop-Process -Id $t.pid -Force
  $t=StartTarget $suffix 'unknown' '--stopped';$unknown=Call $t @('json','{"cmd":"click","x":80,"y":70,"di_poll_timeout_ms":200}');$trace=(Call $t @('trace','key','lbutton')).reply
  Check (-not$unknown.reply.ok-and$unknown.reply.di_poll_mode-eq'required'-and$unknown.reply.button_down_count-eq1-and-not$trace.virtual_down) "$suffix unknown required consumer releases on finite timeout"
  $before=(Call $t @('trace','key','lbutton')).reply;$bad=Call $t @('json','{"cmd":"click","x":80,"y":70,"di_poll_timeout_ms":true}');$after=(Call $t @('trace','key','lbutton')).reply
  Check (-not$bad.reply.ok-and$bad.exit-ne0-and$after.injected_down-eq$before.injected_down) "$suffix malformed poll cap is rejected without a press"
  Stop-Process -Id $t.pid -Force
  $t=StartTarget $suffix 'keyboard' '--keyboard-only' 'di_kbd';$keys=Call $t @('click','left','80','70','--di-poll-timeout-ms','200');$trace=(Call $t @('trace','key','lbutton')).reply
  Check (-not$keys.reply.ok-and-not$keys.reply.di_poll_confirmed-and$trace.channels.DirectInputState.reads-eq0-and-not$trace.virtual_down) "$suffix keyboard DI polling never confirms mouse button"
  Stop-Process -Id $t.pid -Force
  $t=StartTarget $suffix 'short' '--short-state' 'di_mouse';$x2=Call $t @('click','x2','80','70','--di-poll-timeout-ms','200');$trace=(Call $t @('trace','key','xbutton2')).reply
  Check (-not$x2.reply.ok-and-not$x2.reply.di_poll_confirmed-and$trace.channels.DirectInputState.reads-eq0-and-not$trace.virtual_down) "$suffix four-button state reads never confirm fifth button"
  Stop-Process -Id $t.pid -Force
  $t=StartTarget $suffix 'buffer_current' '--buffered' 'di_data_mouse';$prior=Count $t 'CONSUMED_DOWN';$buffer=Call $t @('click','left','80','70')
  Check ($buffer.reply.ok-and$buffer.reply.di_poll_confirmed-and$buffer.reply.di_poll_channel-eq'buffer'-and$buffer.reply.button_down_count-eq1-and$buffer.reply.button_released-and(Count $t 'CONSUMED_DOWN')-eq$prior+1) "$suffix current buffered button generation confirms actual returned down"
  Stop-Process -Id $t.pid -Force
  $t=StartTarget $suffix 'stale_buffer' '--buffered --stopped';OneBufferPoll $t 1
  $centerX=[math]::Floor($t.w/2);$centerY=[math]::Floor($t.hgt/2)
  $old=Call $t @('json',('{"cmd":"click","x":'+$centerX+',"y":'+$centerY+',"di_poll_timeout_ms":0}'))
  OneBufferPoll $t 2 # Drain the old hover's two axis entries, retaining its buffered down/up.
  Control $t 2 1 'ONE_POLL_ARMED';$stale=Call $t @('json',('{"cmd":"click","x":'+$centerX+',"y":'+$centerY+',"di_poll_timeout_ms":500}'));Start-Sleep -Milliseconds 350;$trace=(Call $t @('trace','key','lbutton')).reply
  Check (-not$stale.reply.ok-and$stale.reply.button_down_count-eq1-and-not$stale.reply.di_poll_confirmed-and$trace.channels.DirectInputBuffer.down_reads-ge1-and-not$trace.virtual_down) "$suffix stale buffered down cannot confirm newer press generation"
  Stop-Process -Id $t.pid -Force
 }
}finally{foreach($p in $owned){if(-not$p.HasExited){Stop-Process -Id $p.Id -Force}};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
