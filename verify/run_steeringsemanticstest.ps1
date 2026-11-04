param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\steering_semantics\after",[switch]$KeepGoing,[ValidateRange(0,100)][int]$SampleDelayMs=0)
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_ownedprocess.ps1";. "$PSScriptRoot\vi_pwmmeasure.ps1";$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";New-Item -ItemType Directory -Force $tools,"$tools\temp"|Out-Null
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViSteerNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=@();$token=0
function Check($Ok,$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Call([string[]]$Arguments){$r=& $V --json --pid $script:targetId @Arguments|ConvertFrom-Json;$records.Add(@{args=$Arguments;reply=$r});return $r}
function Text {Get-Content $script:log -Raw}
function Control($Message,$Pattern){[ViSteerNative]::PostMessage($script:h,0x8000+$Message,[IntPtr]$script:token,[IntPtr]::Zero)|Out-Null;$until=[Environment]::TickCount64+2000;do{if((Text)-match$Pattern){return};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$until);throw 'measurement control not dispatched'}
function MeasureSteering([string[]]$Arguments){$script:token++;Control 1 "BEGIN token=$token\r?\n";$start=(Text).Length;$r=Call $Arguments;Control 2 "MEASURE token=$token ";$samples=(Text).Substring($start);$m=[regex]::Match($samples,"MEASURE token=$token elapsed=(\d+) on_ms=(\d+) lx_area=(-?\d+)");if(-not$m.Success){throw 'measurement result missing'};$result=@{reply=$r;elapsed_ms=[long]$m.Groups[1].Value;on_ms=[long]$m.Groups[2].Value;lx_area=[long]$m.Groups[3].Value;samples=$samples;arguments=$Arguments};$records.Add($result);return $result}
try{
 & pwsh -NoProfile -File "$PSScriptRoot\run_pwmmeasuretest.ps1" -Out "$Out\poll_scoring";Check ($LASTEXITCODE-eq0) 'target-poll scorer accepts quantization and rejects wrong or insufficient observations'
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX "$root\test\vi_steeringmeasure.cpp" /Fo:"$tools\steer$suffix.obj" /Fe:"$tools\steer$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture zero warnings"
  $script:log="$Out\steer$suffix.log";$p=Start-Process "$tools\steer$suffix.exe" -ArgumentList "`"$log`" $SampleDelayMs" -WindowStyle Hidden -PassThru;$owned+=$p;$script:targetId=$p.Id;$r=& $V --json inject --pid $p.Id|ConvertFrom-Json;Check $r.ok "$arch injected";$s=Call @('status');Check (-not$s.really_foreground) "$arch background only";$script:h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16))
  $full=MeasureSteering @('hold','d','1200');Check ($full.reply.ok-and$full.on_ms-ge1100-and$full.on_ms-le1300) "$arch actual full-lock positive control integrates near1200ms"
  $prior=-1;$periodMs=320;$expectedCycles=12;$durationMs=$periodMs*$expectedCycles
  foreach($duty in @(0,25,50,75,100)){$m=MeasureSteering @('pulse','d',"$durationMs",'--period-ms',"$periodMs",'--duty',"$duty");$polls=@([regex]::Matches($m.samples,'(\d+) POLL d=(\d)')|ForEach-Object{@{at_ms=[long]$_.Groups[1].Value;down=$_.Groups[2].Value-eq'1'}})
   # Store every actual poll and each complete observed rise-to-rise cycle
   # before any assertion, so failures retain exactly what the target read.
   $m.pwm=Measure-ViPwmPolls -Polls $polls -Duty $duty -PeriodMs $periodMs -ExpectedCycles $expectedCycles;$fraction=$m.pwm.fraction;$m.legacy_on_time_fraction=$m.on_ms/[double]$durationMs;$m.pwm.ended_neutral=$polls.Count-gt0-and-not$polls[-1].down
   Check ($m.reply.ok-and$m.elapsed_ms-ge$durationMs-and$m.samples-notmatch'polls_clipped=1'-and$m.pwm.pass-and$m.pwm.ended_neutral-and$fraction-gt$prior) "$arch PWM duty$duty target-poll proportion=$([math]::Round($fraction,3)), tolerance=$([math]::Round($m.pwm.tolerance,3)), complete cycles=$($m.pwm.complete_cycles)"
   if($duty-eq25){Check ($fraction-lt0.4) "$arch quarter steering is measured below full lock"};$prior=$fraction
  }
  foreach($direction in @(1,-1)){$from=-16000*$direction;$to=16000*$direction;$m=MeasureSteering @('pad-ramp','lx',"$from","$to",'1000','--pacing','time','--ly','1234','--end-hold-ms','300');$points=@([regex]::Matches($m.samples,'(\d+) OBS d=\d lx=(-?\d+) ly=(-?\d+)')|ForEach-Object{@{at_ms=[long]$_.Groups[1].Value;lx=[int]$_.Groups[2].Value;ly=[int]$_.Groups[3].Value}}|Where-Object{$_.ly-eq1234});$m.observed_points=$points;$m.legacy_endpoint_rule_pass=$points.Count-ge10-and$points[0].lx-eq$from-and$points[-1].lx-eq$to;$m.sample_delay_ms=$SampleDelayMs;$m.expected_from=$from;$m.expected_to=$to;$m.endpoint_hold_ms=300;$monotonic=$points.Count-ge10
   for($i=1;$i-lt$points.Count;$i++){if(($points[$i].lx-$points[$i-1].lx)*$direction-lt0){$monotonic=$false}}
   # A polling reader may miss the zero-duration first endpoint. Validate the
   # observed path's range/span and the held final endpoint instead.
   $rangeOk=@($points|Where-Object{$_.lx-lt-16000-or$_.lx-gt16000}).Count-eq0
   $span=if($points.Count){($points[-1].lx-$points[0].lx)*$direction}else{0}
   $intercepts=@($points|Where-Object{$_.lx-ne$to}|ForEach-Object{$_.at_ms-1000.0*($_.lx-$from)/($to-$from)})
   $spread=if($intercepts.Count){($intercepts|Measure-Object -Maximum).Maximum-($intercepts|Measure-Object -Minimum).Minimum}else{[double]::PositiveInfinity}
   $m.observed_span=$span;$m.monotonic=$monotonic;$m.range_ok=$rangeOk;$m.linear_time_spread_ms=$spread
   Check ($m.reply.ok-and$monotonic-and$rangeOk-and$span-ge28800-and$spread-le100-and$points[-1].lx-eq$to) "$arch ramp$direction actual XInput points follow a linear trajectory, span at least90% of the range and reach the held endpoint"
   Check ([math]::Abs($m.lx_area-300*$to)-le650000) "$arch ramp$direction integrates a linear trajectory plus endpoint dwell (area=$($m.lx_area))"
  }
  Stop-ViOwnedProcess $p
 }
}finally{foreach($p in $owned){Stop-ViOwnedProcess $p};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 10|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
