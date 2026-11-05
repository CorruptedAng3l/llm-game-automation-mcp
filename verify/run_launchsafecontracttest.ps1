# PLAIN owned launch only. All desktop/input writers are terminal in CLI AND target.
# Agent preparation is static; root owns compile/native execution after review.
param([Parameter(Mandatory=$true)][string]$RepoRoot,[Parameter(Mandatory=$true)][string]$SourceRoot,[Parameter(Mandatory=$true)][string]$DllDir,[string]$CaseFilter='',[string]$Out="$PSScriptRoot\launch_safe_contract")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;$SourceRoot=(Resolve-Path -LiteralPath $SourceRoot).Path;$DllDir=(Resolve-Path -LiteralPath $DllDir).Path
if(-not(Test-Path -LiteralPath "$SourceRoot\src\vinput_cli.cpp")){throw 'SourceRoot must contain the explicitly pinned CLI source'}
$outRoot=[IO.Path]::GetFullPath($Out);$privateRoot=$RepoRoot.TrimEnd('\')+'\'
if(-not$outRoot.StartsWith($privateRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Out must remain inside the private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\tools","$run\temp")
$env:TEMP="$run\temp";$env:TMP="$run\temp"
try{
. "$PSScriptRoot\vi_retargetrelease_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_launch_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_launch_tail.cs"}
if(-not('ViR2Pipe'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r2_pipe.cs"}
if(-not('ViLaunchSafeNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViLaunchSafeNative{
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]public static extern IntPtr OpenEventW(uint access,bool inherit,string name);
 [DllImport("kernel32.dll",SetLastError=true)]public static extern bool SetEvent(IntPtr h);
 [DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
}
'@}
}catch{@{schema=1;completed=$false;failed=1;error=('setup/compile prerequisite: '+$_.Exception.Message);native_fixture_started=$false;early_behavior_measured=$false}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$run\result.json" -Encoding utf8NoBOM;throw}
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$builds=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new();$complete=$false;$instrumentOk=$true;$case=$null
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name"}
function Require([bool]$Good,[string]$Name){Check $Good $Name;if(-not$Good){throw $Name}}
function Records([string]$Path){if(Test-Path -LiteralPath $Path){foreach($line in ((Get-Content -LiteralPath $Path -Raw)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}}
function HwndValue($Value){try{if($Value-isnot[string]-or-not$Value){return [uint64]0};return [Convert]::ToUInt64($Value,16)}catch{return [uint64]0}}
function Foreground{[uint32]$owner=0;$window=[ViLaunchSafeNative]::GetForegroundWindow();[void][ViLaunchSafeNative]::GetWindowThreadProcessId($window,[ref]$owner);return @{hwnd=$window.ToInt64();pid=$owner;read_only=$true}}
function Live($T){$row=ReadRetainedImage $T.process $T.path $T.created $T.retained_handle;$T.latest_identity=$row;if(-not$row.matched){throw ('Fresh live identity unavailable: '+$row.reason)}}
function Track($P,[string]$Image,[long]$Created,[string]$Role,[bool]$CreatorHandle){
 $t=@{process=$P;pid=$P.Id;path=$Image;created=$Created;role=$Role;retained_handle=0;creator_returned_handle=$CreatorHandle;initial_child_attachment=(-not$CreatorHandle);startup=$null;cache=$null;acked=$false;cleanup=$null}
 $case.actors.Add($t)
 $t.retained_handle=$P.Handle.ToInt64();if($Created-le0){$Created=$P.StartTime.ToUniversalTime().Ticks;$t.created=$Created}
 $seen=ObserveStartupImage $P $Image $Created $t.retained_handle
 Require $seen.matched "$($case.name) $Role initial exact kernel identity"
 $t.startup=ReadRetainedImage $P $Image $Created $t.retained_handle;$t.cache=New-ViStrictCliStartupCache $t.startup $t.retained_handle ([uint32]$t.pid) $Created $Image
 return $t
}
function Ack($T,$Wait,$Guard){
 Require ($Wait.pid-is[long]-and$Wait.creation_filetime-is[long]-and$Wait.case_nonce-is[string]-and$Wait.ack_name-is[string]-and$Wait.case_nonce-ceq$case.nonce-and$Wait.role-ceq$T.role-and$Wait.pid-eq$T.pid-and[DateTime]::FromFileTimeUtc([long]$Wait.creation_filetime).Ticks-eq$T.created-and$Guard.verified_before_product-is[bool]-and$Guard.verified_before_product-and$Guard.native_write_trampolines_called-is[bool]-and-not$Guard.native_write_trampolines_called) "$($case.name) $($T.role) exact nonce/creation/terminal guard before ACK"
 $expectedName="$($case.prefix)_$($T.role)_$($T.pid)";Require ($Wait.ack_name-ceq$expectedName) "$($case.name) exact private ACK name"
 Live $T;$event=[ViLaunchSafeNative]::OpenEventW(2,$false,$expectedName);Require ($event-ne[IntPtr]::Zero) "$($case.name) own ACK event exists"
 try{Require ([ViLaunchSafeNative]::SetEvent($event)) "$($case.name) guard ACK accepted"}finally{[void][ViLaunchSafeNative]::CloseHandle($event)};$T.acked=$true
}
function Discover{
 $logs=@(Get-ChildItem -LiteralPath $case.folder -Filter 'cli_*.jsonl');if($logs.Count-gt4){throw 'Unexpected CLI count in one bounded case'}
 foreach($log in $logs){foreach($spawn in @(Records $log.FullName|Where-Object event -eq spawned_owned)){
  $existing=@($case.actors|Where-Object pid -eq $spawn.child_pid)|Select-Object -First 1
  if($existing){Require ($spawn.creation_known-eq$true-and$spawn.case_nonce-ceq$case.nonce-and[DateTime]::FromFileTimeUtc([long]$spawn.creation_filetime).Ticks-eq$existing.created-and$spawn.role-ceq$existing.role) "$($case.name) repeated creator record matches retained object";continue}
  $parent=@($case.actors|Where-Object pid -eq $spawn.parent_pid)|Select-Object -First 1
  Require ($parent-and$parent.acked-and$parent.role-ceq'cli'-and$spawn.creation_known-is[bool]-and$spawn.creation_known-and$spawn.case_nonce-ceq$case.nonce-and$spawn.role-in@('target','cli')-and$spawn.creation_filetime-is[long]-and$spawn.child_pid-is[long]) "$($case.name) creator journal has exact owned nonce/PID/time"
  $image=if($spawn.role-eq'target'){$case.target_image}else{"$run\tools\x64\vinput32.exe"}
  $p=[Diagnostics.Process]::GetProcessById([int]$spawn.child_pid);$t=Track $p $image ([DateTime]::FromFileTimeUtc([long]$spawn.creation_filetime).Ticks) ([string]$spawn.role) $false;$t.creator_journal=$spawn
 }}
 foreach($log in $logs){
  foreach($publication in @(Records $log.FullName|Where-Object event -eq windows_publication_wait)){Require ($publication.complete-is[bool]-and$publication.complete) "$($case.name) complete window set published before real launch enumeration"}
  $rows=@(Records $log.FullName);$wait=@($rows|Where-Object event -eq guard_wait)|Select-Object -First 1;$guard=@($rows|Where-Object {$_.event-eq'terminal_guard'-and$_.stage-eq'before_cli_entry'})|Select-Object -First 1
  if(-not$wait-or-not$guard){continue}
  $t=@($case.actors|Where-Object pid -eq $wait.pid)|Select-Object -First 1
  if(-not$t){continue} # wait for its creator journal; never attach by a guard filename alone
  Require ([DateTime]::FromFileTimeUtc([long]$wait.creation_filetime).Ticks-eq$t.created-and$wait.case_nonce-ceq$case.nonce) "$($case.name) CLI guard record retains exact object/case"
  if(-not$t.acked){Ack $t $wait $guard};$t.guard_log=$log.FullName
 }
 $rows=@(Records $case.target_log);$wait=@($rows|Where-Object event -eq guard_wait)|Select-Object -First 1;$guard=@($rows|Where-Object {$_.event-eq'terminal_guard'-and$_.stage-eq'before_windows'})|Select-Object -First 1
 if($wait-and$guard){
  $t=@($case.actors|Where-Object role -eq target)|Select-Object -First 1
  if(-not$t){return} # finite next poll; creator journal is required before attachment
  Require ([DateTime]::FromFileTimeUtc([long]$wait.creation_filetime).Ticks-eq$t.created-and$wait.case_nonce-ceq$case.nonce) "$($case.name) target guard record retains exact object/case"
  if(-not$t.acked){Ack $t $wait $guard};$t.guard_log=$case.target_log
  $ready=@($rows|Where-Object event -eq windows_ready)|Select-Object -First 1
  if($ready){
   $wantMain=$case.plan.mode-notin@('hidden_owner','hidden_unproven');$wantPopup=$case.plan.mode-notin@('normal','paused_queue','hidden_unproven','selection_mismatch');$wantSplash=$case.plan.mode-in@('disabled_large_main','selection_mismatch')
   Require ($ready.main_visible-is[bool]-and$ready.popup_visible-is[bool]-and$ready.splash_visible-is[bool]-and$ready.main_visible-eq$wantMain-and$ready.popup_visible-eq$wantPopup-and$ready.splash_visible-eq$wantSplash) "$($case.name) actual initial window-set visibility prerequisite"
  }
 }
}
function FinishActors{
 foreach($t in $case.actors){
  if($t.cleanup){continue}
  try{$t.cleanup=AccountOrReapCli $t.process $t.path $t.created $t.retained_handle ([uint32]$t.pid) $t.cache}catch{$t.cleanup=@{completion_owned=$false;error=$_.Exception.Message;reaped=$false}}
  Check ($t.cleanup.completion_owned-eq$true-and-not$t.cleanup.error) "$($case.name) $($t.role) owned finite cleanup/accounting"
  if($t.cleanup.completion_owned-ne$true){$script:instrumentOk=$false}
 }
}
function FinishTailsAndHandles($OutTail,$ErrTail){
 try{
  if($OutTail){$null=$OutTail.Wait(500);$case.stdout=$OutTail.Text();$case.stdout_complete=$OutTail.Complete;$case.stdout_characters=$OutTail.Characters;$case.stdout_error=$OutTail.Error}
  if($ErrTail){$null=$ErrTail.Wait(500);$case.stderr=$ErrTail.Text();$case.stderr_complete=$ErrTail.Complete;$case.stderr_characters=$ErrTail.Characters;$case.stderr_error=$ErrTail.Error}
  if($OutTail-and$ErrTail){
   $tails=$OutTail.Complete-and$ErrTail.Complete-and$OutTail.Characters-le65536-and$ErrTail.Characters-le65536-and-not$OutTail.Error-and-not$ErrTail.Error
   Check $tails "$($case.name) final bounded stream drain before handle disposal"
   if(-not$tails){$script:instrumentOk=$false;$case.completed=$false}
  }
 }catch{$script:instrumentOk=$false;$case.completed=$false;$case.tail_cleanup_error=$_.Exception.Message;Check $false "$($case.name) final stream cleanup failed"}
 finally{foreach($t in $case.actors){if($t.process){try{$t.process.Dispose()}catch{$t.dispose_error=$_.Exception.Message;$script:instrumentOk=$false;$case.completed=$false};$t.Remove('process')}}}
}
function ReadMain($T,$OutTail,$ErrTail){
 $case.exit_accounting=Get-ViExitedCliAccounting $T.retained_handle ([uint32]$T.pid) $T.created $T.path $T.cache
 Require (Test-ViExitedCliAccounting $case.exit_accounting) "$($case.name) main CLI native-signaled exit accounted"
 $case.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$case.exit_accounting.native_exit_code),0)
 $null=$OutTail.Wait(500);$null=$ErrTail.Wait(500);$case.stdout=$OutTail.Text();$case.stderr=$ErrTail.Text();$case.stdout_complete=$OutTail.Complete;$case.stderr_complete=$ErrTail.Complete;$case.stdout_characters=$OutTail.Characters;$case.stderr_characters=$ErrTail.Characters
 Require ($case.stdout_complete-and$case.stderr_complete-and$OutTail.Characters-le65536-and$ErrTail.Characters-le65536-and-not$OutTail.Error-and-not$ErrTail.Error) "$($case.name) bounded complete separate stdout/stderr"
 $lines=@($case.stdout-split'\r?\n'|Where-Object {$_-match'\S'});$case.stdout_line_count=$lines.Count;$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
 Require ($lines.Count-eq1-and$reply-and$reply.ok-is[bool]) "$($case.name) one JSON record (existing quiet-inject baseline contract)"
 $case.reply=$reply;Check (($case.exit-eq0)-eq$reply.ok) "$($case.name) actual ok/native-exit parity"
}
function FollowPending($OriginalStart,$Target,$Dll){
 Live $Target;$birth=@(Records $case.target_log|Where-Object event -eq owned_identity)|Select-Object -First 1
 Require ($birth.pid-eq$Target.pid-and[DateTime]::FromFileTimeUtc([long]$birth.creation_filetime).Ticks-eq$Target.created) 'follow target birth matches original retained handle'
 $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$OriginalStart.FileName;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
 foreach($kv in $OriginalStart.Environment.GetEnumerator()){$si.Environment[$kv.Key]=$kv.Value}
 foreach($arg in @('--json','launch','--follow-pid',[string]$Target.pid,'--follow-created',[string]$birth.creation_filetime,'--dll',$Dll,'--settle','0','--fg-guard','0','--timeout','10','--claim-ttl','123')){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;Require ($p.Start()) 'guarded follow CLI started';$actor=Track $p $si.FileName 0 cli $true
 $out=[ViWndProcAwTail]::new($p.StandardOutput);$err=[ViWndProcAwTail]::new($p.StandardError)
 try{
  $end=[Environment]::TickCount64+30000;do{Discover;if($p.HasExited){break};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end)
  Require $p.HasExited 'follow CLI bounded exit';Discover;$proof=Get-ViExitedCliAccounting $actor.retained_handle ([uint32]$actor.pid) $actor.created $actor.path $actor.cache
  Require (Test-ViExitedCliAccounting $proof) 'follow original-handle exit accounting';$null=$out.Wait(500);$null=$err.Wait(500)
  Require ($out.Complete-and$err.Complete-and-not$out.Error-and-not$err.Error-and$out.Characters-le65536-and$err.Characters-le65536) 'follow bounded complete streams'
  $lines=@($out.Text()-split'\r?\n'|Where-Object {$_-match'\S'});Require ($lines.Count-eq1) 'one follow JSON record';$r=$lines[0]|ConvertFrom-Json
  $case.follow=@{reply=$r;exit=$proof.native_exit_code;stdout=$out.Text();stderr=$err.Text();exit_accounting=$proof}
  Check ($proof.native_exit_code-eq0-and$r.ok-and$r.pid-eq$Target.pid-and$r.claimed-and$r.owner-ceq$case.reply.owner-and$r.readiness_unconfirmed-eq$true-and$r.input_ready-eq$false-and$r.queue_ready-eq$false-and$r.spawned_pid-eq$null-and$r.final_command_line-eq$null) "$($case.name) followed live window returns success and takes claim while queue stays pending"
 }finally{$null=$out.Wait(500);$null=$err.Wait(500)}
}
try{
 foreach($file in @((Get-ChildItem -LiteralPath "$SourceRoot\src","$RepoRoot\src" -File),(Get-ChildItem -LiteralPath "$PSScriptRoot\..\test" -File|Where-Object {$_.Name-like'vi_launch*'-or$_.Name-eq'vi_cursor_spies.h'}))|ForEach-Object {$_}|Sort-Object FullName -Unique){$pins.Add(@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash})}
 foreach($arch in @('x64','x86')){
  $folder="$run\tools\$arch";[void](New-Item -ItemType Directory -Force "$folder\temp");$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$cliName=if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'}
  foreach($object in @('vimh_buffer.obj','vimh_hook.obj','vimh_trampoline.obj','vimh_hde.obj')){$pins.Add(@{path="$RepoRoot\build\$arch\$object";sha256=(Get-FileHash -LiteralPath "$RepoRoot\build\$arch\$object" -Algorithm SHA256).Hash})}
  $objects="`"$RepoRoot\build\$arch\vimh_buffer.obj`" `"$RepoRoot\build\$arch\vimh_hook.obj`" `"$RepoRoot\build\$arch\vimh_trampoline.obj`" `"$RepoRoot\build\$arch\vimh_hde.obj`""
  $batch=@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++20 /I"$SourceRoot\src" /I"$RepoRoot\src" /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_launch_guarded_cli.cpp" /Fo:"$folder\cli.obj" /Fe:"$folder\$cliName" /link /SUBSYSTEM:CONSOLE $objects || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_launch_ownedtarget.cpp" /Fo:"$folder\target.obj" /Fe:"$folder\target.exe" /link /SUBSYSTEM:WINDOWS $objects || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /LD "$PSScriptRoot\..\test\vi_launch_exit73.cpp" /Fo:"$folder\exit73.obj" /Fe:"$folder\exit73.dll" /link /IMPLIB:"$folder\exit73.lib" || exit /b 2
exit /b 0
"@;[IO.File]::WriteAllText("$folder\build.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$folder\build.cmd" *> "$folder\build.log";$builds.Add(@{arch=$arch;exit=$LASTEXITCODE;log="$folder\build.log"});Require ($LASTEXITCODE-eq0) "$arch guarded source CLI/target/exit73 fixture compile W4 WX"
 }
 Copy-Item -LiteralPath "$run\tools\x86\vinput32.exe" -Destination "$run\tools\x64\vinput32.exe"
 $plans=@(@{name='normal';mode='normal';kind='inject'},@{name='guard_budget';mode='normal';kind='guard_budget'},@{name='hidden_unproven';mode='hidden_unproven';kind='hidden_unproven'},@{name='queue_pending';mode='paused_queue';kind='queue_pending'},@{name='affinity_echo';mode='normal';kind='affinity'},@{name='no_inject';mode='normal';kind='noinject'},@{name='owned_popup_refusal';mode='hidden_owner';kind='reject'},@{name='allow_false';mode='hidden_owner';kind='reject_false'},@{name='allow_invalid';mode='hidden_owner';kind='invalid_flag'},@{name='enabled_main';mode='main_and_popup';kind='inject'},@{name='disabled_larger';mode='disabled_large_main';kind='mismatch_disabled'},@{name='selection_mismatch';mode='selection_mismatch';kind='mismatch'},@{name='allow_owned_popup';mode='hidden_owner';kind='allow'},@{name='missing_dll';mode='normal';kind='missing'},@{name='target_exit73';mode='normal';kind='exit73'})
 if($CaseFilter){$plans=@($plans|Where-Object name -match $CaseFilter)};$expectedCases=$plans.Count*3
 foreach($pair in @(@{name='x64';cli='x64';target='x64'},@{name='x86';cli='x86';target='x86'},@{name='delegated_x86';cli='x64';target='x86'})){foreach($plan in $plans){
  $name=$pair.name+'.'+$plan.name;$folder=Join-Path $run $name;[void](New-Item -ItemType Directory -Force $folder);$nonce=[guid]::NewGuid().ToString('N');$target="$run\tools\$($pair.target)\target.exe";$cli=if($pair.cli-eq'x64'){"$run\tools\x64\vinput.exe"}else{"$run\tools\x86\vinput32.exe"}
  $dll=if($plan.kind-eq'exit73'){"$run\tools\$($pair.target)\exit73.dll"}elseif($plan.kind-eq'missing'){"$folder\missing.dll"}else{Join-Path $DllDir $(if($pair.target-eq'x64'){'vinput64.dll'}else{'vinput32.dll'})}
  if($plan.kind-ne'missing'){Require (Test-Path -LiteralPath $dll) "$name exact DLL exists"}
  $case=@{dll_path=$dll;dll_sha256=if(Test-Path -LiteralPath $dll){(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash}else{$null};name=$name;nonce=$nonce;prefix="Local\vi_launch_$nonce";folder=$folder;target_log="$folder\target.jsonl";target_image=$target;plan=$plan;pair=$pair;actors=[Collections.Generic.List[object]]::new();completed=$false;before=Foreground;source_measurement='guarded CLI rebuilt from pinned source; not retail CLI binary';early_behavior='UNMEASURED; early arguments rejected before entry'};$cases.Add($case)
  $p=$null;$outTail=$null;$errTail=$null
  try{
   $s=[Diagnostics.ProcessStartInfo]::new();$s.FileName=$cli;$s.UseShellExecute=$false;$s.CreateNoWindow=$true;$s.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$s.RedirectStandardOutput=$true;$s.RedirectStandardError=$true
   foreach($entry in @{VI_LAUNCH_GUARD_DIR=$folder;VI_LAUNCH_ACK_PREFIX=$case.prefix;VI_LAUNCH_CASE_NONCE=$nonce;VI_LAUNCH_TARGET=$target;VI_LAUNCH_TARGET_LOG=$case.target_log;VI_LAUNCH_DLL=$dll;VI_LAUNCH_TEST_AFFINITY=$(if($plan.kind-eq'affinity'){'1'}else{''});VI_LAUNCH_TEST_GUARD_BUDGET=$(if($plan.kind-eq'guard_budget'){'1'}else{''});VI_LAUNCH_TEST_OWNER=$(if($plan.kind-in@('queue_pending','guard_budget')){'vi-launch-'+$nonce}else{''})}.GetEnumerator()){$s.Environment[$entry.Key]=[string]$entry.Value}
   $argv=@('--json','launch','--exe',$target,'--args',('"'+$case.target_log+'" '+$plan.mode),'--dll',$dll,'--settle','0','--fg-guard','0','--timeout','10')
   if($plan.kind-eq'guard_budget'){$argv=@('--json','launch','--exe',$target,'--args',('"'+$case.target_log+'" '+$plan.mode),'--dll',$dll,'--settle','0','--fg-guard','5','--timeout','8','--claim-ttl','123')}
   if($plan.kind-in@('noinject','reject','reject_false','invalid_flag')){$argv+='--no-inject'};if($plan.kind-eq'allow'){$argv+='--allow-dialog'};if($plan.kind-eq'reject_false'){$argv+='--allow-dialog=false'};if($plan.kind-eq'invalid_flag'){$argv+='--allow-dialog=maybe'}
   $requestedAffinity=$null
   if($plan.kind-eq'affinity'){$available=[Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity.ToInt64();$lowest=$available-band(-$available);Require ($lowest-ne0) 'usable allowed affinity bit';$requestedAffinity='0x{0:X}'-f$lowest;$argv+=@('--cpu-affinity',$requestedAffinity)}
   if($plan.kind-eq'queue_pending'){$argv+=@('--claim-ttl','123')}
   foreach($arg in $argv){[void]$s.ArgumentList.Add([string]$arg)};$case.arguments=$argv;$p=[Diagnostics.Process]::new();$p.StartInfo=$s;Require ($p.Start()) "$name guarded CLI started"
   $main=Track $p $cli 0 cli $true;$outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError)
   $deadline=[Environment]::TickCount64+80000
   do{Discover;if($p.HasExited){break};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$deadline)
   Require $p.HasExited "$name main CLI finite exit deadline";Discover;ReadMain $main $outTail $errTail
   $targetActor=@($case.actors|Where-Object role -eq target)|Select-Object -First 1;$ready=@(Records $case.target_log|Where-Object event -eq windows_ready)|Select-Object -First 1
   if($plan.kind-ne'invalid_flag'){Require ($targetActor-and$targetActor.acked-and$ready-and$ready.pid-eq$targetActor.pid-and$ready.thread) "$name guarded target identity and actual windows witness"}
   foreach($actor in $case.actors){Require $actor.acked "$name every observed CLI/target ACKed after strict live cache"}
   $r=$case.reply
   if($plan.kind-eq'invalid_flag'){Check ($r.ok-eq$false-and$null-eq$targetActor-and$r.error-match'allow-dialog') "$name invalid opt-in refuses before target scheduling"}
   elseif($plan.kind-eq'queue_pending'){
    Check ($r.ok-and$r.injected-and$r.claimed-and$r.owner-ceq('vi-launch-'+$nonce)-and$r.input_ready-eq$false-and$r.queue_ready-eq$false-and$r.readiness_unconfirmed-eq$true-and$r.launch_readiness.readiness_reason-ceq'queue_not_progressing'-and$r.launch_readiness.pid-eq$targetActor.pid) "$name live adopted target succeeds and claims while queue readiness stays explicitly pending"
    if($r.ok){FollowPending $s $targetActor $dll}
   }
   elseif($plan.kind-eq'hidden_unproven'){
    Check (-not$r.ok-and$r.window_hidden-eq$true-and$r.window_adopted-eq$false-and$r.hidden_discovery_inject.injected-eq$true-and$r.hidden_discovery_inject.ok-eq$true-and$r.launch_elapsed_ms-lt10000-and-not[ViLaunchSafeNative]::IsWindowVisible([IntPtr]::new([long]$ready.main))) "$name unproven hidden window returns bounded metadata without showing or adopting"
    Live $targetActor;$wire=[ViR2Pipe]::Query([uint32]$targetActor.pid,$targetActor.retained_handle,$targetActor.created,$targetActor.path,'{"cmd":"frame-hash","observation_only":true}',2000);$case.capture_refusal=@{complete=$wire.Complete;server_pid=$wire.ServerPid;text=$wire.Text;error=$wire.Error}
    Require ($wire.Complete-and$wire.ServerPid-eq$targetActor.pid-and-not$wire.Error) 'exact owned capture refusal transport';$refused=$wire.Text|ConvertFrom-Json
    Check (-not$refused.ok-and$refused.reason-ceq'no_window_adopted'-and$refused.window_adopted-eq$false-and$refused.error-match'^no window adopted') "$name capture names the absent adopted window"
   }
   elseif($plan.kind-eq'guard_budget'){
    Check ($r.ok-and$r.pid-eq$targetActor.pid-and$r.claimed-and$r.owner-ceq('vi-launch-'+$nonce)-and$r.input_ready-eq$true-and$r.queue_ready-eq$true-and$r.launch_deadline_exceeded-eq$false-and$r.launch_elapsed_ms-lt8000) "$name healthy loaded window retains mandatory finalization inside eight-second budget"
    Check ($r.fg_guard_requested_ms-eq5000-and$r.fg_guard_applied_ms-ge0-and$r.fg_guard_applied_ms-le5050-and$r.finalization_reserve_ms-eq1500) "$name requested and applied guard observations are reported separately"
   }
   elseif($plan.kind-in@('reject','reject_false')){
    Check ($r.ok-eq$false-and$r.dialog_owned_popup-eq$true-and$r.dialog_text-match'Synthetic renderer initialization failed'-and$r.window_title-ceq'Owned startup error'-and(HwndValue $r.dialog_owner_hwnd)-eq[uint64]$ready.main) "$name owned popup is rejected with bounded title/text/owner"
   }elseif($plan.kind-in@('mismatch','mismatch_disabled')){
    $expected=if($plan.kind-eq'mismatch'){[uint64]$ready.splash}else{[uint64]$ready.main};$actual=if($plan.kind-eq'mismatch'){[uint64]$ready.main}else{[uint64]$ready.splash}
    Check ($r.ok-eq$false-and$r.selection_mismatch-eq$true-and(HwndValue $r.expected_window_hwnd)-eq$expected-and(HwndValue $r.actual_window_hwnd)-eq$actual-and$r.inject.ok-eq$true) "$name independent CLI/core selector mismatch fails explicitly with both HWNDs"
   }elseif($plan.kind-in@('missing','exit73')){
    Check ($r.ok-eq$false-and$null-ne$r.inject-and$r.inject.ok-is[bool]-and$r.inject.ok-eq$false) "$name actual inner inject failure is retained"
    if($plan.kind-eq'exit73'){Check ($r.inject.target_exited-eq$true-and$r.inject.exit_code-eq73) "$name inner retained-target native exit73 retained"}
   }elseif($plan.kind-eq'noinject'){
    Check ($r.ok-eq$true-and$r.injected-eq$false-and$r.PSObject.Properties['inject']-and$null-eq$r.inject-and$r.PSObject.Properties['early_inject']-and$null-eq$r.early_inject-and$r.PSObject.Properties['dialog_inject']-and$null-eq$r.dialog_inject) "$name no-inject emits explicit null inner records"
   }else{
    Check ($r.ok-eq$true-and$r.injected-eq$true-and$null-ne$r.inject-and$r.inject.ok-eq$true-and$r.inject.readiness.hook_ready-eq$true-and$r.inject.readiness.window_ready-eq$true-and$r.inject.readiness.queue_ready-eq$true-and$r.inject.pid-eq$targetActor.pid) "$name actual full inner injection/readiness preserved"
    $expectedWindow=if($plan.kind-eq'allow'){[uint64]$ready.popup}else{[uint64]$ready.main}
    $actualWindow=if($r.inject.readiness.hwnd){HwndValue $r.inject.readiness.hwnd}else{0}
    Check ($actualWindow-eq$expectedWindow-and(HwndValue $r.window_hwnd)-eq$expectedWindow-and(HwndValue $r.launch_readiness.hwnd)-eq$expectedWindow-and$r.window_title-ceq$(if($plan.kind-eq'allow'){'Owned startup error'}else{'Synthetic main'})) "$name exact legitimate/explicitly allowed HWND selection"
    if($plan.kind-eq'allow'){Check ($r.dialog_inject.readiness.hook_ready-eq$true-and$r.inject.readiness.queue_ready-eq$true) "$name hooks-only dialog stage stays separate from final full readiness"}
   }
   if($plan.kind-ne'invalid_flag'){
    Check ($r.final_command_line-ceq('"'+$target+'" "'+$case.target_log+'" '+$plan.mode)-and$r.spawned_pid-eq$targetActor.pid) "$name exact final command line survives all outcome paths"
   }
   if($plan.kind-eq'affinity'){Check ($r.requested_cpu_affinity-ceq$requestedAffinity-and$r.applied_cpu_affinity-ceq$requestedAffinity-and$r.cpu_affinity_applied-eq$true) "$name actual applied affinity is echoed"}
   if($r.PSObject.Properties['user_took_foreground']){Check ($r.user_took_foreground-eq$false) "$name never-foreground target does not claim human foreground selection"}
   $case.after=Foreground;Check (@($case.actors|Where-Object pid -eq $case.after.pid).Count-eq0) "$name passive actual foreground remains outside owned actors"
   $case.completed=$true
  }catch{$instrumentOk=$false;$case.error=$_.Exception.Message;Check $false ("$name instrument/prerequisite: "+$case.error)}finally{try{FinishActors}finally{FinishTailsAndHandles $outTail $errTail;$case=$null}}
 }}
 $complete=$instrumentOk-and$cases.Count-eq$expectedCases-and@($cases|Where-Object completed -eq $true).Count-eq$expectedCases
}catch{$instrumentOk=$false;Check $false ('setup/instrument: '+$_.Exception.Message)}finally{
 if($case){try{FinishActors}finally{FinishTailsAndHandles $outTail $errTail;$case=$null}}
 foreach($c in $cases){$c.guard_receipts=@(Get-ChildItem -LiteralPath $c.folder -Filter '*.jsonl'|ForEach-Object { @{path=$_.FullName;text=(Get-Content -LiteralPath $_.FullName -Raw)} })}
 @{schema=1;completed=$complete;expected_case_count=$expectedCases;completed_case_count=@($cases|Where-Object completed -eq $true).Count;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;checks=$checks;cases=$cases;builds=$builds;pins=$pins;source_root=$SourceRoot;dll_dir=$DllDir;early_behavior_measured=$false;historical_ok_exit_mismatch_reproduced=$false;failure_is_expected_inversion=$false;tool_issues=@()}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath "$run\result.json" -Encoding utf8NoBOM
}
Write-Host "$(@($checks|Where-Object pass -eq $true).Count) PASS / $(@($checks|Where-Object pass -eq $false).Count) FAIL ($run)";if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
