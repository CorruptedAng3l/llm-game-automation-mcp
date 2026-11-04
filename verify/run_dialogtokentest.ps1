# Virgin system dialog observer; own inject/readiness only, never launch/early/activation.
param([string]$RepoRoot='', [string]$CliDir='', [string]$ObserverDir='', [string]$Out="$PSScriptRoot\..\build\test\dialog_token",[switch]$NativeOnlyControl,[switch]$RequireX86RollbackWitness)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
if(-not$RepoRoot){$RepoRoot=(Resolve-Path "$PSScriptRoot\..").Path};$RepoRoot=(Resolve-Path $RepoRoot).Path
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=(Resolve-Path $CliDir).Path
$source=(Resolve-Path "$PSScriptRoot\..\test\vi_dialogtoken.cpp").Path;$outRoot=[IO.Path]::GetFullPath($Out)
if(-not$outRoot.StartsWith($RepoRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Receipts must remain in private root'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force "$run\tools","$run\calls"|Out-Null
if(-not('ViDialogTokenNative'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_dialogtoken_native.cs"}
. "$PSScriptRoot\vi_dialogtoken_identity.ps1"
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
  $outTail=[ViDialogTokenTail]::new($p.StandardOutput);$errTail=[ViDialogTokenTail]::new($p.StandardError)
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
function Foreground{[uint32]$owner=0;$h=[ViDialogTokenNative]::GetForegroundWindow();[void][ViDialogTokenNative]::GetWindowThreadProcessId($h,[ref]$owner);return @{hwnd=$h.ToInt64();pid=$owner;read_only=$true}}
function StopOwned($T){if(-not$T-or-not$T.process-or$T.stop){return};$T.stop=Reap $T.process $T.exe $T.created $T.retained_handle;Check ([bool]$T.stop.reaped) "$($T.name) bounded exact owned fixture cleanup";if($T.stop.reaped){$T.process.Dispose();$T.process=$null}}
function Control($T,[uint32]$Message,[string]$Event){
 Require (Alive $T) "$($T.name) retained identity before control";[uint32]$owner=0;$thread=[ViDialogTokenNative]::GetWindowThreadProcessId($T.control,[ref]$owner)
 Require ($owner-eq$T.process.Id-and$thread-eq$T.thread-and[ViDialogTokenNative]::IsWindow($T.control)) "$($T.name) exact owned control HWND/PID/thread"
 Require (Alive $T) "$($T.name) retained identity immediately before post";$script:token++
 if(-not[ViDialogTokenNative]::PostMessageW($T.control,$Message,[UIntPtr]::Zero,[IntPtr]::new([long]$script:token))){throw 'Owned control post failed'}
 return Await $T $Event $script:token
}
function Snap($T){Control $T 0x8090 snapshot}
try{foreach($arch in @('x64','x86')){
 $suffix=if($arch-eq'x64'){''}else{'32'};$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$folder="$run\tools\$arch";New-Item -ItemType Directory -Force "$folder\temp"|Out-Null
 if($ObserverDir){$exe=(Resolve-Path (Join-Path $ObserverDir "$arch\observer.exe")).Path}else{
 $batch=@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" /I"$RepoRoot\test" /I"$RepoRoot\third_party\minhook\include" "$source" /Fo:"$folder\observer.obj" /Fe:"$folder\observer.exe" /link /SUBSYSTEM:WINDOWS "$RepoRoot\build\$arch\vimh_buffer.obj" "$RepoRoot\build\$arch\vimh_hook.obj" "$RepoRoot\build\$arch\vimh_trampoline.obj" "$RepoRoot\build\$arch\vimh_hde.obj"
exit /b %errorlevel%
"@
 [IO.File]::WriteAllText("$folder\build.cmd",$batch,[Text.Encoding]::ASCII);$build=Run $env:ComSpec @('/d','/c',"$folder\build.cmd") 30000;$build|ConvertTo-Json -Depth 10|Set-Content "$folder\build.json"
 Require ($build.exit-eq0-and-not$build.timeout-and$build.stdout_complete-and$build.stderr_complete-and($build.stdout+$build.stderr)-notmatch'(?im)\bwarning (C|LNK)\d+') "$arch observer W4 WX zero warnings"
 $exe=(Resolve-Path "$folder\observer.exe").Path
 }
 foreach($encoding in @('a','w')){
  $mode=if($NativeOnlyControl){'native'}else{'product'};$name="$arch.$encoding.$mode";$log="$run\$name.jsonl"
  $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$exe;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  foreach($arg in @($log,$encoding,$mode)){[void]$start.ArgumentList.Add($arg)}
  $p=[Diagnostics.Process]::Start($start);$owned=@{name=$name;process=$p;pid=$p.Id;exe=$exe;created=$null;retained_handle=$null;stop=$null;log=$log;cli=(Join-Path $CliDir "vinput$suffix.exe")};$fixtures.Add($owned)
  $owned.retained_handle=$p.Handle.ToInt64();$owned.created=$p.StartTime.ToUniversalTime().Ticks;$owned.startup_identity=ObserveStartupImage $p $exe $owned.created $owned.retained_handle;$owned.actual_path=$owned.startup_identity.actual_path
  Require ($owned.startup_identity.matched-and$owned.actual_path-eq$exe) "$name strict retained kernel image/creation identity"
  try{
   $ready=Await $owned ready;Require ($ready.pid-eq$p.Id-and$ready.terminal_spies-and$ready.native_write_trampolines_called-eq$false-and$ready.no_fixture_setter_before_ready-and$ready.native_only-eq[bool]$NativeOnlyControl) "$name terminal guards and virgin phase verified before input infrastructure"
   $owned.thread=[uint32]$ready.thread;$owned.control=[IntPtr]::new([long][uint64]$ready.control);$owned.dialog=[uint64]$ready.hwnd
   Require ((Foreground).pid-ne$p.Id) "$name actual foreground outside observer";$virgin=Rows $owned|Where-Object {$_.event-eq'snapshot'-and$_.stage-eq'virgin_before_product'}|Select-Object -First 1
   Require ($virgin-and$virgin.class_is_dialog-and$virgin.requested_unicode-eq($encoding-eq'w')) "$name actual system #32770 with recorded raw A/W/class/dialog tokens"
   $beforeSet=@(Rows $owned|Where-Object event -eq native_set);Require ($beforeSet.Count-eq0) "$name main procedure has no observed public setter before product/native-only phase"
   $record=@{name=$name;arch=$arch;encoding=$encoding;phase=$mode;ready=$ready;virgin=$virgin;observer_sha256=(Get-FileHash $exe -Algorithm SHA256).Hash;cli_sha256=(Get-FileHash $owned.cli -Algorithm SHA256).Hash;input_actions_requested=$false;token_representation_cause_proven=$false}
   if($NativeOnlyControl){
    $record.installed=Control $owned 0x8092 native_only_installed;Check $record.installed.ok "$name native-only separate-process setter installed"
    $record.text=Control $owned 0x8091 text;Check ($record.text.ok-and$record.text.caption_matches) "$name authoritative native-returned token forwards caption through CallWindowProc"
    $record.removed=Control $owned 0x8093 native_only_removed;Check $record.removed.ok "$name native-only hook removed using exact native-returned token"
   }else{
    $record.inject=Call $owned @('inject','--hooks-only') 15000;Require ($record.inject.exit-eq0-and$record.inject.reply.ok) "$name hooks-only injection transport/guard handshake"
    # Let the ordinary monitor retry a refused fresh attach; do not normalize
    # the system window or rescue the result through a manual config/reattach.
    $record.manual_config_issued=$false
    $record.readiness=Call $owned @('readiness','--wait-ms','2000')
    Check ($record.readiness.exit-eq0-and$record.readiness.reply.ok-and$record.readiness.reply.window_ready-and$record.readiness.reply.queue_ready-and[Convert]::ToUInt64($record.readiness.reply.hwnd,16)-eq$owned.dialog) "$name full readiness is established for the exact system window"
    $record.status=Call $owned @('status');Require ($record.status.exit-eq0-and$record.status.reply.ok) "$name status diagnostic transport succeeds"
    Check ($record.status.reply.wndproc_binding_ready-eq$true-and$record.status.reply.subclassed-eq$true-and[Convert]::ToUInt64($record.status.reply.hwnd,16)-eq$owned.dialog) "$name automatic adoption has a live binding and known own topmost procedure"
    $record.text=Control $owned 0x8091 text;Check ($record.text.ok-and$record.text.caption_matches) "$name system caption callback remains callable"
   }
   $record.final=Snap $owned;$all=@(Rows $owned);$sets=@($all|Where-Object {$_.event-eq'native_set'-and$_.hwnd-eq$owned.dialog})
   Check ($sets.Count-ge1-and@($sets|Where-Object {$_.native_result_returned_unchanged-ne$true}).Count-eq0) "$name actual native setter predecessor and before/after getter evidence recorded"
   $record.native_setters=$sets;$record.getters=@($all|Where-Object event -eq native_get);$record.raw_log=Text $log
   $record.native_previous_differs_from_matching_before=@($sets|Where-Object {($_.api-eq'SetA'-and$_.previous-ne$_.before_a)-or($_.api-eq'SetW'-and$_.previous-ne$_.before_w)})
   $witness=@{required=([bool]$RequireX86RollbackWitness-and$arch-eq'x86'-and-not$NativeOnlyControl);mismatch_observed=$false;rollback_observed=$false;retry_observed=$false;setter_index=$null;scope='raw native setter/getter sequence only; opaque tokens are not decoded'}
   if(-not$NativeOnlyControl){
    for($i=0;$i-lt$sets.Count;$i++){
     $first=$sets[$i];$which=if($first.api-eq'SetA'){'a'}elseif($first.api-eq'SetW'){'w'}else{''}
     if(-not$which){continue};$sampled=$first.('before_'+$which)
     if($first.previous-eq$sampled){continue}
     $witness.mismatch_observed=$true;$witness.setter_index=$i;$witness.first=$first
     for($j=$i+1;$j-lt$sets.Count;$j++){
      $rollback=$sets[$j]
      if($rollback.api-ne$first.api-or$rollback.replacement-ne$first.previous-or$rollback.previous-ne$first.replacement-or$rollback.('after_'+$which)-ne$first.previous){continue}
      $witness.rollback_observed=$true;$witness.rollback=$rollback
      for($k=$j+1;$k-lt$sets.Count;$k++){
       $retry=$sets[$k]
       if($retry.api-eq$first.api-and$retry.replacement-eq$first.replacement-and$retry.previous-eq$first.previous-and$retry.('before_'+$which)-eq$first.previous-and$retry.('after_'+$which)-eq$first.replacement){$witness.retry_observed=$true;$witness.retry=$retry;break}
      };break
     };break
    }
    if($witness.mismatch_observed){
     Check ($witness.first.previous-ne0-and$witness.first.previous-ne$witness.first.replacement-and$witness.rollback_observed) "$name changed native predecessor was restored and rollback returned our exact installed top"
     Check ($witness.retry_observed-and$record.readiness.reply.ok-and$record.status.reply.wndproc_binding_ready-eq$true) "$name verified fresh rollback is followed by a new native attach and actual readiness"
    }
    if($witness.required){Check $witness.mismatch_observed "$name required x86 virgin getter/native-return difference actually observed"}
   }
   $record.fresh_rollback_witness=$witness
   $cases.Add($record);Require ((Foreground).pid-ne$p.Id) "$name actual foreground remains outside observer"
  }finally{StopOwned $owned;$owned=$null}
 }
};$complete=$true}catch{$exception=$_.Exception.Message;Check $false ('runner incomplete: '+$exception)}finally{
 if($owned){StopOwned $owned};$failed=@($checks|Where-Object {-not$_.pass}).Count
 @{schema=1;completed=$complete;status=$(if($failed-or-not$complete){'failed'}else{'passed'});failed_check_count=$failed;positive_case_count=$checks.Count-$failed;checks=$checks;cases=$cases;calls=$calls;fixtures=@($fixtures|ForEach-Object {$v=@{};foreach($k in $_.Keys){if($k-ne'process'){$v[$k]=$_[$k]}};$v});exception=$exception;native_only_control=[bool]$NativeOnlyControl;require_x86_rollback_witness=[bool]$RequireX86RollbackWitness;physical_input_generated=$false;native_write_trampolines_called=$false;production_procedure_tokens_unwrapped=$false;source_sha256=(Get-FileHash $source -Algorithm SHA256).Hash;tool_issues=@()}|ConvertTo-Json -Depth 45|Set-Content "$run\result.json" -Encoding utf8NoBOM
 Write-Host "$($checks.Count-$failed) PASS / $failed FAIL ($run)"
}
if($failed-or-not$complete){exit 1};exit 0
