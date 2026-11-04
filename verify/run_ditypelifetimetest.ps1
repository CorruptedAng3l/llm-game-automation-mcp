# Same synthetic COM address changes from proven-other to keyboard and back.
# New scoped derivative of current reader fixture; no native driver sees fake self.
param([string]$RepoRoot='', [string]$Vinput='', [string]$Out="$PSScriptRoot\..\build\test\di_type_lifetime\after")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
if(-not$RepoRoot){$candidate=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($n=0;$n-lt6-and$candidate;$n++){if(Test-Path (Join-Path $candidate.FullName 'build\x64\host_mh\buffer.obj')){$RepoRoot=$candidate.FullName;break};$candidate=$candidate.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot for the product checkout containing prebuilt ordinary MinHook objects'}
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;if(-not$Vinput){$Vinput=Join-Path $RepoRoot 'dist\vinput.exe'};$V=(Resolve-Path -LiteralPath $Vinput).Path
$source=(Resolve-Path -LiteralPath "$PSScriptRoot\..\test\vi_physical_readers.cpp").Path
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $run|Out-Null;$run=(Resolve-Path -LiteralPath $run).Path
$checks=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new();$complete=$false;$exception=$null
if(-not('ViDiTypeLifetimeNative' -as [type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViDiTypeLifetimeNative {
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
}
'@}
function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{pass=$ok;name=$name});Write-Host "$(if($ok){'PASS'}else{'FAIL'}) $name"}
function Require([bool]$ok,[string]$name){Check $ok $name;if(-not$ok){throw $name}}
function Rows([string]$path){if(-not(Test-Path -LiteralPath $path)){return};foreach($line in (Get-Content -LiteralPath $path)){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Await($target,[string]$kind,[uint64]$token=0){$until=[Environment]::TickCount64+3000;do{foreach($row in @(Rows $target.log)){if($row.kind-eq$kind-and($token-eq0-or$row.token-eq$token)){return $row}};if($target.process.HasExited){throw ('owned fixture exited code0x'+[BitConverter]::ToUInt32([BitConverter]::GetBytes([int32]$target.process.ExitCode),0).ToString('X8'))};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until);throw "missing $kind token$token"}
function Live-Owned($identity){try{$p=Get-Process -Id $identity.process.Id -ErrorAction SilentlyContinue;if($p-and$p.Path-eq$identity.path-and$p.StartTime.ToUniversalTime().Ticks-eq$identity.created){return $p}}catch{};return $null}
function Stop-Owned($identity){$p=Live-Owned $identity;if($p){try{Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction Stop}catch{if(Live-Owned $identity){throw}}}}
function Run-Bounded([string]$file,[string[]]$arguments,[int]$budget=5000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$file;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $arguments){[void]$info.ArgumentList.Add($argument)};$p=[Diagnostics.Process]::new();$p.StartInfo=$info;if(-not$p.Start()){throw 'owned process start failed'}
 $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$timeout=-not$p.WaitForExit($budget);if($timeout-and-not$p.HasExited){$p.Kill();[void]$p.WaitForExit(1000)}
 $output=if($stdout.Wait(1000)){$stdout.Result}else{'<stdout unavailable>'};$errorText=if($stderr.Wait(1000)){$stderr.Result}else{'<stderr unavailable>'};$exit=if($p.HasExited){$p.ExitCode}else{-1};$p.Dispose()
 return [pscustomobject]@{exit=$exit;stdout=$output;stderr=$errorText;timeout=$timeout}
}
function Vi($target,[string[]]$arguments,[int]$budget=5000){$r=Run-Bounded $V (@('--json','--pid',"$($target.process.Id)")+$arguments) $budget;$reply=$null;$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
 $call=[pscustomobject]@{pid=$target.process.Id;arguments=$arguments;exit=$r.exit;reply=$reply;stdout=$r.stdout;stderr=$r.stderr;timeout=$r.timeout};$calls.Add($call);return $call
}
function Post($target,[uint32]$message,[uint32]$value){[uint32]$pidValue=0;$thread=[ViDiTypeLifetimeNative]::GetWindowThreadProcessId($target.window,[ref]$pidValue);if(-not$thread-or$pidValue-ne$target.process.Id-or-not[ViDiTypeLifetimeNative]::IsWindow($target.window)){throw 'fixture HWND/PID identity changed'};if(-not[ViDiTypeLifetimeNative]::PostMessageW($target.window,$message,[UIntPtr]::new($value),[IntPtr]::Zero)){throw 'owned fixture post failed'}}
function Foreground(){[uint32]$p=0;$window=[ViDiTypeLifetimeNative]::GetForegroundWindow();[void][ViDiTypeLifetimeNative]::GetWindowThreadProcessId($window,[ref]$p);return [pscustomobject]@{hwnd=$window.ToInt64();pid=$p}}
try {
 foreach($arch in @('x64','x86')){
  $folder=Join-Path $run "tools\$arch";New-Item -ItemType Directory -Force -Path $folder,(Join-Path $folder 'temp')|Out-Null
  $mh=Join-Path $RepoRoot "build\$arch\host_mh";$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
  foreach($object in @('buffer.obj','hook.obj','trampoline.obj',($hde+'.obj'))){if(-not(Test-Path (Join-Path $mh $object))){throw "missing $arch ordinary MinHook object $object"}}
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
  $built=Run-Bounded $env:ComSpec @('/d','/c',$build) 30000;($built.stdout+$built.stderr)|Set-Content (Join-Path $folder 'build.log');Require (-not$built.timeout-and$built.exit-eq0-and($built.stdout+$built.stderr)-notmatch'(?im)\bwarning (?:C|LNK)\d+') "$arch fixture compiles /W4 /WX without warnings"
 }
 foreach($arch in @('x64','x86')){
  $exe=(Resolve-Path (Join-Path $run "tools\$arch\fixture.exe")).Path;$log=Join-Path $run ($arch+'.jsonl');$start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$exe;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;[void]$start.ArgumentList.Add($log)
  $process=[Diagnostics.Process]::Start($start);$identity=[pscustomobject]@{process=$process;path=$exe;created=$process.StartTime.ToUniversalTime().Ticks};$owned.Add($identity);$target=[pscustomobject]@{process=$process;log=$log;window=[IntPtr]::Zero}
  try {
   $ready=Await $target ready;Require ($ready.pid-eq$process.Id-and$ready.foreground_spies_terminal-and$ready.write_spies_terminal-and$ready.di_type_lifetime_message-eq(0x8000+0x69)) "$arch safe fixture ready with lifetime control"
   $target.window=[IntPtr]::new([long][uint64]$ready.hwnd);$foregroundBefore=Foreground;Require ($foregroundBefore.pid-ne$process.Id) "$arch actual foreground outside fixture"
   $inject=Vi $target @('inject','--hooks-only') 8000;Require (-not$inject.timeout-and$inject.exit-eq0-and$inject.reply.ok) "$arch hooks-only injection succeeds"
   $config=Vi $target @('config',('hwnd=0x{0:X}'-f[uint64]$ready.hwnd),'isolate=true','fakefocus=true');Require ($config.exit-eq0-and$config.reply.ok-and[Convert]::ToUInt64($config.reply.hwnd.Substring(2),16)-eq[uint64]$ready.hwnd) "$arch adopts exact owned tool HWND"
   $readiness=Vi $target @('readiness','--wait-ms','1500');Require ($readiness.exit-eq0-and$readiness.reply.ok-and$readiness.reply.hook_ready-and$readiness.reply.window_ready-and$readiness.reply.queue_ready) "$arch fresh queue ACK before synthetic reader control"
   Post $target (0x8000+0x69) 6901;$probe=Await $target di_type_lifetime 6901
   Require ($probe.entry_preflight_ok-and$probe.synthetic_self_only-and$probe.same_address-and$probe.self_before-eq$probe.self_after-and$probe.model-eq0-and$probe.native_enabled) "$arch exact same-address lifetime control is isolated with fabricated native source"
   Check ($probe.other_hr-eq0-and$probe.other_count-eq1-and$probe.other_preserved) "$arch initial proven-other event passes through"
   Check ($probe.keyboard_state_hr-eq0-and$probe.keyboard_b-eq0) "$arch reused address now keyboard masks fabricated physical state"
   Check ($probe.keyboard_buffer_hr-eq0-and$probe.keyboard_count-eq0-and-not$probe.keyboard_physical_bytes) "$arch reused address now keyboard masks fabricated physical buffered event"
   Check ($probe.caps_after_other-ge1-and$probe.caps_after_keyboard-ge$probe.caps_after_other+2-and$probe.caps_after_other_again-gt$probe.caps_after_keyboard) "$arch every current object reader rechecks capabilities across address reuse"
   Check ($probe.other_again_hr-eq0-and$probe.other_again_count-eq1-and$probe.other_again_preserved) "$arch same address changed back to proven-other still preserves its event"
   $status=Vi $target @('status');$foregroundAfter=Foreground;Check ($status.reply.ok-and$status.reply.isolate-and-not$status.reply.really_foreground-and$foregroundAfter.pid-ne$process.Id) "$arch remains isolated and physically background"
   $evidence.Add([pscustomobject]@{arch=$arch;ready=$ready;probe=$probe;status=$status;foreground_before=$foregroundBefore;foreground_after=$foregroundAfter;log=$log})
  } catch {$evidence.Add([pscustomobject]@{arch=$arch;error=$_.Exception.Message;exit_code=$(if($process.HasExited){$process.ExitCode}else{$null});log=$log;last_records=@(Rows $log|Select-Object -Last 5)});if(-not$checks.Count-or$checks[$checks.Count-1].pass){Check $false "$arch runner error: $($_.Exception.Message)"}}
  finally {Stop-Owned $identity}
 }
 $complete=$true
} catch {$exception=$_.Exception.Message;if(-not$checks.Count-or$checks[$checks.Count-1].pass){Check $false ('runner error: '+$exception)}}
finally {
 foreach($identity in $owned){Stop-Owned $identity};$failed=@($checks|Where-Object {-not$_.pass}).Count
 $checks|ConvertTo-Json -Depth 10|Set-Content (Join-Path $run 'checks.json');$calls|ConvertTo-Json -Depth 24|Set-Content (Join-Path $run 'calls.json');$evidence|ConvertTo-Json -Depth 24|Set-Content (Join-Path $run 'evidence.json')
 [pscustomobject]@{pass=$checks.Count-$failed;fail=$failed;complete=$complete;expected_red=$false;exception=$exception;out_dir=$run;source=$source;source_sha256=(Get-FileHash $source -Algorithm SHA256).Hash;cli=$V;cli_sha256=(Get-FileHash $V -Algorithm SHA256).Hash;scope='synthetic COM address reuse; no native driver fake self, no input generation'}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $run 'result.json')
}
Write-Host "$($checks.Count-$failed) PASS / $failed FAIL ($run)";if($failed){exit 1};exit 0
