# Actual standard TranslateMessage pump. No expected-red inversion or product helper calls.
param([string]$CliDir='', [string]$RepoRoot='', [string]$Out="$PSScriptRoot\..\build\test\char_translation\after")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
if(-not$RepoRoot){$candidate=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($n=0;$n-lt6-and$candidate;$n++){if(Test-Path (Join-Path $candidate.FullName 'src\vinput_core.cpp')){$RepoRoot=$candidate.FullName;break};$candidate=$candidate.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot for the product checkout and prebuilt host_mh objects'}
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'}
$V=(Resolve-Path -LiteralPath (Join-Path $CliDir 'vinput.exe')).Path
$source=(Resolve-Path -LiteralPath "$PSScriptRoot\..\test\vi_chartranslate.cpp").Path;$header=(Resolve-Path -LiteralPath "$PSScriptRoot\..\test\vi_char_spies.h").Path
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force -Path $run|Out-Null;$run=(Resolve-Path -LiteralPath $run).Path
$tools=Join-Path $run 'tools';New-Item -ItemType Directory -Path $tools|Out-Null
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new();$exception=$null
if(-not('ViCharTranslateNative' -as [type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViCharTranslateNative {
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
}
'@}
function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{pass=$ok;name=$name});Write-Host "$(if($ok){'PASS'}else{'FAIL'}) $name"}
function Require([bool]$ok,[string]$name){Check $ok $name;if(-not$ok){throw $name}}
function Text([string]$path){if(-not(Test-Path -LiteralPath $path)){return ''};$stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete);$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}}
function Records([string]$path){foreach($line in ((Text $path)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Await($target,[string]$kind,[uint64]$token,[int]$budget=2500){$until=[Environment]::TickCount64+$budget;do{foreach($record in @(Records $target.log)){if($record.kind-eq$kind-and$record.token-eq$token){return $record}};if($target.process.HasExited){throw "owned fixture exited$($target.process.ExitCode) waiting for $kind token$token"};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "missing $kind token$token"}
function Ptr($value){return [IntPtr]::new([long][uint64]$value)}
function Post($target,[uint32]$message,[uint64]$w=0,[IntPtr]$l=[IntPtr]::Zero){if($target.process.HasExited){throw 'owned fixture exited before control post'};[uint32]$pidValue=0;$thread=[ViCharTranslateNative]::GetWindowThreadProcessId($target.window,[ref]$pidValue);if(-not$thread-or$pidValue-ne$target.process.Id-or-not[ViCharTranslateNative]::IsWindow($target.window)){throw 'owned HWND/PID changed'};if(-not[ViCharTranslateNative]::PostMessageW($target.window,$message,[UIntPtr]::new($w),$l)){throw "owned PostMessage failed $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}}
function Live-Owned($identity){try{$p=Get-Process -Id $identity.process.Id -ErrorAction SilentlyContinue;if($p-and$p.Path-eq$identity.path-and$p.StartTime.ToUniversalTime().Ticks-eq$identity.created){return $p}}catch{};return $null}
function Stop-Owned($identity){$p=Live-Owned $identity;if($p){try{Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction Stop}catch{if(Live-Owned $identity){throw}}}}
function Run-Bounded([string]$file,[string[]]$arguments,[int]$budget=4000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$file;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $arguments){[void]$info.ArgumentList.Add($argument)};$p=[Diagnostics.Process]::new();$p.StartInfo=$info;$watch=[Diagnostics.Stopwatch]::StartNew();if(-not$p.Start()){throw 'owned process start failed'}
 $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$expired=-not$p.WaitForExit($budget);if($expired-and-not$p.HasExited){$p.Kill();[void]$p.WaitForExit(1000)}
 $outText=if($stdout.Wait(1000)){$stdout.Result}else{'<stdout unavailable>'};$errText=if($stderr.Wait(1000)){$stderr.Result}else{'<stderr unavailable>'};$exit=if($p.HasExited){$p.ExitCode}else{-1};$watch.Stop();$p.Dispose()
 return [pscustomobject]@{exit=$exit;stdout=$outText;stderr=$errText;timeout=$expired;elapsed_ms=$watch.ElapsedMilliseconds}
}
function Vi($target,[string[]]$arguments,[int]$budget=4000){$result=Run-Bounded $V (@('--json','--pid',"$($target.process.Id)")+$arguments) $budget;$reply=$null;$lines=@($result.stdout-split'\r?\n'|Where-Object{$_-match'\S'});if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
 $record=[pscustomobject]@{pid=$target.process.Id;arguments=$arguments;exit=$result.exit;reply=$reply;stdout=$result.stdout;stderr=$result.stderr;timeout=$result.timeout;elapsed_ms=$result.elapsed_ms;one_json=($lines.Count-eq1-and$null-ne$reply)};$calls.Add($record);return $record
}
function Producer($target,[string[]]$arguments){$r=Vi $target $arguments;Require (-not$r.timeout-and$r.one_json-and$r.exit-eq0-and$r.reply.ok) "$($target.name) producer exit0 and ok: $($arguments-join' ')";return $r}
function Begin($target,[uint64]$token){Start-Sleep -Milliseconds 20;Post $target (0x8000+0x70) $token;[void](Await $target begin $token)}
function Snapshot($target,[uint64]$token){Post $target (0x8000+0x71) $token;return Await $target snapshot $token}
function Wait-Chars($target,[uint64]$token,[int]$expected,[int]$budget=650){$until=[Environment]::TickCount64+$budget;do{$events=@(Records $target.log|Where-Object {$_.kind-eq'input'-and$_.token-eq$token-and$_.message-eq0x102});if($events.Count-ge$expected){break};if($target.process.HasExited){throw 'owned fixture exited during character witness'};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)}
function Expected($snapshot,[int[]]$codes){return ($snapshot.chars-join',')-eq($codes-join',')-and@($snapshot.syschars).Count-eq0}
function Actual-Foreground(){[uint32]$p=0;$h=[ViCharTranslateNative]::GetForegroundWindow();[void][ViCharTranslateNative]::GetWindowThreadProcessId($h,[ref]$p);return [pscustomobject]@{hwnd=$h.ToInt64();pid=$p}}
function Pause($target,[uint64]$token){Post $target (0x8000+0x72) $token;$ack=Await $target paused $token;Require $ack.ok "$($target.name) own UI pause acknowledged";return $ack}
function Resume($target,[uint64]$token){$event=[Threading.EventWaitHandle]::OpenExisting($target.resume);try{[void]$event.Set()}finally{$event.Dispose()};$ack=Await $target resumed $token;Check (-not$ack.timed_out-and$ack.wait_result-eq0) "$($target.name) pause resumes by owned event within deadline";return $ack}
try {
 foreach($arch in @('x64','x86')){
  $folder=Join-Path $tools $arch;New-Item -ItemType Directory -Force -Path $folder,(Join-Path $folder 'temp')|Out-Null;$mh=Join-Path $RepoRoot "build\$arch\host_mh"
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
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
  $r=Run-Bounded $env:ComSpec @('/d','/c',$build) 30000;($r.stdout+$r.stderr)|Set-Content -LiteralPath (Join-Path $folder 'build.log');Require (-not$r.timeout-and$r.exit-eq0-and($r.stdout+$r.stderr)-notmatch'(?im)\bwarning (?:C|LNK)\d+') "$arch fixture builds /W4 /WX without warnings"
 }
 foreach($arch in @('x64','x86')){foreach($pump in @('w','a')){
  $name="$arch.$pump";$log=Join-Path $run ($name+'.jsonl');$exe=(Resolve-Path (Join-Path $tools "$arch\fixture.exe")).Path;$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  [void]$info.ArgumentList.Add($log);[void]$info.ArgumentList.Add($pump);$process=[Diagnostics.Process]::Start($info);$identity=[pscustomobject]@{process=$process;path=$exe;created=$process.StartTime.ToUniversalTime().Ticks};$owned.Add($identity)
  try {
   $until=[Environment]::TickCount64+2500;$ready=$null;do{$ready=@(Records $log|Where-Object kind -eq ready)|Select-Object -First 1;if($ready-or$process.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
   Require ($ready-and$ready.pid-eq$process.Id-and$ready.write_spies_terminal-and$ready.foreground_spies_terminal-and$ready.loaded_us_only-and$ready.no_layout_mutation) "$name terminal safety spies and already-loaded US layout available"
   $target=[pscustomobject]@{name=$name;process=$process;window=(Ptr $ready.hwnd);resume=$ready.resume_event;log=$log};$foregroundBefore=Actual-Foreground
   Require ($foregroundBefore.pid-ne$process.Id) "$name actual foreground outside fixture"
   $inject=Vi $target @('inject','--hooks-only') 8000;Require (-not$inject.timeout-and$inject.one_json-and$inject.exit-eq0-and$inject.reply.ok) "$name bounded hooks-only injection succeeds"
   $adopt=Vi $target @('config',('hwnd=0x{0:X}'-f[uint64]$ready.hwnd),'isolate=true','fakefocus=true','msgs=on','raw=off');Require ($adopt.exit-eq0-and$adopt.reply.ok-and$adopt.reply.hwnd-and[Convert]::ToUInt64($adopt.reply.hwnd.Substring(2),16)-eq[uint64]$ready.hwnd) "$name adopts exact owned tool HWND"
   $arm=Vi $target @('readiness','--wait-ms','1500');Require ($arm.exit-eq0-and$arm.reply.ok-and$arm.reply.hook_ready-and$arm.reply.window_ready-and$arm.reply.queue_ready) "$name fresh queue readiness ACK before producers"
   $snapshots=[Collections.Generic.List[object]]::new()
   Begin $target 100;Post $target 0x102 122 ([IntPtr]1);Post $target 0x106 121 ([IntPtr]1);Start-Sleep -Milliseconds 40;$s=Snapshot $target 100;$snapshots.Add($s);Check (Expected $s @()) "$name external harness WM_CHAR and WM_SYSCHAR are denied"
   Begin $target 101;$null=Producer $target @('type','h','--delay','0');Wait-Chars $target 101 1;$s=Snapshot $target 101;$snapshots.Add($s);Check (Expected $s @(104)) "$name explicit type h reaches standard pump and warms command arming"
   Begin $target 102;$null=Producer $target @('key','A','40');Wait-Chars $target 102 1;$s=Snapshot $target 102;$snapshots.Add($s);Check ((Expected $s @(97))-and$s.key_down-ge1-and$s.key_up-ge1) "$name virtual A translates to lowercase a"
   Begin $target 103;$null=Producer $target @('key','Shift+A','40');Wait-Chars $target 103 1;$s=Snapshot $target 103;$snapshots.Add($s);Check ((Expected $s @(65))-and$s.key_down-ge2-and$s.key_up-ge2) "$name virtual Shift+A translates to uppercase A"
   Begin $target 104;$null=Producer $target @('type','aB','--keys','--delay','0');Wait-Chars $target 104 2;$s=Snapshot $target 104;$snapshots.Add($s);Check (Expected $s @(97,66)) "$name type --keys preserves per-character modifiers"
   Begin $target 105;$null=Producer $target @('type',"`n",'--delay','0');Wait-Chars $target 105 1;$s=Snapshot $target 105;$snapshots.Add($s);Check (Expected $s @(13)) "$name newline key path generates one carriage-return WM_CHAR"
   Begin $target 106;$null=Pause $target 106
   try{$null=Producer $target @('down','Shift');$null=Producer $target @('down','A');$null=Producer $target @('up','A');$released=Producer $target @('up','Shift')}finally{$resumed=Resume $target 106}
   Wait-Chars $target 106 1;$s=Snapshot $target 106;$snapshots.Add($s);Check ((Expected $s @(65))-and$s.key_down-ge2-and$s.key_up-ge2) "$name delayed pump uses publication modifier snapshot after Shift was released"
   $pressureStatusBefore=Vi $target @('status')
   Begin $target 107;$null=Pause $target 107;$text='p'*768;$pressure=$null
   try{$pressure=Vi $target @('type',$text,'--delay','0') 4000}finally{$pressureResume=Resume $target 107}
   $accepted=-1;$receiptValid=$false
   if($pressure-and$pressure.reply-and$pressure.reply.PSObject.Properties['accepted_utf16_units']){try{$value=$pressure.reply.accepted_utf16_units;if(($value-is[ValueType])-and-not($value-is[bool])-and[double]$value-eq[math]::Floor([double]$value)-and[double]$value-ge0-and[double]$value-lt$text.Length){$accepted=[int]$value;$receiptValid=$true}}catch{}}
   Check ($pressure-and-not$pressure.timeout-and$pressure.one_json-and$pressure.exit-ne0-and-not$pressure.reply.ok-and$receiptValid-and$pressure.reply.requested_utf16_units-eq$text.Length-and$pressure.reply.partial-and$pressure.reply.post_error-ne0-and$pressure.reply.posting_stage-eq'character_messages'-and$pressure.reply.delivery_confirmed-eq$false) "$name bounded paused-pump pressure returns honest partial UTF16 admission failure"
   Wait-Chars $target 107 $(if($receiptValid){$accepted}else{0}) 1500;Start-Sleep -Milliseconds 60;$s=Snapshot $target 107;$snapshots.Add($s)
   $prefix=if($receiptValid){[int[]]@(for($n=0;$n-lt$accepted;$n++){112})}else{[int[]]@()}
   Check ($receiptValid-and(Expected $s $prefix)-and@($s.chars).Count-eq$accepted) "$name resumed pump receives exactly accepted prefix and no unmarked suffix"
   $pressureStatusAfter=Vi $target @('status');Check ($pressureStatusAfter.reply.posted_input.version-ge1-and$pressureStatusAfter.reply.posted_input.ring_refused-gt$pressureStatusBefore.reply.posted_input.ring_refused-and$pressureStatusAfter.reply.posted_input.pending-eq0) "$name explicit ring refusal counter advances and dispatched marks fully drain"
   Begin $target 108;$null=Producer $target @('type','h','--delay','0');Wait-Chars $target 108 1;$s=Snapshot $target 108;$snapshots.Add($s);Check (Expected $s @(104)) "$name fresh type succeeds after pressure drains"
   Begin $target 109;$null=Pause $target 109
   try{$null=Producer $target @('type',"a`nb",'--delay','0')}finally{$orderedResume=Resume $target 109}
   Wait-Chars $target 109 3;$s=Snapshot $target 109;$snapshots.Add($s);Check (Expected $s @(97,13,98)) "$name paused Unicode type preserves a CR b order without duplicate Enter translation"
   $status=Vi $target @('status');$foregroundAfter=Actual-Foreground;Check ($status.reply.ok-and$status.reply.isolate-and-not$status.reply.really_foreground-and$foregroundAfter.pid-ne$process.Id) "$name remains isolated and actual background after all controls"
   $evidence.Add([pscustomobject]@{case=$name;ready=$ready;inject=$inject;adopt=$adopt;readiness=$arm;snapshots=$snapshots;pressure=$pressure;pressure_status_before=$pressureStatusBefore;pressure_status_after=$pressureStatusAfter;accepted_utf16_units=$accepted;foreground_before=$foregroundBefore;foreground_after=$foregroundAfter;status=$status;log=$log})
  } catch {$evidence.Add([pscustomobject]@{case=$name;error=$_.Exception.Message;log=$log});if(-not$checks.Count-or$checks[$checks.Count-1].pass){Check $false "$name runner/setup error: $($_.Exception.Message)"}}
  finally {Stop-Owned $identity}
 }}
} catch {$exception=$_.Exception.Message;if(-not$checks.Count-or$checks[$checks.Count-1].pass){Check $false ('runner error: '+$exception)}}
finally {
 foreach($identity in $owned){Stop-Owned $identity};$failed=@($checks|Where-Object {-not$_.pass}).Count
 $checks|ConvertTo-Json -Depth 10|Set-Content (Join-Path $run 'checks.json');$calls|ConvertTo-Json -Depth 24|Set-Content (Join-Path $run 'calls.json');$evidence|ConvertTo-Json -Depth 30|Set-Content (Join-Path $run 'evidence.json')
 [pscustomobject]@{pass=$checks.Count-$failed;fail=$failed;out_dir=$run;exception=$exception;expected_red=$false;source_sha256=(Get-FileHash $source -Algorithm SHA256).Hash;header_sha256=(Get-FileHash $header -Algorithm SHA256).Hash;cli=$V;cli_sha256=(Get-FileHash $V -Algorithm SHA256).Hash;scope='standard TranslateMessage with virtual publication state; no dead-key/IME/real-layout activation tests'}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $run 'result.json')
}
Write-Host "$($checks.Count-$failed) PASS / $failed FAIL ($run)";if($failed){exit 1};exit 0
