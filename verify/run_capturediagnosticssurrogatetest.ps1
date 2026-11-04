# KNOWN-SOURCE PRINTWINDOW API SURROGATE; not native PrintWindow compatibility.
# Add-only derivative of original native runner SHA256
# 2CDF43DCB210FE2153B3008CF1303CEC06CF48DCD5F370A66F7084FB5C378AA5.
# Preserve original native112 PASS/14 FAIL receipt; no expected-red inversion.
param([string]$CliDir='', [string]$RepoRoot='', [string]$Out="$PSScriptRoot\..\build\test\capture_diagnostics_surrogate\after")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
. "$PSScriptRoot\vi_capture_diag_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
if(-not('ViCaptureDiagTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_capture_diag_process.cs"}
if(-not$RepoRoot){$candidate=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($n=0;$n-lt8-and$candidate;$n++){if((Test-Path (Join-Path $candidate.FullName 'src\vinput_core.cpp'))-and(Test-Path (Join-Path $candidate.FullName 'build\x64\host_mh\buffer.obj'))){$RepoRoot=$candidate.FullName;break};$candidate=$candidate.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot for tested prebuilt host_mh objects'}
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliDir).Path);$V=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir 'vinput.exe')).Path)
$source=(Resolve-Path -LiteralPath "$PSScriptRoot\..\test\vi_capture_diagnostics_surrogate.cpp").Path
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force -Path $run|Out-Null;$run=(Resolve-Path -LiteralPath $run).Path
$checks=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new();$exception=$null;$notMeasured=[Collections.Generic.List[object]]::new();$completeCandidatePositiveCount=0;$positiveCaseCount=0
Add-Type -AssemblyName System.Drawing
if(-not('ViCaptureDiagNative' -as [type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViCaptureDiagNative{
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
}
'@}
function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{pass=$ok;name=$name});Write-Host "$(if($ok){'PASS'}else{'FAIL'}) $name"}
function Require([bool]$ok,[string]$name){Check $ok $name;if(-not$ok){throw $name}}
function Text([string]$path){if(-not(Test-Path -LiteralPath $path)){return ''};$s=[IO.FileStream]::new($path,'Open','Read',([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));$r=[IO.StreamReader]::new($s);try{return $r.ReadToEnd()}finally{$r.Dispose();$s.Dispose()}}
function Records([string]$path){foreach($line in ((Text $path)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
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
function AccountOrReapCli($P,[string]$Expected,$Created,[long]$Retained,[uint32]$ExpectedPid,$StartupCache){
 # This read-only query cannot authorize Reap, Kill, Control, Post or AssertOwned.
 $proof=Get-ViExitedCliAccounting $Retained $ExpectedPid $Created $Expected $StartupCache
 if($proof.native_signaled){
  $identity=ReadRetainedImage $P $Expected $Created $Retained
  $r=@{reaped=$true;identity_verified=[bool]$identity.primary_identity_match;identity=$identity;exit_known=$proof.native_exit_known;native_exit_code=$null;error=$null;deadline_ms=1500;termination_attempted=$false;identity_source='current strict EA65 identity, unchanged; separate read-only exited accounting';exit_accounting=$proof;completion_owned=$false}
 }else{
  # Live action authority is still the unmodified fresh EA65 Reap path.
  $r=Reap $P $Expected $Created $Retained
  $proof=Get-ViExitedCliAccounting $Retained $ExpectedPid $Created $Expected $StartupCache
  $r.exit_accounting=$proof;$r.completion_owned=$false
 }
 if($proof.native_exit_known-and$proof.native_signaled){$r.exit_known=$true;$r.native_exit_code=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0)}
 $r.completion_owned=(Test-ViExitedCliAccounting $proof)-and$r.reaped-and-not$r.error
 if(-not$r.completion_owned-and-not$r.error){$r.error='Owned CLI exit accounting refused: '+$proof.reason}
 return $r
}
function Run([string]$Exe,[string[]]$Arguments,[int]$Budget=8000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Exe;$info.WorkingDirectory=$run;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;[void]$info.Environment.Remove('VINPUT_OWNER');$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;foreach($a in $Arguments){[void]$info.ArgumentList.Add([string]$a)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;$outTail=$null;$errTail=$null;$watch=[Diagnostics.Stopwatch]::StartNew();$started=$false;$actualPath=$null;$created=$null
 $r=@{exe=$Exe;pid=$null;actual_path=$null;creation_utc_ticks=$null;retained_handle=$null;startup_identity=$null;startup_cache=$null;startup_cache_valid=$false;exit=$null;exit_known=$false;timeout=$false;owned_reaped=$false;kill_error=$null;stdout='';stderr='';stdout_complete=$false;stderr_complete=$false;stdout_available=$false;stderr_available=$false;stdout_truncated=$false;stderr_truncated=$false;stdout_error=$null;stderr_error=$null;error=$null}
 try{
  if(-not$p.Start()){throw 'Owned process start failed'};$started=$true;$r.pid=$p.Id;$r.retained_handle=$p.Handle.ToInt64()
  $created=$p.StartTime.ToUniversalTime().Ticks;$r.creation_utc_ticks=$created;$r.startup_identity=ReadRetainedImage $p $Exe $created $r.retained_handle;try{$r.dotnet_start_path=$p.Path}catch{$r.dotnet_start_path_error=$_.Exception.Message};$r.expected_path=$Exe
  $outTail=[ViCaptureDiagTail]::new($p.StandardOutput);$errTail=[ViCaptureDiagTail]::new($p.StandardError)
  # A child without an earlier strict LIVE image observation stays incomplete.
  $r.startup_cache=New-ViStrictCliStartupCache $r.startup_identity $r.retained_handle ([uint32]$r.pid) $created $Exe
  $r.startup_cache_valid=$r.startup_cache-is[ViOwnedCliStartupCacheV1]
  if(-not$r.startup_cache_valid){throw 'Strict original child startup cache unavailable'}
  $r.timeout=-not$p.WaitForExit($Budget)
 }catch{$r.error=$_.Exception.Message}
 finally{
  if($started){$r.cleanup=AccountOrReapCli $p $Exe $created $r.retained_handle ([uint32]$r.pid) $r.startup_cache;if($r.cleanup.identity.primary_identity_match){$r.actual_path=$r.cleanup.identity.kernel_path};$r.owned_reaped=$r.cleanup.reaped;$r.kill_error=$r.cleanup.error;$r.exit_known=$r.cleanup.exit_known;$r.exit=$r.cleanup.native_exit_code}
  if($outTail){$r.stdout_available=$outTail.Wait(400);$r.stdout=$outTail.Text();$r.stdout_complete=$outTail.Complete;$r.stdout_truncated=$outTail.Characters-gt65536;$r.stdout_error=$outTail.Error}
  if($errTail){$r.stderr_available=$errTail.Wait(400);$r.stderr=$errTail.Text();$r.stderr_complete=$errTail.Complete;$r.stderr_truncated=$errTail.Characters-gt65536;$r.stderr_error=$errTail.Error}
  if($outTail-and-not$outTail.Complete){try{$p.StandardOutput.Close()}catch{}};if($errTail-and-not$errTail.Complete){try{$p.StandardError.Close()}catch{}}
  $watch.Stop();$r.elapsed_ms=$watch.ElapsedMilliseconds;$p.Dispose()
 }
 return $r
}
function Live-Owned($t){return Alive $t}
function Stop-Owned($t){if(-not$t-or-not$t.process-or$t.stop){return};$t.stop=Reap $t.process $t.exe $t.created $t.retained_handle;Check ([bool]($t.stop.reaped-and$t.stop.identity_verified)) "$($t.name) bounded exact retained-kernel fixture cleanup";if($t.stop.reaped){$t.process.Dispose();$t.process=$null}}
function Vi($t,[string[]]$arguments,[int]$budget=5000){Require (Alive $t) "$($t.name) retained kernel identity before CLI";$r=Run $t.cli (@('--json','--pid',"$($t.process.Id)")+$arguments) $budget;$lines=@($r.stdout-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}};$item=[pscustomobject]@{pid=$t.process.Id;arguments=$arguments;exit=$r.exit;timeout=$r.timeout;stdout=$r.stdout;stderr=$r.stderr;reply=$reply;process_receipt=$r;capture_transport='known_source_printwindow_api_surrogate';native_printwindow_compatibility_measured=$false};$calls.Add($item);Require (-not$r.timeout-and$r.owned_reaped-and-not$r.error-and$r.startup_cache_valid-and$r.cleanup.completion_owned-and-not$r.cleanup.error-and(Test-ViExitedCliAccounting $r.cleanup.exit_accounting)-and$r.exit_known-and$r.stdout_complete-and$r.stderr_complete-and-not$r.stdout_truncated-and-not$r.stderr_truncated-and$lines.Count-eq1-and$null-ne$reply) "$($t.name) bounded CLI and stream completion";return $item}
function Foreground-Pid{[uint32]$p=0;[void][ViCaptureDiagNative]::GetWindowThreadProcessId([ViCaptureDiagNative]::GetForegroundWindow(),[ref]$p);return $p}
function Snapshot($t,[uint64]$token,[uint64]$action=0){
 if(-not(Live-Owned $t)-or$t.process.HasExited){throw 'owned fixture identity ended before control post'}
 [uint32]$p=0;$h=[IntPtr]::new([long][uint64]$t.ready.hwnd);if([ViCaptureDiagNative]::GetWindowThreadProcessId($h,[ref]$p)-ne$t.thread-or$p-ne$t.process.Id){throw 'owned control HWND identity changed'}
 Require (Alive $t) "$($t.name) retained kernel lifetime immediately before control"
 if(-not[ViCaptureDiagNative]::PostMessageW($h,0x8090,[UIntPtr]::new($action),[IntPtr]::new([long]$token))){throw 'owned control PostMessage refused'}
 $until=[Environment]::TickCount64+2500;do{$record=@(Records $t.log|Where-Object {$_.kind-eq'snapshot'-and$_.token-eq$token})|Select-Object -Last 1;if($record){return $record};if($t.process.HasExited){throw "owned fixture exited$($t.process.ExitCode) awaiting snapshot$token"};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "owned snapshot$token unavailable"
}
function Start-Target([string]$exe,[string]$name,[string]$mode){
 $log=Join-Path $run ($name+'.jsonl');$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';[void]$info.ArgumentList.Add($log);[void]$info.ArgumentList.Add($mode)
 $p=[Diagnostics.Process]::Start($info);$t=[pscustomobject]@{process=$p;pid=$p.Id;path=$exe;exe=$exe;created=$null;retained_handle=$null;latest_lifecycle_identity=$null;startup_identity=$null;stop=$null;log=$log;ready=$null;name=$name;thread=[uint32]0;cli=$script:activeCli};$owned.Add($t);$t.retained_handle=$p.Handle.ToInt64();$t.created=$p.StartTime.ToUniversalTime().Ticks;$t.startup_identity=ObserveStartupImage $p $exe $t.created $t.retained_handle;Require $t.startup_identity.matched "$name exact retained kernel identity before ready"
 $until=[Environment]::TickCount64+3000;do{$t.ready=@(Records $log|Where-Object kind -eq ready)|Select-Object -First 1;if($t.ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 Require ($t.ready-and$t.ready.pid-eq$p.Id-and$t.ready.terminal_spies-and$t.ready.api_entries_verified-and$t.ready.no_activation_or_physical_input) "$name terminal safety spies and owned ready HWND"
 Require ($t.ready.capture_transport-eq'known_source_printwindow_api_surrogate'-and$t.ready.printwindow_surrogate_entry_verified-and$t.ready.native_printwindow_compatibility_measured-eq$false) "$name explicitly uses verified known-source PrintWindow API surrogate"
 $prepared=@(Records $log|Where-Object kind -eq replacement_constructor_prepared);Require ($prepared.Count-eq1-and$prepared[0].pre_dll_trampoline-eq$true-and$prepared[0].hook_enabled-eq$false-and$prepared[0].entry_bytes_unchanged-eq$true-and$prepared[0].native_create_called-eq$false) "$name pre-DLL replacement trampoline prepared without enabling or calling native Create"
 [uint32]$windowOwner=0;$t.thread=[ViCaptureDiagNative]::GetWindowThreadProcessId([IntPtr]::new([long][uint64]$t.ready.hwnd),[ref]$windowOwner);Require ($windowOwner-eq$p.Id-and$t.thread-ne0) "$name ready HWND has exact owned UI thread"
 Require ((Foreground-Pid)-ne$p.Id) "$name actual foreground outside fixture";return $t
}
function Bitmap-Observation([string]$path){$b=[Drawing.Bitmap]::new($path);try{$black=$true;$uniform=$true;$first=$b.GetPixel(0,0);for($y=0;$y-lt$b.Height;$y++){for($x=0;$x-lt$b.Width;$x++){$p=$b.GetPixel($x,$y);if($p.R-or$p.G-or$p.B){$black=$false};if($p.R-ne$first.R-or$p.G-ne$first.G-or$p.B-ne$first.B){$uniform=$false}}};$samples=@();foreach($point in @(@(5,5),@(50,10),@(90,10),@(10,50),@(10,90))){if($point[0]-lt$b.Width-and$point[1]-lt$b.Height){$c=$b.GetPixel($point[0],$point[1]);$samples+=@{x=$point[0];y=$point[1];r=[int]$c.R;g=[int]$c.G;b=[int]$c.B;a=[int]$c.A}}};return [pscustomobject]@{width=$b.Width;height=$b.Height;all_black=$black;uniform=$uniform;samples=$samples}}finally{$b.Dispose()}}

function Require-VisibleReplacement($t,$snapshot,[string]$label){
 $p=$snapshot.proxy_observation
 Require ($null-ne$p-and$p.is_window-is[bool]-and$p.is_window-and$p.visible-is[bool]-and$p.visible-and$p.style_visible-is[bool]-and$p.style_visible-and$p.noactivate-is[bool]-and$p.noactivate-and$p.client_ok-and$p.client_w-eq640-and$p.client_h-eq480-and$p.rect_ok-and$p.left-eq-25000-and$p.top-eq900-and$p.right-eq-24360-and$p.bottom-eq1380-and$p.pid-eq$t.process.Id-and$p.thread-eq$t.thread-and$p.query_thread-eq$t.thread-and$p.on_proxy_ui_thread-and$p.hwnd-eq$snapshot.proxy-and$p.class_available-and$p.class-eq'D3DProxyWindow'-and-not$p.class_may_be_truncated) "$label exact live visible own offscreen NOACTIVATE proxy prerequisite"
}
function Capture-VisibleReplacement($t,$replacement,[string]$name){
 $attempts=[Collections.Generic.List[object]]::new();$fresh=$null
 # Finite max3 API calls. A full enumeration miss is a product failure, never retried.
 # Only an explicitly truncated miss may retry after a fresh visible identity witness.
 for($n=0;$n-lt3;$n++){
  $witness=if($n-eq0){$replacement}else{Snapshot $t ([uint64](120+$n))}
  Require-VisibleReplacement $t $witness "$name attempt$n"
  Require ($witness.proxy-eq$replacement.proxy) "$name attempt$n replacement HWND remains exact"
  $fresh=Vi $t @('capture',(Join-Path $run ($name+".replacement.$n.png")),'--method','printwindow')
  $enum=$fresh.reply.capture_diagnostics.enumeration
  $expected=@($enum.candidates|Where-Object {$_.candidate_kind-eq'd3d_proxy'-and(Same-Hwnd $_.hwnd $replacement.proxy)})
  $attempts.Add([pscustomobject]@{attempt=$n;visible_identity=$witness;capture_call=$fresh;expected_candidate_count=$expected.Count;retry_reason=$null})
  # Retain every attempt immediately, including a later failed prerequisite/exception.
  $evidence.Add([pscustomobject]@{name=($name+'.replacement_attempt');attempt=$attempts[$attempts.Count-1]})
  if($fresh.exit-ne0-or-not$fresh.reply.ok-or-not$enum-or$expected.Count-ne0){break}
  if($enum.truncated-isnot[bool]-or-not$enum.truncated){break}
  if($n-lt2){$attempts[$attempts.Count-1].retry_reason='explicit_truncated_enumeration_missing_exact_live_visible_candidate';Start-Sleep -Milliseconds 10}
 }
 # The caller classifies a final truncated result as NOT MEASURED; no added retry.
 return [pscustomobject]@{fresh=$fresh;attempts=@($attempts)}
}

function Not-Measured([string]$name,$enumeration=$null){
 $notMeasured.Add([pscustomobject]@{name=$name;enumeration=$enumeration});Write-Host "NOT MEASURED $name"
}
function Measure-ProxyEnumeration($enumeration,[int]$expected,[uint32]$ownerPid,[uint64]$foreignHwnd,[string]$name){
 Require ($null-ne$enumeration-and$enumeration.truncated-is[bool]-and$enumeration.candidates-is[array]-and$enumeration.stop_reason-is[string]-and$enumeration.stop_reason-and($enumeration.visited-is[int]-or$enumeration.visited-is[long])-and$enumeration.visited-ge0-and$enumeration.visited-le4096-and@($enumeration.candidates).Count-le16) "$name typed bounded enumeration metadata"
 $candidates=@($enumeration.candidates);$proxies=@($candidates|Where-Object candidate_kind -eq d3d_proxy)
 $returnedValid=@($candidates|Where-Object {$_.pid-ne$ownerPid-or(Same-Hwnd $_.hwnd $foreignHwnd)}).Count-eq0
 Check $returnedValid "$name returned candidates exclude foreign PID and foreign lookalike"
 if($enumeration.truncated){
  # An incomplete subset can expose an error, but cannot establish exact count
  # or absence. A missing candidate is not a failed product assertion.
  Check ($proxies.Count-le$expected) "$name truncated subset has no unexpected extra proxy"
  Not-Measured "$name exact candidate count/absence and dependent coverage" $enumeration
  return
 }
 $matches=$proxies.Count-eq$expected
 Check $matches "$name complete enumeration has the expected visible proxy count"
 if($matches-and$returnedValid-and$expected-gt0){$script:completeCandidatePositiveCount++}
}
function Complete-EnumerationCoverage{
 if(-not$completeCandidatePositiveCount){Not-Measured 'no independently complete positive proxy enumeration control'}
}
function Same-Hwnd($value,$number){return $value-and[Convert]::ToUInt64($value.Substring(2),16)-eq[uint64]$number}
try{
 foreach($arch in @('x64','x86')){
  $folder=Join-Path $run "tools\$arch";New-Item -ItemType Directory -Force -Path $folder,(Join-Path $folder 'temp')|Out-Null;$mh=Join-Path $RepoRoot "build\$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'};$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  foreach($object in @('buffer.obj','hook.obj','trampoline.obj',($hde+'.obj'))){if(-not(Test-Path -LiteralPath (Join-Path $mh $object))){throw "missing tested $arch ordinary MinHook object $object"}}
  $build=Join-Path $folder 'build.cmd';@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul
if errorlevel 1 exit /b 1
cd /d "$folder"
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"$RepoRoot\third_party\minhook\include" "$source" /Fe:fixture.exe /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $build -Encoding ascii
  $script:activeCli=Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'});$compiled=Run $env:ComSpec @('/d','/c',$build) 30000;($compiled.stdout+$compiled.stderr)|Set-Content -LiteralPath (Join-Path $folder 'build.log');Require (-not$compiled.timeout-and$compiled.owned_reaped-and-not$compiled.error-and$compiled.startup_cache_valid-and$compiled.cleanup.completion_owned-and-not$compiled.cleanup.error-and(Test-ViExitedCliAccounting $compiled.cleanup.exit_accounting)-and$compiled.exit_known-and$compiled.stdout_complete-and$compiled.stderr_complete-and-not$compiled.stdout_truncated-and-not$compiled.stderr_truncated-and$compiled.exit-eq0-and($compiled.stdout+$compiled.stderr)-notmatch'(?im)\bwarning (?:C|LNK)\d+') "$arch fixture builds W4 WX with no warnings"
  $exe=(Resolve-Path -LiteralPath (Join-Path $folder 'fixture.exe')).Path;$foreign=Start-Target $exe "$arch.foreign.surrogate" 'foreign'
  foreach($mode in @('proxy','none','nonblack','two','hidden','ansi')){
   $name="$arch.$mode.surrogate";$t=Start-Target $exe $name $mode;$inj=Vi $t @('inject','--hooks-only') 8000;Require ($inj.exit-eq0-and-not$inj.timeout-and$inj.reply.ok) "$name bounded hooks-only injection"
   $adopt=Vi $t @('config',('hwnd=0x{0:X}'-f[uint64]$t.ready.hwnd),'isolate=true','fakefocus=true');Require ($adopt.exit-eq0-and$adopt.reply.ok-and(Same-Hwnd $adopt.reply.hwnd $t.ready.hwnd)) "$name adopts exact owned input HWND"
   $arm=Vi $t @('readiness','--wait-ms','1500');Require ($arm.exit-eq0-and$arm.reply.ok-and$arm.reply.queue_ready) "$name queue ready"
   $captionBefore=Snapshot $t 100;$path=Join-Path $run ($name+'.png');$capture=Vi $t @('capture',$path,'--method','printwindow');$captionAfter=Snapshot $t 101;Require ($capture.exit-eq0-and-not$capture.timeout-and$capture.reply.ok-and(Test-Path -LiteralPath $path)) "$name known-source PrintWindow surrogate capture succeeds"
   $surrogateRows=@(Records $t.log|Where-Object {$_.kind-eq'printwindow_surrogate'-and$_.accepted})
   Check ($captionAfter.surrogate_accepted-gt$captionBefore.surrogate_accepted-and$surrogateRows.Count-ge1-and@($surrogateRows|Where-Object {$_.capture_transport-ne'known_source_printwindow_api_surrogate'-or$_.owner_pid-ne$t.process.Id-or$_.window-ne$t.ready.hwnd-or-not$_.owned_live_window-or-not$_.memory_dc-or$_.dc_object_type-ne10-or$_.native_printwindow_called-ne$false}).Count-eq0) "$name deterministic painting occurred only for live owned HWND and memory DC; native PrintWindow was not called"
   Check ($captionBefore.custom_wm_gettext-eq$captionAfter.custom_wm_gettext) "$name capture diagnostics invoke no application WM_GETTEXT callback"
   $pixels=Bitmap-Observation $path;$evidence.Add([pscustomobject]@{name=$name;ready=$t.ready;foreign=$foreign.ready;pixels=$pixels;reply=$capture.reply;capture_transport='known_source_printwindow_api_surrogate';native_printwindow_compatibility_measured=$false;surrogate_callbacks=$surrogateRows})
   Check ($pixels.width-eq320-and$pixels.height-eq240-and$pixels.all_black-eq($mode-ne'nonblack')-and$capture.reply.capture_path-eq'printwindow') "$name actual input image remains unchanged; proxy is not auto-selected"
   if($mode-eq'nonblack'){
    $expected=@(@(20,220,30),@(25,60,230),@(220,35,20),@(25,60,230),@(220,35,20));$known=$pixels.samples.Count-eq5
    for($n=0;$n-lt$pixels.samples.Count;$n++){if($n-ge5-or$pixels.samples[$n].r-ne$expected[$n][0]-or$pixels.samples[$n].g-ne$expected[$n][1]-or$pixels.samples[$n].b-ne$expected[$n][2]-or$pixels.samples[$n].a-ne255){$known=$false}}
    Require $known "$name independently decoded PNG has exact green marker and red/blue checker tiles";$positiveCaseCount++
   }

   $d=$capture.reply.capture_diagnostics;$has=$d-and$d.version-eq1-and$d.available;Check ([bool]$has) "$name capture source diagnostics available"
   if($has){
    $expectedClass=if($mode-eq'ansi'){'GNW95 ANSI Class'}else{'GNW95 Class'};$encoding=if($mode-eq'ansi'){'ansi_default_proc_acp_to_utf16'}else{'unicode_default_proc'}
    Check ((Same-Hwnd $d.input.hwnd $t.ready.hwnd)-and(Same-Hwnd $d.source.hwnd $t.ready.hwnd)-and$d.input.class-eq$expectedClass-and$d.input.title-eq'Synthetic GNW95 input'-and$d.input.title_available-and$d.input.title_encoding-eq$encoding-and$d.input.title_source-eq'default_window_caption_not_custom_wm_gettext'-and$d.input.pid-eq$t.process.Id-and$d.source.pid-eq$t.process.Id-and$d.input.lifetime_and_client_size_stable_at_reply-and$d.input_adoption_stable_at_reply) "$name exact input/source class title client and lifetime metadata"
    $candidates=@($d.enumeration.candidates);$proxies=@($candidates|Where-Object candidate_kind -eq d3d_proxy);$expected=if($mode-eq'two'){2}elseif($mode-eq'none'-or$mode-eq'hidden'){0}else{1}
    Measure-ProxyEnumeration $d.enumeration $expected ([uint32]$t.process.Id) ([uint64]$foreign.ready.proxy) $name
    Check ($d.enumeration.visited-le4096-and$candidates.Count-le16-and$d.enumeration.PSObject.Properties['truncated']-and$d.enumeration.PSObject.Properties['stop_reason']) "$name bounded enumeration includes truncation state"
    Check ($d.observation.available-and$d.observation.all_black_rgb-eq$pixels.all_black-and$d.observation.uniform_rgb-eq$pixels.uniform-and$d.observation.space-eq'full_printwindow_client_image_before_region_and_output_transforms') "$name independent PNG RGB observation agrees without diagnosing cause"
    Check ($d.automatic_proxy_selection-is[bool]-and$d.automatic_proxy_selection-eq$false-and$d.candidate_pixels_probed-is[bool]-and$d.candidate_pixels_probed-eq$false-and$d.transform.input_target_changed-is[bool]-and$d.transform.input_target_changed-eq$false-and$d.transform.policy-eq'input_client_printwindow') "$name no candidate probing retarget or invented proxy transform"
    if($mode-eq'proxy'-and$proxies.Count-eq1){
     $old=$proxies[0];$destroy=Snapshot $t 102 1;Require ($destroy.proxy-eq0) "$name owned proxy destruction acknowledged"
     $gone=Vi $t @('capture',(Join-Path $run ($name+'.destroyed.png')),'--method','printwindow');$goneCandidates=@($gone.reply.capture_diagnostics.enumeration.candidates|Where-Object candidate_kind -eq d3d_proxy)
     Require ($gone.exit-eq0-and$gone.reply.ok) "$name post-destruction capture succeeds"
     Measure-ProxyEnumeration $gone.reply.capture_diagnostics.enumeration 0 ([uint32]$t.process.Id) ([uint64]$foreign.ready.proxy) "$name destroyed candidate"
     $replacement=Snapshot $t 103 2;Require ($replacement.proxy-ne0) "$name owned proxy replacement acknowledged"
     Require-VisibleReplacement $t $replacement "$name replacement"
     $constructor=@(Records $t.log|Where-Object kind -eq replacement_constructor);Require ($constructor.Count-eq1-and$constructor[0].admitted-eq$true-and$constructor[0].native_create_called-eq$true-and$constructor[0].fixed_rectangle_outside_virtual_screen-eq$true-and$constructor[0].noactivate_style-eq$true-and$constructor[0].fixture_explicit_native_show_or_activation_called-eq$false-and$constructor[0].hwnd-eq$replacement.proxy-and$constructor[0].call_count-eq1) "$name exactly one constrained native replacement construction with no explicit native show/activation"
     $replacementCaptures=Capture-VisibleReplacement $t $replacement $name;$fresh=$replacementCaptures.fresh;$freshCandidates=@($fresh.reply.capture_diagnostics.enumeration.candidates|Where-Object candidate_kind -eq d3d_proxy)
     Require ($fresh.exit-eq0-and$fresh.reply.ok) "$name replacement capture succeeds"
     Measure-ProxyEnumeration $fresh.reply.capture_diagnostics.enumeration 1 ([uint32]$t.process.Id) ([uint64]$foreign.ready.proxy) "$name replacement"
     # Validate each returned positive even when the rest of enumeration is
     # truncated. Do not turn an explicitly truncated miss into a false failure.
     if($freshCandidates.Count-or-not$fresh.reply.capture_diagnostics.enumeration.truncated){
     Check ($fresh.exit-eq0-and$fresh.reply.ok-and$freshCandidates.Count-eq1-and(Same-Hwnd $freshCandidates[0].hwnd $replacement.proxy)-and$freshCandidates[0].lifetime_cookie-ne$old.lifetime_cookie-and$freshCandidates[0].lifetime_and_client_size_stable_at_reply-and(Same-Hwnd $fresh.reply.capture_diagnostics.input.hwnd $t.ready.hwnd)) "$name replacement has a fresh lifetime witness even if numeric HWND is reused"
     }
     $evidence.Add([pscustomobject]@{name=($name+'.replacement');old=$old;destroy=$destroy;gone=$gone.reply;replacement=$replacement;fresh=$fresh.reply;replacement_capture_attempts=$replacementCaptures.attempts;numeric_hwnd_reused=(Same-Hwnd $old.hwnd $replacement.proxy);capture_transport='known_source_printwindow_api_surrogate';native_printwindow_compatibility_measured=$false})
    }
   }
   if($mode-eq'nonblack'){
    $captionBeforeCrop=Snapshot $t 110;$cropPath=Join-Path $run ($name+'.tiny_red.png')
    $payload=@{cmd='capture';path=$cropPath;method='printwindow';region='0,0,2,2';scale=0.5}|ConvertTo-Json -Compress
    $tiny=Vi $t @('json',$payload);$captionAfterCrop=Snapshot $t 111
    Require ($tiny.exit-eq0-and$tiny.reply.ok-and(Test-Path -LiteralPath $cropPath)) "$name tiny requested source crop/resize succeeds"
    $b=[Drawing.Bitmap]::new($cropPath);try{$c=$b.GetPixel(0,0);$tinyPixel=@{width=$b.Width;height=$b.Height;r=[int]$c.R;g=[int]$c.G;b=[int]$c.B;a=[int]$c.A}}finally{$b.Dispose()}
    Check ($tinyPixel.width-eq1-and$tinyPixel.height-eq1-and$tiny.reply.w-eq1-and$tiny.reply.h-eq1-and$tinyPixel.r-eq220-and$tinyPixel.g-eq35-and$tinyPixel.b-eq20-and$tinyPixel.a-eq255) "$name independently decoded output crop is one exact red pixel"
    $obs=$tiny.reply.capture_diagnostics.observation
    Check ($obs.available-eq$true-and$obs.uniform_rgb-eq$false-and$obs.all_black_rgb-eq$false-and$obs.space-eq'full_printwindow_client_image_before_region_and_output_transforms'-and$tiny.reply.capture_diagnostics.transform.space-eq'printwindow_source_to_full_input_client_before_region_and_output_transforms'-and$tiny.reply.capture_diagnostics.transform.output_w-eq320-and$tiny.reply.capture_diagnostics.transform.output_h-eq240) "$name RGB diagnostic remains full nonuniform client image before uniform output crop"
    Check ($captionBeforeCrop.custom_wm_gettext-eq$captionAfterCrop.custom_wm_gettext) "$name cropped diagnostic adds no custom WM_GETTEXT callback"
    $evidence.Add([pscustomobject]@{name=($name+'.tiny_red');pixel=$tinyPixel;reply=$tiny.reply;capture_transport='known_source_printwindow_api_surrogate';native_printwindow_compatibility_measured=$false})
   }
   $s=Vi $t @('status');Check ($s.exit-eq0-and$s.reply.ok-and(Same-Hwnd $s.reply.hwnd $t.ready.hwnd)-and(Foreground-Pid)-ne$t.process.Id) "$name input target remains unchanged and actual background"
   Stop-Owned $t
  }
  Stop-Owned $foreign
 }
}catch{$exception=$_.Exception.Message;Write-Host "ERROR $exception"}finally{
 foreach($t in $owned){try{Stop-Owned $t}catch{$checks.Add([pscustomobject]@{pass=$false;name="owned cleanup: $($_.Exception.Message)"})}}
 $failed=@($checks|Where-Object {-not$_.pass}).Count;if(-not$exception-and-not$failed){Complete-EnumerationCoverage};$nmCount=$notMeasured.Count
 $status=if($exception-or$failed){'failed'}elseif($nmCount){'not_measured'}else{'passed'}
 $result=[ordered]@{schema=1;status=$status;failed_check_count=$failed;not_measured_check_count=$nmCount;positive_case_count=$positiveCaseCount;complete_candidate_positive_case_count=$completeCandidatePositiveCount;not_measured_reason=$(if($nmCount){'capture_enumeration_incomplete'}else{$null});not_measured_checks=@($notMeasured);completed=($null-eq$exception);passed=$checks.Count-$failed;failed=$failed;error=$exception;checks=$checks;owned_processes=@($owned|ForEach-Object{@{name=$_.name;pid=$_.pid;exe=$_.exe;creation_utc_ticks=$_.created;startup_identity=$_.startup_identity;cleanup=$_.stop}});native_fixture_process_run=$true;capture_transport='known_source_printwindow_api_surrogate';native_printwindow_compatibility_measured=$false;native_printwindow_compatibility='UNKNOWN; preserved original native baseline112 PASS/14 FAIL';source_sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;original_native_fixture_sha256='F3560591BC9D1A5D7D12415C8ACEAA0DDECE7C7CB1850D3B69BACC96F0E0CB4F';original_native_runner_sha256='2CDF43DCB210FE2153B3008CF1303CEC06CF48DCD5F370A66F7084FB5C378AA5';physical_input_generated=$false;foreground_activation_requested=$false;real_game_measured=$false}
 $result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $run 'result.json');$calls|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'calls.json');$evidence|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'evidence.json');Write-Host "$($result.passed) PASS / $failed FAIL; $run"
}
if($exception-or$failed){exit 1};if($nmCount){exit 3};exit 0
