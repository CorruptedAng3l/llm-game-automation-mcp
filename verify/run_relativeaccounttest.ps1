param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\relative_account\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_ownedprocess.ps1";$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";New-Item -ItemType Directory -Force $tools,"$tools\temp"|Out-Null
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViRelativeNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=@()
function Check($Ok,$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Call([string[]]$Arguments){$r=& $V --json --pid $script:targetId @Arguments|ConvertFrom-Json;$records.Add(@{args=$Arguments;reply=$r});return $r}
function LogText {Get-Content $script:log -Raw}
function Control($Message,$Pattern,$X=0,$Y=0){$n=[regex]::Matches((LogText),$Pattern,[Text.RegularExpressions.RegexOptions]::Multiline).Count;[ViRelativeNative]::PostMessage($script:h,0x8000+$Message,[IntPtr]$X,[IntPtr]$Y)|Out-Null;$until=[Environment]::TickCount64+3000;do{$text=LogText;if([regex]::Matches($text,$Pattern,[Text.RegularExpressions.RegexOptions]::Multiline).Count-gt$n){$line=@($text-split'\r?\n'|Where-Object{$_-match$Pattern})[-1];$records.Add(@{control=$Message;line=$line});return $line};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$until);throw 'owned relative-meter control not dispatched'}
function CleanMeters {$null=Control 1 '^STATE ';$null=Control 1 '^STATE ';$null=Control 4 '^DI_BUFFER ';$null=Control 5 '^RESET'}
try{
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX "$root\test\vi_relativeaccount.cpp" /Fo:"$tools\relative$suffix.obj" /Fe:"$tools\relative$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture zero warnings"
  $script:log="$Out\relative$suffix.log";$p=Start-Process "$tools\relative$suffix.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned+=$p;$script:targetId=$p.Id;$r=& $V --json inject --pid $p.Id|ConvertFrom-Json;Check $r.ok "$arch injected";$s=Call @('config','cursor=virtual','raw=post');Check (-not$s.really_foreground) "$arch background only";$script:h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16));CleanMeters
  $null=Control 2 '^CURSOR_SET' 100 100;$r=Call @('move','--raw','1003','-999','--steps','7','--step-ms','0');$actual=Control 6 '^RAW_POST '
  Check ($r.ok-and$r.requested_dx-eq1003-and$r.applied_dx-eq1003-and$r.device_dx-eq1003-and$r.relative_delivery.raw_posted_dx-eq1003-and$r.relative_delivery.raw_posted_dy-eq-999-and$r.relative_delivery.raw_submission_exact-and$actual-match'total_x=1003 total_y=-999') "$arch exact requested/cursor/device/posted totals and actual returned raw delta across7 rounded steps"
  CleanMeters;$null=Control 2 '^CURSOR_SET' 2147483547 100;$r=Call @('move','--raw','1003','-999','--steps','7','--step-ms','0');$actual=Control 6 '^RAW_POST '
  Check ($r.ok-and$r.requested_dx-eq1003-and$r.applied_dx-eq100-and$r.cursor_saturated-and$r.relative_delivery.raw_posted_dx-eq1003-and$actual-match'total_x=1003 total_y=-999') "$arch numeric cursor saturation is explicit while actual raw device intent stays exact"
  CleanMeters;$null=Control 2 '^CURSOR_SET' 100 100;$r=Call @('move','1000','-800','--steps','7','--step-ms','0');$actual=Control 6 '^RAW_POST '
  Check ($r.ok-and$r.cursor_clamped-and$r.requested_dx-eq1000-and$r.applied_dx-eq$s.client_w-101-and$r.relative_delivery.raw_posted_dx-eq1000-and$actual-match'total_x=1000 total_y=-800') "$arch bounded cursor travel is distinguished from full submitted relative intent"
  $before=Call @('status');$r=Call @('json','{"cmd":"move","dx":1.5,"dy":0,"raw":true}');$after=Call @('status');Check (-not$r.ok-and$after.counters.raw_posted-eq$before.counters.raw_posted-and$after.vcur_x-eq$before.vcur_x) "$arch fractional relative intent is rejected before movement instead of silently truncated"
  CleanMeters
  if($arch-eq'x64'){$null=Call @('config','raw=buffer');$r=Call @('move','--raw','1500','0','--steps','1500','--step-ms','0');$actual=Control 3 '^RAW_BUFFER '
   Check ($r.ok-and$r.relative_delivery.raw_queued_dx-eq1024-and$r.relative_delivery.raw_failed_dx-eq476-and$r.relative_delivery.raw_failures-eq476-and-not$r.relative_delivery.raw_submission_exact-and$r.relative_delivery_warning-and$actual-match'total_x=1024 total_y=0') "$arch queue-full reports exact retained and failed deltas and actual drained total"}
  CleanMeters;$null=Call @('config','raw=off','msgs=off');$scriptPath="$Out\pressure$suffix.txt";[IO.File]::WriteAllLines($scriptPath,[string[]](@('move --raw 1000000 0 --step-ms 0')*2200));$progress="$Out\pressure$suffix.progress.jsonl";$r=& $V --json --pid $targetId script $scriptPath 2> $progress|ConvertFrom-Json;$records.Add(@{pressure_reply=$r;progress=$progress});Check ($r.ok-and$r.commands_run-eq2200) "$arch actual2200-request pressure script completes"
  $a=Control 1 '^STATE ';$b=Control 1 '^STATE ';$buffer=Control 4 '^DI_BUFFER '
  Check ($b-match'total_x=2200000000 total_y=0'-and$a-match'dx=2147483647') "$arch unread DirectInput accumulation preserves all2.2billion units across bounded LONG reads"
  $sumState=0L;$sumBuffer=0L;$drops=0L;$last=$null;foreach($line in [IO.File]::ReadLines($progress)){if($line.StartsWith('{')){$last=$line|ConvertFrom-Json;$sumState+=[long]$last.reply.relative_delivery.di_state_accumulated_dx;$sumBuffer+=[long]$last.reply.relative_delivery.di_buffer_queued_dx;$drops+=[long]$last.reply.relative_delivery.di_buffer_dropped_events}}
  Check ($sumState-eq2200000000-and$sumBuffer-eq256000000-and$drops-eq3888-and$buffer-match'total_x=256000000'-and$last.reply.cursor_saturated) "$arch per-channel accepted/dropped accounting matches actual DirectInput buffer drain; no silent wrap or clamp"
  Stop-ViOwnedProcess $p
 }
}finally{foreach($p in $owned){Stop-ViOwnedProcess $p};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
