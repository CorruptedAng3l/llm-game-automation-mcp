param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\di_release\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_ownedprocess.ps1";$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";New-Item -ItemType Directory -Force $tools,"$tools\temp"|Out-Null
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViReleaseNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=@();$current=$null
function Check($Ok,$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Call($T,[string[]]$Arguments){$watch=[Diagnostics.Stopwatch]::StartNew();$raw=@(& $V --json --pid $T.pid @Arguments);$code=$LASTEXITCODE;$watch.Stop();$r=@{args=$Arguments;exit=$code;elapsed_ms=$watch.ElapsedMilliseconds;reply=($raw|ConvertFrom-Json)};$script:current.calls.Add($r);return $r}
function Text($T){Get-Content $T.log -Raw -ErrorAction SilentlyContinue}
function Control($T,$Message,$Value,$Pattern){$n=[regex]::Matches((Text $T),$Pattern).Count;[ViReleaseNative]::PostMessage($T.h,0x8000+$Message,[IntPtr]$Value,[IntPtr]::Zero)|Out-Null;$until=[Environment]::TickCount64+2000;do{if([regex]::Matches((Text $T),$Pattern).Count-gt$n){return};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$until);throw 'control not dispatched'}
function OneBufferPoll($T,$Capacity){$n=[regex]::Matches((Text $T),'BUFFER_POLL').Count;Control $T 2 $Capacity 'ONE_POLL_ARMED';$until=[Environment]::TickCount64+2000;do{if([regex]::Matches((Text $T),'BUFFER_POLL').Count-gt$n){return};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$until);throw 'buffer poll not returned'}
function StartTarget($Suffix,$Name,$Extra=''){
 $script:current=@{arch=$Suffix;name=$Name;calls=[Collections.Generic.List[object]]::new()};$records.Add($current);$log="$Out\$Name$Suffix.log"
 $p=Start-Process "$tools\release$Suffix.exe" -ArgumentList "`"$log`" $Extra" -WindowStyle Hidden -PassThru;$script:owned+=$p
 $r=& $V --json inject --pid $p.Id|ConvertFrom-Json;if(-not$r.ok){throw 'inject failed'};$t=@{pid=$p.Id;p=$p;log=$log};$until=[Environment]::TickCount64+2500
 do{$s=(Call $t @('status')).reply;if($s.hwnd-ne'0x0'-and($s.counters.di_mouse-gt0-or$s.counters.di_data_mouse-gt0-or$s.counters.di_kbd-gt0)){break};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until)
 if($s.hwnd-eq'0x0'-or$s.really_foreground){throw 'background fixture not ready'};$t.h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16));return $t
}
try{
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_releasepoll.cpp" /Fo:"$tools\release$suffix.obj" /Fe:"$tools\release$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture zero warnings"}
 foreach($suffix in @('','32')){
  $t=StartTarget $suffix 'state';$r=Call $t @('click','left','80','70')
  Check ($r.reply.ok-and$r.reply.di_poll_confirmed-and$r.reply.di_release_confirmed-and$r.reply.di_press_release_confirmed-and$r.reply.di_release_channel-eq'state') "$suffix default confirms current press AND release state reads"
  $lines=(Text $t)-split'\r?\n';$down=@($lines|Where-Object{$_-match'STATE_POLL.*left=1'});$after=(Text $t).Substring((Text $t).IndexOf($down[-1]));Check ($after-match'STATE_POLL.*left=0'-and$r.reply.button_released) "$suffix actual consumer read up after down before reply"
  $r=Call $t @('click','left','80','70','--di-poll-timeout-ms','0');Check ($r.reply.ok-and-not$r.reply.di_release_waited-and-not$r.reply.di_release_confirmed) "$suffix explicit bypass never invents release evidence"
  $before=(Call $t @('trace','key','lbutton')).reply;$bad=Call $t @('json','{"cmd":"click","x":80,"y":70,"di_release_timeout_ms":true}');$after=(Call $t @('trace','key','lbutton')).reply
  Check (-not$bad.reply.ok-and$after.injected_down-eq$before.injected_down) "$suffix malformed release cap rejected before press"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'slow' '--slow-release';$r=Call $t @('click','left','80','70')
  Check ($r.reply.ok-and$r.reply.di_release_confirmed-and$r.reply.di_release_wait_ms-ge500-and$r.reply.di_release_wait_ms-lt1500) "$suffix slow release poll is awaited within bounded cap"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'stopped_required' '--stop-after-down';$r=Call $t @('json','{"cmd":"click","x":80,"y":70,"di_poll_timeout_ms":500,"di_release_timeout_ms":200}');$trace=(Call $t @('trace','key','lbutton')).reply
  Check (-not$r.reply.ok-and$r.exit-ne0-and$r.reply.di_poll_confirmed-and-not$r.reply.di_release_confirmed-and$r.reply.di_release_timed_out-and-not$trace.virtual_down-and$r.reply.button_down_count-eq1-and$r.elapsed_ms-lt2000) "$suffix stop after down cannot satisfy required release; no retry and button up"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'stopped_auto' '--stop-after-down';$r=Call $t @('click','left','80','70')
  Check ($r.reply.ok-and$r.reply.di_poll_confirmed-and$r.reply.di_release_timed_out-and$r.reply.di_release_warning-and$r.reply.button_released) "$suffix auto release timeout warns with truthful down-only evidence"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'buffer' '--buffered';$r=Call $t @('click','left','80','70')
  Check ($r.reply.ok-and$r.reply.di_poll_confirmed-and$r.reply.di_release_confirmed-and$r.reply.di_release_channel-eq'buffer') "$suffix buffered current up token confirms release"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'stale' '--hybrid --stopped';OneBufferPoll $t 1;$null=Call $t @('mousedown','left');$null=Call $t @('mouseup','left');OneBufferPoll $t 1;Control $t 1 1 'POLLING 1'
  $r=Call $t @('json','{"cmd":"click","x":80,"y":70,"di_poll_timeout_ms":500,"di_release_timeout_ms":400}')
  Check (-not$r.reply.ok-and$r.reply.di_poll_confirmed-and-not$r.reply.di_release_confirmed-and$r.reply.di_release_timed_out-and(Text $t)-match'BUFFER_BUTTON button=0 down=0') "$suffix stale buffered up cannot confirm current release token"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'keyboard' '--keyboard-only';$r=Call $t @('json','{"cmd":"click","x":80,"y":70,"di_poll_timeout_ms":100,"di_release_timeout_ms":100}')
  Check (-not$r.reply.ok-and-not$r.reply.di_release_confirmed-and$r.reply.button_released) "$suffix keyboard polling cannot confirm mouse release"
  Stop-ViOwnedProcess $t.p
 }
}finally{foreach($p in $owned){if(-not$p.HasExited){Stop-ViOwnedProcess $p}};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 14|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
