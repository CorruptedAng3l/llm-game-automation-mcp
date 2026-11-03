param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\placement\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_ownedprocess.ps1";$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";New-Item -ItemType Directory -Force $tools,"$tools\temp"|Out-Null
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViPlaceNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=@();$current=$null
function Check($Ok,$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Call($T,[string[]]$Arguments){$watch=[Diagnostics.Stopwatch]::StartNew();$raw=@(& $V --json --pid $T.pid @Arguments);$code=$LASTEXITCODE;$watch.Stop();$r=@{args=$Arguments;exit=$code;elapsed_ms=$watch.ElapsedMilliseconds;reply=($raw|ConvertFrom-Json)};$script:current.calls.Add($r);return $r}
function Text($T){Get-Content $T.log -Raw -ErrorAction SilentlyContinue}
function WaitLog($T,$Pattern,$Timeout=2500){$until=[Environment]::TickCount64+$Timeout;do{if((Text $T)-match$Pattern){return $true};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);return $false}
function StartTarget($Suffix,$Name,$Extra=''){
 $script:current=@{arch=$Suffix;name=$Name;calls=[Collections.Generic.List[object]]::new()};$records.Add($current);$log="$Out\$Name$Suffix.log"
 $p=Start-Process "$tools\placement$Suffix.exe" -ArgumentList "`"$log`" $Extra" -WindowStyle Hidden -PassThru;$script:owned+=$p
 $r=& $V --json inject --pid $p.Id|ConvertFrom-Json;if(-not$r.ok){throw 'inject failed'};$t=@{pid=$p.Id;p=$p;log=$log};$until=[Environment]::TickCount64+2500
 do{$s=(Call $t @('status')).reply;if($s.hwnd-ne'0x0'-and$s.counters.gcur-gt0){break};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until)
 if($s.hwnd-eq'0x0'-or$s.really_foreground){throw 'background fixture not ready'};$t.h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16));return $t
}
function WaitJob($T,$Id){$until=[Environment]::TickCount64+12000;do{$r=(Call $T @('job-status',$Id)).reply;if($r.job.job_complete){return $r.job};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until);throw 'placement job timed out'}
try{
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_placement.cpp" /Fo:"$tools\placement$suffix.obj" /Fe:"$tools\placement$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture zero warnings"}
 foreach($suffix in @('','32')){
  $t=StartTarget $suffix 'control';$null=Call $t @('mousedown','left','40','50');Check (WaitLog $t ' PICKUP\r?\n') "$suffix control actual consumer picks up before motion"
  $null=Call $t @('mouse','240','170','--steps','5','--step-ms','1');$null=Call $t @('mouseup','left')
  Check (WaitLog $t 'DROPPED') "$suffix unconfirmed fast motion releases before final position and drops item"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'complete';$r=Call $t @('placement-drag','40','50','240','170','--steps','5','--ms','50','--dwell-ms','450','--poll-timeout-ms','1000')
  Check ($r.reply.ok-and$r.reply.steps_consumed-eq5-and$r.reply.final_position_seen-and$r.reply.di_press_release_confirmed-and$r.reply.final_position_before_release-and$r.reply.dwell_ms-eq450) "$suffix placement confirms press, each position, dwell, final before release, and up"
  Check ((WaitLog $t 'PLACED')-and(Text $t)-notmatch'DROPPED' -and[regex]::Matches((Text $t),'STEP x=').Count-eq5) "$suffix independent slow consumer actually places at final point after all five steps"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'async';$r=Call $t @('drag','40','50','240','170','--placement','--async','--steps','5','--ms','50','--dwell-ms','450')
  Check ($r.reply.ok-and$r.reply.accepted-and-not$r.reply.job_complete-and$r.elapsed_ms-lt1000) "$suffix explicit placement drag shares async worker"
  if($r.reply.job_id){$cap=Call $t @('capture',"$Out\async$suffix.png",'--method','printwindow');Check ($cap.reply.ok-and$cap.elapsed_ms-lt1000) "$suffix capture remains usable during polled placement";$done=WaitJob $t $r.reply.job_id;Check ($done.ok-and(WaitLog $t 'PLACED')) "$suffix async placement completes with actual placement"}
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'cancel';$r=Call $t @('placement-drag','40','50','240','170','--async','--steps','5','--ms','50','--dwell-ms','450');if($r.reply.job_id){Check (WaitLog $t ' PICKUP\r?\n') "$suffix cancellable job reaches observed pickup";$null=Call $t @('mouse','100','100');$done=WaitJob $t $r.reply.job_id;$trace=(Call $t @('trace','key','lbutton')).reply;Check ($done.job_state-eq'cancelled'-and-not$trace.virtual_down-and-not$done.final_position_before_release) "$suffix manual pointer override cancels exact job and releases its button"}
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'stalled';$r=Call $t @('placement-drag','40','50','240','170','--async','--steps','5','--ms','50','--poll-timeout-ms','500');if($r.reply.job_id){Check (WaitLog $t ' PICKUP\r?\n') "$suffix stalled case reaches actual pickup";[ViPlaceNative]::PostMessage($t.h,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;$done=WaitJob $t $r.reply.job_id;$trace=(Call $t @('trace','key','lbutton')).reply;Check (-not$done.ok-and$done.job_reason-in@('step_position_not_observed','press_position_not_observed','button_down_not_observed')-and-not$done.final_position_seen-and-not$trace.virtual_down) "$suffix missing current-step consumption fails boundedly and releases"}
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'resize';$r=Call $t @('placement-drag','40','50','240','170','--async','--steps','5','--ms','50');if($r.reply.job_id){Check (WaitLog $t ' PICKUP\r?\n') "$suffix resize case reaches actual pickup";[ViPlaceNative]::PostMessage($t.h,0x8003,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;$done=WaitJob $t $r.reply.job_id;Check ($done.job_state-eq'cancelled'-and$done.job_reason-eq'pointer_target_resized'-and-not(Call $t @('trace','key','lbutton')).reply.virtual_down) "$suffix resize cancels before using stale placement geometry"}
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'lost_reply';$replyPath="$Out\lost_reply$suffix.json";$client=Start-Process $V -ArgumentList "--json --pid $($t.pid) placement-drag 40 50 240 170 --steps 5 --ms 50 --dwell-ms 450" -RedirectStandardOutput $replyPath -WindowStyle Hidden -PassThru;$script:owned+=$client
  Check (WaitLog $t ' PICKUP\r?\n') "$suffix synchronous client reaches actual pickup";Stop-ViOwnedProcess $client
  Check ((WaitLog $t 'PLACED' 6000)-and-not(Call $t @('trace','key','lbutton')).reply.virtual_down) "$suffix lost client reply cannot strand the owned placement button"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'drift';[ViPlaceNative]::PostMessage($t.h,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;$r=Call $t @('placement-drag','40','50','240','170','--steps','5','--ms','50','--dwell-ms','450')
  Check (-not$r.reply.ok-and$r.reply.job_state-eq'cancelled'-and$r.reply.job_reason-eq'game_cursor_drift'-and-not(Call $t @('trace','key','lbutton')).reply.virtual_down-and(WaitLog $t 'GAME_CURSOR_DRIFT')) "$suffix game cursor drift invalidates transaction and cleans up"
  Stop-ViOwnedProcess $t.p
  $t=StartTarget $suffix 'unsupported' '--cursor-only';$r=Call $t @('placement-drag','40','50','240','170','--steps','5','--ms','50','--poll-timeout-ms','200')
  Check (-not$r.reply.ok-and-not$r.reply.di_poll_confirmed-and-not(Call $t @('trace','key','lbutton')).reply.virtual_down) "$suffix cursor-only reader cannot invent button polling evidence"
  Stop-ViOwnedProcess $t.p
 }
}finally{foreach($p in $owned){if(-not$p.HasExited){Stop-ViOwnedProcess $p}};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 14|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
