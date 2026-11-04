# Synthetic GDI source diagnostics; no expected-red inversion or game execution.
param([string]$CliDir='', [string]$RepoRoot='', [string]$Out="$PSScriptRoot\..\build\test\capture_diagnostics\after")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
if(-not$RepoRoot){$candidate=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($n=0;$n-lt8-and$candidate;$n++){if((Test-Path (Join-Path $candidate.FullName 'src\vinput_core.cpp'))-and(Test-Path (Join-Path $candidate.FullName 'build\x64\host_mh\buffer.obj'))){$RepoRoot=$candidate.FullName;break};$candidate=$candidate.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot for tested prebuilt host_mh objects'}
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$V=(Resolve-Path -LiteralPath (Join-Path $CliDir 'vinput.exe')).Path
$source=(Resolve-Path -LiteralPath "$PSScriptRoot\..\test\vi_capture_diagnostics.cpp").Path
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force -Path $run|Out-Null;$run=(Resolve-Path -LiteralPath $run).Path
$checks=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new();$exception=$null
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
function Live-Owned($t){try{$p=Get-Process -Id $t.process.Id -ErrorAction SilentlyContinue;if($p-and$p.Path-eq$t.path-and$p.StartTime.ToUniversalTime().Ticks-eq$t.created){return $p}}catch{};return $null}
function Stop-Owned($t){$p=Live-Owned $t;if($p){Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction Stop}}
function Run-Bounded([string]$file,[string[]]$arguments,[int]$budget=5000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$file;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $arguments){[void]$info.ArgumentList.Add($argument)};$p=[Diagnostics.Process]::new();$p.StartInfo=$info;if(-not$p.Start()){throw 'owned CLI/compiler start failed'}
 $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$expired=-not$p.WaitForExit($budget);if($expired-and-not$p.HasExited){$p.Kill();[void]$p.WaitForExit(1000)}
 $o=if($stdout.Wait(1000)){$stdout.Result}else{'<unavailable>'};$e=if($stderr.Wait(1000)){$stderr.Result}else{'<unavailable>'};$code=if($p.HasExited){$p.ExitCode}else{-1};$p.Dispose();return [pscustomobject]@{stdout=$o;stderr=$e;exit=$code;timeout=$expired}
}
function Vi($t,[string[]]$arguments,[int]$budget=5000){$r=Run-Bounded $V (@('--json','--pid',"$($t.process.Id)")+$arguments) $budget;$lines=@($r.stdout-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}};$item=[pscustomobject]@{pid=$t.process.Id;arguments=$arguments;exit=$r.exit;timeout=$r.timeout;stdout=$r.stdout;stderr=$r.stderr;reply=$reply};$calls.Add($item);return $item}
function Foreground-Pid{[uint32]$p=0;[void][ViCaptureDiagNative]::GetWindowThreadProcessId([ViCaptureDiagNative]::GetForegroundWindow(),[ref]$p);return $p}
function Snapshot($t,[uint64]$token,[uint64]$action=0){
 if(-not(Live-Owned $t)-or$t.process.HasExited){throw 'owned fixture identity ended before control post'}
 [uint32]$p=0;$h=[IntPtr]::new([long][uint64]$t.ready.hwnd);if(-not[ViCaptureDiagNative]::GetWindowThreadProcessId($h,[ref]$p)-or$p-ne$t.process.Id){throw 'owned control HWND identity changed'}
 if(-not[ViCaptureDiagNative]::PostMessageW($h,0x8090,[UIntPtr]::new($action),[IntPtr]::new([long]$token))){throw 'owned control PostMessage refused'}
 $until=[Environment]::TickCount64+2500;do{$record=@(Records $t.log|Where-Object {$_.kind-eq'snapshot'-and$_.token-eq$token})|Select-Object -Last 1;if($record){return $record};if($t.process.HasExited){throw "owned fixture exited$($t.process.ExitCode) awaiting snapshot$token"};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "owned snapshot$token unavailable"
}
function Start-Target([string]$exe,[string]$name,[string]$mode){
 $log=Join-Path $run ($name+'.jsonl');$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';[void]$info.ArgumentList.Add($log);[void]$info.ArgumentList.Add($mode)
 $p=[Diagnostics.Process]::Start($info);$t=[pscustomobject]@{process=$p;path=$exe;created=$p.StartTime.ToUniversalTime().Ticks;log=$log;ready=$null;name=$name};$owned.Add($t)
 $until=[Environment]::TickCount64+3000;do{$t.ready=@(Records $log|Where-Object kind -eq ready)|Select-Object -First 1;if($t.ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 Require ($t.ready-and$t.ready.pid-eq$p.Id-and$t.ready.terminal_spies-and$t.ready.api_entries_verified-and$t.ready.no_activation_or_physical_input) "$name terminal safety spies and owned ready HWND"
 Require ((Foreground-Pid)-ne$p.Id) "$name actual foreground outside fixture";return $t
}
function Bitmap-Observation([string]$path){$b=[Drawing.Bitmap]::new($path);try{$black=$true;$uniform=$true;$first=$b.GetPixel(0,0);for($y=0;$y-lt$b.Height;$y++){for($x=0;$x-lt$b.Width;$x++){$p=$b.GetPixel($x,$y);if($p.R-or$p.G-or$p.B){$black=$false};if($p.R-ne$first.R-or$p.G-ne$first.G-or$p.B-ne$first.B){$uniform=$false}}};return [pscustomobject]@{width=$b.Width;height=$b.Height;all_black=$black;uniform=$uniform}}finally{$b.Dispose()}}
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
  $compiled=Run-Bounded $env:ComSpec @('/d','/c',$build) 30000;($compiled.stdout+$compiled.stderr)|Set-Content -LiteralPath (Join-Path $folder 'build.log');Require (-not$compiled.timeout-and$compiled.exit-eq0-and($compiled.stdout+$compiled.stderr)-notmatch'(?im)\bwarning (?:C|LNK)\d+') "$arch fixture builds W4 WX with no warnings"
  $exe=(Resolve-Path -LiteralPath (Join-Path $folder 'fixture.exe')).Path;$foreign=Start-Target $exe "$arch.foreign" 'foreign'
  foreach($mode in @('proxy','none','nonblack','two','hidden','ansi')){
   $name="$arch.$mode";$t=Start-Target $exe $name $mode;$inj=Vi $t @('inject','--hooks-only') 8000;Require ($inj.exit-eq0-and-not$inj.timeout-and$inj.reply.ok) "$name bounded hooks-only injection"
   $adopt=Vi $t @('config',('hwnd=0x{0:X}'-f[uint64]$t.ready.hwnd),'isolate=true','fakefocus=true');Require ($adopt.exit-eq0-and$adopt.reply.ok-and(Same-Hwnd $adopt.reply.hwnd $t.ready.hwnd)) "$name adopts exact owned input HWND"
   $arm=Vi $t @('readiness','--wait-ms','1500');Require ($arm.exit-eq0-and$arm.reply.ok-and$arm.reply.queue_ready) "$name queue ready"
   $captionBefore=Snapshot $t 100;$path=Join-Path $run ($name+'.png');$capture=Vi $t @('capture',$path,'--method','printwindow');$captionAfter=Snapshot $t 101;Require ($capture.exit-eq0-and-not$capture.timeout-and$capture.reply.ok-and(Test-Path -LiteralPath $path)) "$name actual PrintWindow capture succeeds"
   Check ($captionBefore.custom_wm_gettext-eq$captionAfter.custom_wm_gettext) "$name capture diagnostics invoke no application WM_GETTEXT callback"
   $pixels=Bitmap-Observation $path;$evidence.Add([pscustomobject]@{name=$name;ready=$t.ready;foreign=$foreign.ready;pixels=$pixels;reply=$capture.reply})
   Check ($pixels.width-eq320-and$pixels.height-eq240-and$pixels.all_black-eq($mode-ne'nonblack')-and$capture.reply.capture_path-eq'printwindow') "$name actual input image remains unchanged; proxy is not auto-selected"
   $d=$capture.reply.capture_diagnostics;$has=$d-and$d.version-eq1-and$d.available;Check ([bool]$has) "$name capture source diagnostics available"
   if($has){
    $expectedClass=if($mode-eq'ansi'){'GNW95 ANSI Class'}else{'GNW95 Class'};$encoding=if($mode-eq'ansi'){'ansi_default_proc_acp_to_utf16'}else{'unicode_default_proc'}
    Check ((Same-Hwnd $d.input.hwnd $t.ready.hwnd)-and(Same-Hwnd $d.source.hwnd $t.ready.hwnd)-and$d.input.class-eq$expectedClass-and$d.input.title-eq'Synthetic GNW95 input'-and$d.input.title_available-and$d.input.title_encoding-eq$encoding-and$d.input.title_source-eq'default_window_caption_not_custom_wm_gettext'-and$d.input.pid-eq$t.process.Id-and$d.source.pid-eq$t.process.Id-and$d.input.lifetime_and_client_size_stable_at_reply-and$d.input_adoption_stable_at_reply) "$name exact input/source class title client and lifetime metadata"
    $candidates=@($d.enumeration.candidates);$proxies=@($candidates|Where-Object candidate_kind -eq d3d_proxy);$expected=if($mode-eq'two'){2}elseif($mode-eq'none'-or$mode-eq'hidden'){0}else{1}
    Check (@($candidates|Where-Object {$_.pid-ne$t.process.Id-or(Same-Hwnd $_.hwnd $foreign.ready.proxy)}).Count-eq0-and$proxies.Count-eq$expected) "$name same-PID visible proxy candidates exclude foreign and hidden lookalikes"
    Check ($d.enumeration.visited-le4096-and$candidates.Count-le16-and$d.enumeration.PSObject.Properties['truncated']-and$d.enumeration.PSObject.Properties['stop_reason']) "$name bounded enumeration includes truncation state"
    Check ($d.observation.available-and$d.observation.all_black_rgb-eq$pixels.all_black-and$d.observation.uniform_rgb-eq$pixels.uniform-and$d.observation.space-eq'full_printwindow_client_image_before_region_and_output_transforms') "$name independent PNG RGB observation agrees without diagnosing cause"
    Check (-not$d.automatic_proxy_selection-and-not$d.candidate_pixels_probed-and-not$d.transform.input_target_changed-and$d.transform.policy-eq'input_client_printwindow') "$name no candidate probing retarget or invented proxy transform"
    if($mode-eq'proxy'-and$proxies.Count-eq1){
     $old=$proxies[0];$destroy=Snapshot $t 102 1;Require ($destroy.proxy-eq0) "$name owned proxy destruction acknowledged"
     $gone=Vi $t @('capture',(Join-Path $run ($name+'.destroyed.png')),'--method','printwindow');$goneCandidates=@($gone.reply.capture_diagnostics.enumeration.candidates|Where-Object candidate_kind -eq d3d_proxy)
     Check ($gone.exit-eq0-and$gone.reply.ok-and$goneCandidates.Count-eq0) "$name destroyed candidate is absent without input retarget"
     $replacement=Snapshot $t 103 2;Require ($replacement.proxy-ne0) "$name owned proxy replacement acknowledged"
     $fresh=Vi $t @('capture',(Join-Path $run ($name+'.replacement.png')),'--method','printwindow');$freshCandidates=@($fresh.reply.capture_diagnostics.enumeration.candidates|Where-Object candidate_kind -eq d3d_proxy)
     Check ($fresh.exit-eq0-and$fresh.reply.ok-and$freshCandidates.Count-eq1-and(Same-Hwnd $freshCandidates[0].hwnd $replacement.proxy)-and$freshCandidates[0].lifetime_cookie-ne$old.lifetime_cookie-and$freshCandidates[0].lifetime_and_client_size_stable_at_reply-and(Same-Hwnd $fresh.reply.capture_diagnostics.input.hwnd $t.ready.hwnd)) "$name replacement has a fresh lifetime witness even if numeric HWND is reused"
     $evidence.Add([pscustomobject]@{name=($name+'.replacement');old=$old;destroy=$destroy;gone=$gone.reply;replacement=$replacement;fresh=$fresh.reply;numeric_hwnd_reused=(Same-Hwnd $old.hwnd $replacement.proxy)})
    }
   }
   $s=Vi $t @('status');Check ($s.exit-eq0-and$s.reply.ok-and(Same-Hwnd $s.reply.hwnd $t.ready.hwnd)-and(Foreground-Pid)-ne$t.process.Id) "$name input target remains unchanged and actual background"
   Stop-Owned $t
  }
  Stop-Owned $foreign
 }
}catch{$exception=$_.Exception.Message;Write-Host "ERROR $exception"}finally{
 foreach($t in $owned){try{Stop-Owned $t}catch{$checks.Add([pscustomobject]@{pass=$false;name="owned cleanup: $($_.Exception.Message)"})}}
 $failed=@($checks|Where-Object {-not$_.pass}).Count;$result=[ordered]@{completed=($null-eq$exception);passed=$checks.Count-$failed;failed=$failed;error=$exception;checks=$checks;native_test_run=$true;physical_input_generated=$false;foreground_activation_requested=$false;real_game_measured=$false}
 $result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $run 'result.json');$calls|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'calls.json');$evidence|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'evidence.json');Write-Host "$($result.passed) PASS / $failed FAIL; $run"
}
if($exception-or$failed){exit 1};exit 0
