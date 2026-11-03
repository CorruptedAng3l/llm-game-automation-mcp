# Deterministic slow native Presents, bounded actual CPU/GPU neighbor, no foreground/input desktop.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\hover_load\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
. "$PSScriptRoot\vi_textlog.ps1"
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools=Join-Path $Out 'tools';New-Item -ItemType Directory -Force -Path @($tools,"$tools\temp")|Out-Null
if(-not('ViHoverLoadNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViHoverLoadNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'}
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new()
function Check($Ok,[string]$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function ReadLog($Target){Read-ViSharedText $Target.log}
function Count($Target,[string]$Pattern){[regex]::Matches((ReadLog $Target),$Pattern).Count}
function Vi($GamePid,[string[]]$Arguments){$watch=[Diagnostics.Stopwatch]::StartNew();$reply=& $V --json --pid $GamePid @Arguments|ConvertFrom-Json;$code=$LASTEXITCODE;$watch.Stop();return [pscustomobject]@{reply=$reply;exit=$code;elapsed_ms=$watch.ElapsedMilliseconds}}
function Control($Target,[int]$Message,[int]$Value,[string]$Pattern){$old=Count $Target $Pattern;if(-not[ViHoverLoadNative]::PostMessage($Target.h,0x8000+$Message,[IntPtr]$Value,[IntPtr]::Zero)){throw 'owned fixture control failed'};$until=[Environment]::TickCount64+2000;do{if((Count $Target $Pattern)-gt$old){return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "control not observed: $Pattern"}
try{
 foreach($arch in @('x64','x86')){
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'}
  $batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_hoverload.cpp" /Fo:"$tools\hover$suffix.obj" /Fd:"$tools\hover$suffix.pdb" /Fe:"$tools\vi_hoverload$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
exit /b 0
"@
  [IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch hover/load fixture builds /W4 /WX"
 }
 foreach($suffix in @('','32')){
  $arch=if($suffix){'x86'}else{'x64'};$exe="$tools\vi_hoverload$suffix.exe";$log="$Out\target$suffix.log"
  $worker=Start-Process -FilePath $exe -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned.Add($worker)
  $injected=& $V --json inject --pid $worker.Id|ConvertFrom-Json;Check ($LASTEXITCODE-eq0-and$injected.ok) "$arch target injected"
  $until=[Environment]::TickCount64+5000;do{$s=(Vi $worker.Id @('status')).reply;if($s.counters.present_ours-gt3){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
  Check ($s.counters.present_ours-gt3-and-not$s.really_foreground) "$arch actual native Present readiness in background"
  $target=@{pid=$worker.Id;h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16));log=$log}
  $neighborLog="$Out\neighbor$suffix.log";$neighbor=Start-Process -FilePath $exe -ArgumentList "`"$neighborLog`" --neighbor-ms 6000" -WindowStyle Hidden -PassThru;$owned.Add($neighbor)
  $until=[Environment]::TickCount64+4000;do{$busy=Read-ViSharedText $neighborLog;$begin=[regex]::Match($busy,'(\d+) NEIGHBOR_BEGIN duration_ms=6000');if($begin.Success){$neighborStart=[long]$begin.Groups[1].Value;break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
  Check ($busy-match'NEIGHBOR_BEGIN') "$arch bounded two-CPU-worker GPU-copy neighbor starts"
  Control $target 1 700 'INTERVAL 700'
  $beforeDown=Count $target 'DOWN accepted=';$slowStart=[Environment]::TickCount64
  $cli=Vi $worker.Id @('click','left','80','70')
  Check ($cli.reply.ok-and$cli.exit-eq0-and$cli.reply.hover_frames-ge3) "$arch CLI omitted timeout accepts three slow native Presents without retry"
  Check ($cli.reply.hover_adaptive-eq$true-and$cli.reply.hover_frames_requested-eq3-and$cli.reply.hover_idle_budget_ms-eq1500-and$cli.reply.hover_absolute_budget_ms-eq5000-and$cli.reply.hover_wait_ms-gt1500) "$arch CLI reports actual extended wait and default progress budgets"
  Check ((Count $target 'DOWN accepted=1')-eq$beforeDown+1-and(Count $target 'DOWN accepted=0')-eq0-and$cli.reply.button_down_count-eq1) "$arch consumer observes exactly one accepted CLI press after three frames"
  Control $target 1 700 'INTERVAL 700'
  $raw=Vi $worker.Id @('json','{"cmd":"click","x":100,"y":90}')
  $slowEnd=[Environment]::TickCount64
  Check ($raw.reply.ok-and$raw.exit-eq0-and$raw.reply.hover_adaptive-eq$true-and$raw.reply.hover_frames-ge3-and$raw.reply.hover_wait_ms-gt1500) "$arch raw JSON omission uses progress adaptation"
  Check ((Count $target 'DOWN accepted=1')-eq$beforeDown+2-and(Count $target 'DOWN accepted=0')-eq0-and$raw.reply.button_down_count-eq1) "$arch raw JSON sends exactly one accepted press"
  if(-not$neighbor.WaitForExit(5000)){throw 'bounded neighbor exceeded cleanup deadline'}
  $busy=Read-ViSharedText $neighborLog;$measured=[regex]::Match($busy,'(\d+) NEIGHBOR_DONE completed_batches=(\d+) cpu_ops=(\d+) elapsed_ms=(\d+)')
  Check ($neighbor.ExitCode-eq0-and$measured.Success-and[long]$measured.Groups[2].Value-gt0-and[long]$measured.Groups[3].Value-gt0-and$neighborStart-le$slowStart-and[long]$measured.Groups[1].Value-ge$slowEnd) "$arch actual completed GPU-copy batches and CPU work overlap both slow clicks"
  $prior=Count $target 'DOWN accepted='
  Control $target 1 700 'INTERVAL 700'
  $explicit=Vi $worker.Id @('click','left','120','100','--hover-timeout-ms','1500')
  Check (-not$explicit.reply.ok-and$explicit.exit-ne0-and$explicit.elapsed_ms-lt2300-and(Count $target 'DOWN accepted=')-eq$prior) "$arch explicit CLI total cap still fails before pressing"
  Check ($explicit.reply.hover_adaptive-eq$false-and$explicit.reply.hover_absolute_budget_ms-eq1500-and$explicit.reply.hover_frames-lt3-and$explicit.reply.button_down_count-eq0) "$arch explicit CLI failure reports frames and total budget"
  Control $target 1 700 'INTERVAL 700'
  $rawExplicit=Vi $worker.Id @('json','{"cmd":"click","x":130,"y":110,"hover_timeout_ms":1500}')
  Check (-not$rawExplicit.reply.ok-and$rawExplicit.exit-ne0-and$rawExplicit.reply.hover_adaptive-eq$false-and$rawExplicit.reply.hover_absolute_budget_ms-eq1500-and(Count $target 'DOWN accepted=')-eq$prior) "$arch raw explicit timeout preserves total-cap semantics"
  Control $target 2 1 'PAUSED 1'
  $stopped=Vi $worker.Id @('click','left','140','120')
  Check (-not$stopped.reply.ok-and$stopped.exit-ne0-and$stopped.reply.hover_wait_reason-eq'render_idle_timeout'-and$stopped.reply.hover_frames-eq0-and$stopped.elapsed_ms-lt2300) "$arch zero Present progress fails at idle deadline rather than five-second cap"
  $trace=(Vi $worker.Id @('trace','key','lbutton')).reply
  Check ((Count $target 'DOWN accepted=')-eq$prior-and-not$trace.virtual_down-and$stopped.reply.button_down_count-eq0) "$arch stalled-render failure injects no button-down and leaves button released"
  $hold=Vi $worker.Id @('mousedown','left');$held=(Vi $worker.Id @('trace','key','lbutton')).reply;$up=Vi $worker.Id @('mouseup','left');$released=(Vi $worker.Id @('trace','key','lbutton')).reply
  Check ($hold.reply.ok-and$held.virtual_down-and$up.reply.ok-and-not$released.virtual_down) "$arch explicit mouse-up still releases while rendering is paused"
  Control $target 2 0 'PAUSED 0';Control $target 1 80 'INTERVAL 80';$prior=Count $target 'DOWN accepted='
  $absolute=Vi $worker.Id @('click','left','150','130','--hover-frames','100')
  Check (-not$absolute.reply.ok-and$absolute.exit-ne0-and$absolute.reply.hover_wait_reason-eq'render_absolute_timeout'-and$absolute.reply.hover_frames-gt20-and$absolute.reply.hover_frames-lt100-and$absolute.elapsed_ms-ge4800-and$absolute.elapsed_ms-lt6000) "$arch continuous native progress cannot extend the absolute budget indefinitely"
  $released=(Vi $worker.Id @('trace','key','lbutton')).reply
  Check ((Count $target 'DOWN accepted=')-eq$prior-and-not$released.virtual_down-and$absolute.reply.button_down_count-eq0-and$absolute.reply.hover_absolute_budget_ms-eq5000) "$arch absolute-budget failure retains frame diagnostics and released state"
  $records.Add(@{arch=$arch;pid=$worker.Id;cli=$cli;raw=$raw;explicit_cli=$explicit;explicit_raw=$rawExplicit;stopped=$stopped;absolute=$absolute;neighbor=@{start=$neighborStart;end=[long]$measured.Groups[1].Value;completed_batches=$measured.Groups[2].Value;cpu_ops=$measured.Groups[3].Value}})
  Stop-Process -Id $worker.Id -Force -Confirm:$false
 }
}finally{foreach($worker in $owned){if(-not$worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false}};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
