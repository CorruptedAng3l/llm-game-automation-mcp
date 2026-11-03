param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\input_jobs\hold_maintained\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path;New-Item -ItemType Directory -Force "$Out\tools\temp"|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";$owned=[Collections.Generic.List[Diagnostics.Process]]::new();$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new()
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
function Check($Good,$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});"$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good-and-not$KeepGoing){throw $Name}}
function Text($T){Get-Content $T.log -Raw -ErrorAction SilentlyContinue}
function WaitState($T,$Offset,$State,$Ms=3000){$end=[Environment]::TickCount64+$Ms;do{$log=Text $T;if($log-and$log.Length-ge$Offset-and$log.Substring($Offset)-match"OBS w=$State "){return};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);throw "actual W$State not observed"}
function Call($T,[string[]]$Command){$cli=$T.cli;$raw=@(& $cli --json --pid $T.pid @Command);$code=$LASTEXITCODE;$r=@{command=$Command;raw=$raw;exit=$code;reply=($raw|ConvertFrom-Json)};$records.Add($r);return $r}
function StartObserver($T,$Text,$Timeout,$Tag){$path="$Out\$Tag$($T.pid).json";$offset=(Text $T).Length;$p=Start-Process $T.cli -ArgumentList "--json --pid $($T.pid) hold-while w --until-text `"$Text`" --timeout-ms $Timeout" -RedirectStandardOutput $path -WindowStyle Hidden -PassThru;$owned.Add($p);WaitState $T $offset 1;return @{process=$p;path=$path;offset=$offset}}
function FinishObserver($O){if(-not$O.process.WaitForExit(8000)){throw 'observer did not exit'};$raw=Get-Content $O.path -Raw;$r=@{observer_pid=$O.process.Id;raw=$raw;exit=$O.process.ExitCode;reply=($raw|ConvertFrom-Json)};$records.Add($r);return $r}
try{
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_jobconsumer.cpp" /Fo:"$tools\consumer$suffix.obj" /Fd:"$tools\consumer$suffix.pdb" /Fe:"$tools\consumer$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture /W4 /WX"}
 foreach($suffix in @('','32')){
  $log="$Out\target$suffix.log";$p=Start-Process "$tools\consumer$suffix.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned.Add($p);$injected=& $V --json inject --pid $p.Id|ConvertFrom-Json;if(-not$injected.ok){throw 'owned target injection failed'};$t=@{pid=$p.Id;log=$log;cli=(Join-Path $CliDir $(if($suffix){'vinput32.exe'}else{'vinput.exe'}))};$status=Call $t @('status');if($status.reply.really_foreground){throw 'background prerequisite failed'}
  $o=StartObserver $t 'HELD W' 3000 'normal';$normal=FinishObserver $o;WaitState $t $o.offset 0
  Check ($normal.reply.ok-and$normal.reply.expectation_met-and$normal.reply.cleanup_confirmed-and$normal.reply.hold_maintained) "$suffix normal hold remains active through observation then cancels"
  $o=StartObserver $t 'IDLE' 3000 'released';$released=Call $t @('release');WaitState $t $o.offset 0;$result=FinishObserver $o
  Check ($released.reply.ok-and-not$result.reply.ok-and$result.exit-ne0-and$result.reply.expectation_met-and-not$result.reply.hold_maintained-and$result.reply.cleanup_confirmed) "$suffix externally canceled hold cannot succeed merely because later IDLE text matches"
  $o=StartObserver $t 'ABSENT' 500 'crashed';$identity=Get-Process -Id $o.process.Id;if($identity.Path-ne(Resolve-Path $t.cli).Path){throw 'observer PID identity changed'};Stop-Process -Id $o.process.Id -Force;WaitState $t $o.offset 0 3000;$jobs=(Call $t @('jobs')).reply.jobs;$last=$jobs[-1]
  $records.Add(@{observer_terminated=$o.process.Id;observer_path=(Resolve-Path $t.cli).Path;deadline_job=$last})
  Check ($last.job_complete-and$last.job_state-eq'completed'-and$last.requested_ms-eq1500) "$suffix target hard deadline releases actual W after owned observer termination"
  Stop-Process -Id $t.pid -Force
 }
}finally{foreach($p in $owned){if(-not$p.HasExited){Stop-Process -Id $p.Id -Force}};@{checks=$checks;records=$records}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$fail=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$fail) PASS / $fail FAIL";if($fail){exit 1};$global:LASTEXITCODE=0
