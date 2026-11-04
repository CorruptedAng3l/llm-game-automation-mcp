# Mixed A/W fixture-only observation. Terminal lower spies; root runs native.
param([string]$RepoRoot='', [string]$CliDir='', [string]$Out="$PSScriptRoot\..\build\test\wndproc_aw")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
if(-not$RepoRoot){$d=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($n=0;$n-lt9-and$d;$n++){if(Test-Path (Join-Path $d.FullName 'build\x64\vimh_hook.obj')){$RepoRoot=$d.FullName;break};$d=$d.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot with tested VIMH objects'};$RepoRoot=(Resolve-Path $RepoRoot).Path;if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=(Resolve-Path $CliDir).Path
if(-not('ViWndProcAwNative'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_wndprocaw_native.cs"}
$source=(Resolve-Path "$PSScriptRoot\..\test\vi_wndprocaw.cpp").Path;$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Receipts must remain in private repository'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force "$run\tools","$run\calls"|Out-Null
. "$PSScriptRoot\vi_wndprocaw_identity.ps1"
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$fixtures=[Collections.Generic.List[object]]::new();$owned=$null;$token=1000;$complete=$false;$exception=$null
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name"}
function Require([bool]$Good,[string]$Name){Check $Good $Name;if(-not$Good){throw $Name}}
function Text($Path){if(-not(Test-Path $Path)){return ''};$s=[IO.FileStream]::new($Path,'Open','Read',([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));$r=[IO.StreamReader]::new($s);try{return $r.ReadToEnd()}finally{$r.Dispose();$s.Dispose()}}
function Rows($T){foreach($line in ((Text $T.log)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Alive($T){
 if(-not$T-or-not$T.process-or$T.process.HasExited-or-not$T.created-or-not$T.retained_handle){return $false}
 $T.latest_lifecycle_identity=ReadRetainedImage $T.process $T.exe $T.created $T.retained_handle
 return [bool]($T.latest_lifecycle_identity.matched-and-not$T.process.HasExited)
}
function Reap($P,[string]$Expected,$Created,[long]$Retained){
 $r=@{reaped=$false;identity_verified=$false;exit_known=$false;native_exit_code=$null;error=$null;deadline_ms=1500;termination_attempted=$false;identity_source='retained handle PID + creation + QueryFullProcessImageNameW'}
 try{
  $r.identity=ReadRetainedImage $P $Expected $Created $Retained
  if($P.HasExited){$r.reaped=$true;$r.identity_verified=$r.identity.primary_identity_match}else{
   if(-not$r.identity.matched){throw ('Retained kernel ownership unverified; termination refused: '+$r.identity.reason)}
   $r.identity_verified=$true
   if(-not$P.HasExited){$r.termination_attempted=$true;$P.Kill()};$r.reaped=$P.WaitForExit(1500)
  }
 }catch{$r.error=$_.Exception.Message}
 finally{try{if($P.HasExited){$r.reaped=$true;$r.exit_known=$true;$r.native_exit_code=$P.ExitCode}}catch{if(-not$r.error){$r.error=$_.Exception.Message}}}
 return $r
}
function Run([string]$Exe,[string[]]$Arguments,[int]$Budget=8000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Exe;$info.WorkingDirectory=$run;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;[void]$info.Environment.Remove('VINPUT_OWNER');$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;foreach($a in $Arguments){[void]$info.ArgumentList.Add([string]$a)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;$outTail=$null;$errTail=$null;$watch=[Diagnostics.Stopwatch]::StartNew();$started=$false;$actualPath=$null;$created=$null
 $r=@{exe=$Exe;pid=$null;actual_path=$null;creation_utc_ticks=$null;retained_handle=$null;exit=$null;exit_known=$false;timeout=$false;owned_reaped=$false;kill_error=$null;stdout='';stderr='';stdout_complete=$false;stderr_complete=$false;stdout_available=$false;stderr_available=$false;stdout_truncated=$false;stderr_truncated=$false;stdout_error=$null;stderr_error=$null;error=$null}
 try{
  if(-not$p.Start()){throw 'Owned process start failed'};$started=$true;$r.pid=$p.Id;$r.retained_handle=$p.Handle.ToInt64()
  $created=$p.StartTime.ToUniversalTime().Ticks;$r.creation_utc_ticks=$created;try{$r.dotnet_start_path=$p.Path}catch{$r.dotnet_start_path_error=$_.Exception.Message};$r.expected_path=$Exe
  $outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError)
  $r.timeout=-not$p.WaitForExit($Budget)
 }catch{$r.error=$_.Exception.Message}
 finally{
  if($started){$r.cleanup=Reap $p $Exe $created $r.retained_handle;if($r.cleanup.identity.primary_identity_match){$r.actual_path=$r.cleanup.identity.kernel_path};$r.owned_reaped=$r.cleanup.reaped;$r.kill_error=$r.cleanup.error;$r.exit_known=$r.cleanup.exit_known;$r.exit=$r.cleanup.native_exit_code}
  if($outTail){$r.stdout_available=$outTail.Wait(400);$r.stdout=$outTail.Text();$r.stdout_complete=$outTail.Complete;$r.stdout_truncated=$outTail.Characters-gt65536;$r.stdout_error=$outTail.Error}
  if($errTail){$r.stderr_available=$errTail.Wait(400);$r.stderr=$errTail.Text();$r.stderr_complete=$errTail.Complete;$r.stderr_truncated=$errTail.Characters-gt65536;$r.stderr_error=$errTail.Error}
  if($outTail-and-not$outTail.Complete){try{$p.StandardOutput.Close()}catch{}};if($errTail-and-not$errTail.Complete){try{$p.StandardError.Close()}catch{}}
  $watch.Stop();$r.elapsed_ms=$watch.ElapsedMilliseconds;$p.Dispose()
 }
 return $r
}

function Call($T,[string[]]$Arguments,[int]$Budget=8000){Require (Alive $T) "$($T.name) retained process before CLI";$r=Run $T.cli (@('--json','--pid',[string]$T.process.Id)+$Arguments) $Budget;$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}};$r.reply=$reply;$r.arguments=$Arguments;$calls.Add($r);$r|ConvertTo-Json -Depth 25|Set-Content "$run\calls\$($calls.Count).json" -Encoding utf8NoBOM;Require (-not$r.timeout-and$r.owned_reaped-and$r.exit_known-and$r.stdout_complete-and$r.stderr_complete-and-not$r.stdout_truncated-and$lines.Count-eq1-and$null-ne$reply) "$($T.name) bounded CLI reply";return $r}
function Await($T,[string]$Event,[long]$Token=0){$end=[Environment]::TickCount64+4000;do{foreach($r in @(Rows $T)){if($r.event-eq$Event-and($Token-eq0-or$r.token-eq$Token)){return $r}};if(-not(Alive $T)){throw 'Owned fixture exited'};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end);throw "Missing $Event token$Token"}
function Control($T,[uint32]$Message,[int]$Which,[string]$Event,[bool]$Flag=$false){Require (Alive $T) "$($T.name) lifetime before control";[uint32]$owner=0;$thread=[ViWndProcAwNative]::GetWindowThreadProcessId($T.control,[ref]$owner);Require ($owner-eq$T.process.Id-and$thread-eq$T.thread-and[ViWndProcAwNative]::IsWindow($T.control)) "$($T.name) exact control HWND owner/thread";$script:token++;$w=[uint64]$Which;if($Flag){$w=$w-bor256};if(-not[ViWndProcAwNative]::PostMessageW($T.control,$Message,[UIntPtr]::new($w),[IntPtr]::new([long]$script:token))){throw 'Owned control post failed'};return Await $T $Event $script:token}
function Snap($T,[int]$Which){return Control $T 0x8090 $Which snapshot}
function Foreground{[uint32]$owner=0;$h=[ViWndProcAwNative]::GetForegroundWindow();[void][ViWndProcAwNative]::GetWindowThreadProcessId($h,[ref]$owner);return @{hwnd=$h.ToInt64();pid=$owner;read_only=$true}}
function Adopt($T,[int]$Which){$h=if($Which-eq1){$T.a}else{$T.w};$r=Call $T @('config',('hwnd=0x{0:X}'-f[uint64]$h),'autoactivate=false','isolate=true','msgs=on','raw=off','fakefocus=true');Require ($r.exit-eq0-and$r.reply.ok) "$($T.name) adopt exact window$Which";return $r}
function Probe($T,[int]$Which,[string]$Stage){foreach($sendW in @($false,$true)){$before=Snap $T $Which;$r=Control $T 0x8093 $Which text_probe $sendW;$after=Await $T text_snapshot $script:token;Check ($r.sent-and$r.output_matches-and$after.original_text-eq$before.original_text+1-and$after.text_matches-eq$before.text_matches+1-and$after.cycles-eq$before.cycles) "$($T.name) $Stage window$Which sendW$sendW preserves original A/W sentinel text";$cases.Add(@{name=$Stage;which=$Which;send_unicode=$sendW;before=$before;probe=$r;after=$after})};$p=Control $T 0x8095 $Which private_probe;Check ($p.sent-and$p.result-eq$(if($Which-eq1){0xA01}else{0xB02})) "$($T.name) $Stage private message reaches the matching original"}
function StopOwned($T){if(-not$T-or-not$T.process-or$T.stop){return};$T.stop=Reap $T.process $T.exe $T.created $T.retained_handle;Check ([bool]$T.stop.reaped) "$($T.name) bounded exact owned fixture cleanup";if($T.stop.reaped){$T.process.Dispose();$T.process=$null}}
try{foreach($arch in @('x64','x86')){
 $suffix=if($arch-eq'x64'){''}else{'32'};$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$folder="$run\tools\$arch";New-Item -ItemType Directory -Force "$folder\temp"|Out-Null
 $batch=@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" /I"$RepoRoot\test" /I"$RepoRoot\third_party\minhook\include" "$source" /Fo:"$folder\fixture.obj" /Fe:"$folder\fixture.exe" /link /SUBSYSTEM:WINDOWS "$RepoRoot\build\$arch\vimh_buffer.obj" "$RepoRoot\build\$arch\vimh_hook.obj" "$RepoRoot\build\$arch\vimh_trampoline.obj" "$RepoRoot\build\$arch\vimh_hde.obj"
exit /b %errorlevel%
"@;[IO.File]::WriteAllText("$folder\build.cmd",$batch,[Text.Encoding]::ASCII);$build=Run $env:ComSpec @('/d','/c',"$folder\build.cmd") 30000;$build|ConvertTo-Json -Depth 8|Set-Content "$folder\build.json";Require ($build.exit-eq0-and-not$build.timeout-and$build.stdout_complete-and$build.stderr_complete-and($build.stdout+$build.stderr)-notmatch'(?im)\bwarning (C|LNK)\d+') "$arch fixture W4 WX zero warnings"
 $exe=(Resolve-Path "$folder\fixture.exe").Path;$log="$run\$arch.fixture.jsonl";$start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$exe;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;[void]$start.ArgumentList.Add($log);$p=[Diagnostics.Process]::Start($start);$owned=@{name=$arch;process=$p;pid=$p.Id;exe=$exe;created=$null;retained_handle=$null;stop=$null;log=$log;cli=(Join-Path $CliDir "vinput$suffix.exe")};$fixtures.Add($owned);$owned.retained_handle=$p.Handle.ToInt64();$owned.created=$p.StartTime.ToUniversalTime().Ticks;$owned.startup_identity=ObserveStartupImage $p $exe $owned.created $owned.retained_handle;$owned.actual_path=$owned.startup_identity.actual_path;Require ($owned.startup_identity.matched-and$owned.actual_path-eq$exe) "$arch fixture retained kernel identity and exact image"
 try{$ready=Await $owned ready;Require ($ready.pid-eq$p.Id-and$ready.safe_spies-and$ready.native_write_trampolines_called-eq$false) "$arch terminal lower write spies ready before injection";$owned.thread=[uint32]$ready.thread;$owned.control=[IntPtr]::new([long][uint64]$ready.control);$owned.a=[uint64]$ready.ansi;$owned.w=[uint64]$ready.unicode;Require ((Foreground).pid-ne$p.Id) "$arch actual foreground starts outside fixture";$r=Call $owned @('inject','--hooks-only') 15000;Require ($r.exit-eq0-and$r.reply.ok) "$arch hook handshake"
  foreach($which in @(1,2)){
   $other=if($which-eq1){2}else{1};[void](Adopt $owned $which);$before=Snap $owned $which;Probe $owned $which before_outer
   $installed=Control $owned 0x8091 $which installed;Require $installed.ok "$arch legitimate opposite-A/W outer installed";$installedSnap=Await $owned installed_snapshot $script:token;Probe $owned $which mixed_outer
   $readyReply=Call $owned @('readiness','--wait-ms','250');Check ($readyReply.exit-eq0-and$readyReply.reply.ok-and$readyReply.reply.window_ready-and$readyReply.reply.queue_ready) "$arch window$which mixed outer preserves actual queued readiness"
   $beforeClick=Snap $owned $which;$click=Call $owned @('click','left','50','60','--hover-timeout-ms','250','--settle-ms','0','--ms','1','--di-poll-timeout-ms','0');$afterClick=Snap $owned $which;Check ($click.exit-eq0-and$click.reply.ok-and$click.reply.hover_unconfirmed-and$click.reply.queue_progress_confirmed-and$afterClick.outer_downs-eq$beforeClick.outer_downs+1) "$arch window$which real mixed outer consumes hover and qualifies through actual queued barrier"
   [void](Adopt $owned $other);Probe $owned $which retained_mixed_old_window;Probe $owned $other current_other
   $target=if($which-eq1){$owned.a}else{$owned.w};$refused=Call $owned @('config',('hwnd=0x{0:X}'-f$target));Check ($refused.reply.ok-eq$false-and$refused.reply.adoption_refused-eq$true-and$refused.reply.target_changed-eq$false-and$refused.reply.adoption_reason-eq'retained_opaque_outer_chain') "$arch window$which retained opaque chain readoption remains explicitly refused"
   $removed=Control $owned 0x8092 $which removed;Require $removed.ok "$arch window$which outer removed through its exact saved CallProc token";$removedSnap=Await $owned removed_snapshot $script:token;Probe $owned $which restored_inner
   [void](Adopt $owned $which);$status=Call $owned @('status');Check ($status.reply.ok-and$status.reply.subclassed-eq$true) "$arch window$which restored own inner is identified as own";$filter=Control $owned 0x8094 $which filter $true;Require $filter.enabled "$arch loop consumes restored-own hover moves";$ownBefore=Snap $owned $which;$negative=Call $owned @('click','left','70','80','--hover-timeout-ms','100','--settle-ms','0','--ms','1','--di-poll-timeout-ms','0');$ownAfter=Snap $owned $which;Check ($negative.exit-ne0-and$negative.reply.ok-eq$false-and$ownAfter.original_downs-eq$ownBefore.original_downs-and$ownAfter.outer_downs-eq$ownBefore.outer_downs-and$ownAfter.loop_moves-gt$ownBefore.loop_moves) "$arch window$which restored own queued barrier cannot qualify loop-consumed hover as an external overlay";[void](Control $owned 0x8094 $which filter $false)
   $cases.Add(@{arch=$arch;which=$which;initial=$before;installed=$installedSnap;removed=$removedSnap;readiness=$readyReply;outer_click=$click;opaque_refusal=$refused;restored_status=$status.reply;own_negative=$negative;own_before=$ownBefore;own_after=$ownAfter;observed_current_unicode_changed=($before.current_is_unicode-ne$installedSnap.current_is_unicode);native_thunk_tokens_not_interpreted=$true})
  };Require ((Foreground).pid-ne$p.Id) "$arch actual foreground remains outside fixture"
 }finally{StopOwned $owned;$owned=$null}
};$complete=$true}catch{$exception=$_.Exception.Message;Check $false ('runner incomplete: '+$exception)}finally{if($owned){StopOwned $owned};$failed=@($checks|Where-Object {-not$_.pass}).Count;@{schema=1;completed=$complete;status=$(if($failed-or-not$complete){'failed'}else{'passed'});failed_check_count=$failed;positive_case_count=$checks.Count-$failed;checks=$checks;cases=$cases;calls=$calls;fixtures=@($fixtures|ForEach-Object {$v=@{};foreach($k in $_.Keys){if($k-ne'process'){$v[$k]=$_[$k]}};$v});exception=$exception;actual_os_foreground_handoff_measured=$false;physical_input_generated=$false;native_write_trampolines_called=$false;tool_issues=@()}|ConvertTo-Json -Depth 40|Set-Content "$run\result.json" -Encoding utf8NoBOM;Write-Host "$($checks.Count-$failed) PASS / $failed FAIL ($run)"}
if($failed-or-not$complete){exit 1};exit 0
