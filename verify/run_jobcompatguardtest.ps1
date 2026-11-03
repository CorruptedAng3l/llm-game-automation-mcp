param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\input_jobs\job_support\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;New-Item -ItemType Directory -Force "$Out\tools\temp"|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";$checks=@();$records=@();$owned=@()
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
function Check($Ok,$Name){$script:checks+=@{pass=[bool]$Ok;name=$Name};"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Call($T,[string[]]$ArgsList){$raw=@(& $T.cli --json --pid $T.pid @ArgsList);$code=$LASTEXITCODE;$r=@{args=$ArgsList;raw=$raw;exit=$code;reply=($raw|ConvertFrom-Json)};$script:records+=$r;return $r}
function State($T){$lines=@((Get-Content $T.log -Raw)-split'\r?\n'|Where-Object{$_-match' OBS '});if(-not$lines.Count){return ''};return $lines[-1]}
try{
 foreach($arch in @('x64','x86')){$suffix=if($arch-eq'x64'){''}else{'32'};$vc=if($suffix){'vcvars32.bat'}else{'vcvars64.bat'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_jobconsumer.cpp" /Fo:"$tools\consumer$suffix.obj" /Fd:"$tools\consumer$suffix.pdb" /Fe:"$tools\consumer$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture /W4 /WX"}
 foreach($suffix in @('','32')){
  $cli=(Resolve-Path "$CliDir\vinput$suffix.exe").Path;$log="$Out\target$suffix.log";$process=Start-Process "$tools\consumer$suffix.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned+=$process;$injected=& $cli --json inject --pid $process.Id|ConvertFrom-Json;if(-not$injected.ok){throw 'injection failed'};$t=@{pid=$process.Id;cli=$cli;log=$log};$s=Call $t @('status');if($s.reply.really_foreground){throw 'owned target not background'}
  $before=(Call $t @('trace','key','w')).reply;$r=Call $t @('key','w','120','--async');$after=(Call $t @('trace','key','w')).reply
  Check (-not$r.reply.ok-and$r.exit-ne0-and$r.reply.input_injected-eq$false-and$after.injected_down-eq$before.injected_down) "$suffix async key on old DLL rejected before any down"
  $r=Call $t @('pad','--lx','9000','--hold','--async');Start-Sleep -Milliseconds 40;$state=State $t;$off=Call $t @('pad','off','--async')
  Check (-not$r.reply.ok-and$r.reply.input_injected-eq$false-and$state-notmatch'lx=9000') "$suffix unsupported async indefinite pad cannot become stuck"
  Check ($off.reply.ok) "$suffix pad off cleanup bypasses missing jobs support"
  $before=(Call $t @('trace','key','w')).reply;$r=Call $t @('hold-while','w','--until-text','HELD W','--timeout-ms','500');$after=(Call $t @('trace','key','w')).reply
  Check (-not$r.reply.ok-and$r.reply.input_injected-eq$false-and$after.injected_down-eq$before.injected_down) "$suffix hold-while probes support before original key"
  Call $t @('down','w')|Out-Null;$up=Call $t @('json','{ "cmd" : "up", "key":"w", "async":true }');$trace=(Call $t @('trace','key','w')).reply
  Check ($up.reply.ok-and-not$trace.virtual_down) "$suffix up cleanup bypasses support probe even raw JSON"
  $before=(Call $t @('trace','key','w')).reply;$r=Call $t @('key','w','120');$after=(Call $t @('trace','key','w')).reply
  Check ($r.reply.ok-and$after.injected_down-eq$before.injected_down+1-and-not$after.virtual_down) "$suffix ordinary flagless timed key retains old-DLL behavior"
  $r=Call $t @('json','{"cmd":"p\u0061d","buttons":"none","lx":12345,"hold":true,"as\u0079nc":true}');Start-Sleep -Milliseconds 30;$state=State $t;$off=Call $t @('json','{"cmd":"p\u0061d","buttons":"o\u0066f","async":true}')
  Check (-not$r.reply.ok-and$r.reply.input_injected-eq$false-and$state-notmatch'lx=12345') "$suffix escaped ASCII command and feature keys cannot bypass support gate"
  Check ($off.reply.ok) "$suffix escaped pad off still bypasses support probe"
  Call $t @('mousedown','left')|Out-Null;$up=Call $t @('mouseup','left','--async');$trace=(Call $t @('trace','key','lbutton')).reply
  Check ($up.reply.ok-and-not$trace.virtual_down) "$suffix mouseup bypasses old-DLL support probe"
  Call $t @('down','w')|Out-Null;$release=Call $t @('release','--replace');$trace=(Call $t @('trace','key','w')).reply
  Check ($release.reply.ok-and-not$trace.virtual_down) "$suffix release bypasses old-DLL support probe"
  Stop-Process -Id $process.Id -Force
 }
}finally{foreach($p in $owned){if(-not$p.HasExited){Stop-Process -Id $p.Id -Force}};@{checks=$checks;records=$records}|ConvertTo-Json -Depth 14|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$fail=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$fail) PASS / $fail FAIL";if($fail){exit 1};$global:LASTEXITCODE=0
