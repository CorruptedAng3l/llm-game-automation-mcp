# Exact host-producer transport coverage, synthetic owned windows only.
# No expected-red inversion: any failed assertion exits1. A passing baseline is valid
# evidence that these transport controls do not reproduce the game's downstream failure.
param([string]$CliDir='', [string]$RepoRoot='', [string]$Out="$PSScriptRoot\..\build\test\host_selfraw\after")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
if(-not$RepoRoot){$candidate=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($n=0;$n-lt6-and$candidate;$n++){
 if(Test-Path -LiteralPath (Join-Path $candidate.FullName 'src\vinput_core.cpp')){$RepoRoot=$candidate.FullName;break};$candidate=$candidate.Parent
}}
if(-not$RepoRoot){throw 'Pass -RepoRoot with the product repository containing build/<arch>/host_mh objects'}
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'}
$V=(Resolve-Path -LiteralPath (Join-Path $CliDir 'vinput.exe')).Path
$source=(Resolve-Path -LiteralPath "$PSScriptRoot\..\test\vi_hostselfraw.cpp").Path
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force -Path $run|Out-Null
$run=(Resolve-Path -LiteralPath $run).Path;$tools=Join-Path $run 'tools';New-Item -ItemType Directory -Path $tools|Out-Null
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$posts=[Collections.Generic.List[object]]::new()
$owned=[Collections.Generic.List[object]]::new();$exception=$null
if(-not('ViHostSelfRawNative' -as [type])){Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class ViHostSelfRawNative {
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
}
'@}
function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{pass=$ok;name=$name});Write-Host "$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not$ok){throw $name}}
function Text([string]$path){if(-not(Test-Path -LiteralPath $path)){return ''};$stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete);$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}}
function Records([string]$path){foreach($line in ((Text $path)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Wait-Record([string]$path,[scriptblock]$predicate,[int]$timeout=2500){$until=[Environment]::TickCount64+$timeout;do{
 foreach($item in @(Records $path)){if(& $predicate $item){return $item}};Start-Sleep -Milliseconds 15
}while([Environment]::TickCount64-lt$until);return $null}
function Ptr([object]$value){return [IntPtr]::new([long][uint64]$value)}
function Post($fixture,[IntPtr]$window,[uint32]$message,[uint64]$w=0,[IntPtr]$l=[IntPtr]::Zero){
 $actual=[uint32]0;$thread=[ViHostSelfRawNative]::GetWindowThreadProcessId($window,[ref]$actual)
 if(-not$thread-or$actual-ne$fixture.process.Id-or-not[ViHostSelfRawNative]::IsWindow($window)){throw 'fixture HWND ownership/liveness changed'}
 $ok=[ViHostSelfRawNative]::PostMessageW($window,$message,[UIntPtr]::new($w),$l);$errorCode=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
 $posts.Add([pscustomobject]@{pid=$actual;hwnd=$window.ToInt64();message=$message;wparam=$w;lparam=$l.ToInt64();accepted=$ok;error=$errorCode;tick=[Environment]::TickCount64})
 if(-not$ok){throw "owned fixture PostMessage failed ($errorCode)"}
}
function Live-Owned($identity){try{$fresh=Get-Process -Id $identity.process.Id -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$identity.path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$identity.created){return $fresh}}catch{};return $null}
function Stop-Owned($identity){$fresh=Live-Owned $identity;if($fresh){try{Stop-Process -Id $fresh.Id -Force -Confirm:$false -ErrorAction Stop}catch{if(Live-Owned $identity){throw}}}}
function Run-Bounded([string]$file,[string[]]$arguments,[int]$timeout=6000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$file;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;foreach($argument in $arguments){[void]$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info;$watch=[Diagnostics.Stopwatch]::StartNew()
 if(-not$process.Start()){throw "could not start owned process $file"}
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timedOut=-not$process.WaitForExit($timeout)
 if($timedOut-and-not$process.HasExited){$process.Kill();[void]$process.WaitForExit(1000)}
 $outText=if($stdout.Wait(1000)){$stdout.Result}else{'<stdout unavailable after owned deadline>'}
 $errText=if($stderr.Wait(1000)){$stderr.Result}else{'<stderr unavailable after owned deadline>'}
 $code=if($process.HasExited){$process.ExitCode}else{-1};$watch.Stop();$process.Dispose()
 return [pscustomobject]@{exit=$code;stdout=$outText;stderr=$errText;timeout=$timedOut;elapsed_ms=$watch.ElapsedMilliseconds}
}
function Vi([uint32]$targetPid,[string[]]$arguments){$result=Run-Bounded $V (@('--json','--pid',"$targetPid")+$arguments) 8000
 $reply=$null;if(-not$result.timeout-and$result.stdout.Trim()){try{$reply=$result.stdout.Trim()|ConvertFrom-Json}catch{}}
 return [pscustomobject]@{exit=$result.exit;reply=$reply;stdout=$result.stdout;stderr=$result.stderr;timeout=$result.timeout;elapsed_ms=$result.elapsed_ms}
}
function Snapshot($fixture,[uint64]$token){Post $fixture $fixture.main (0x8000+3) $token
 $record=Wait-Record $fixture.log {param($r)$r.kind-eq'snapshot'-and$r.token-eq$token};if(-not$record){throw "missing snapshot token $token"};return $record}
function Real-Foreground(){[uint32]$pidValue=0;$window=[ViHostSelfRawNative]::GetForegroundWindow();[void][ViHostSelfRawNative]::GetWindowThreadProcessId($window,[ref]$pidValue);return [pscustomobject]@{hwnd=$window.ToInt64();pid=$pidValue}}
try {
 foreach($arch in @('x64','x86')){
  $archTools=Join-Path $tools $arch;New-Item -ItemType Directory -Force -Path $archTools,(Join-Path $archTools 'temp')|Out-Null
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
  $mh=Join-Path $RepoRoot "build\$arch\host_mh";foreach($name in @('buffer.obj','hook.obj','trampoline.obj',($hde+'.obj'))){if(-not(Test-Path -LiteralPath (Join-Path $mh $name))){throw "missing prebuilt $arch MinHook object $name"}}
  $build=Join-Path $archTools 'build.cmd'
  @"
@echo off
set "TEMP=$archTools\temp"
set "TMP=$archTools\temp"
call "$vcDir\$vc" >nul
if errorlevel 1 exit /b 1
cd /d "$archTools"
cl /nologo /O2 /MT /EHsc /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"$RepoRoot\third_party\minhook\include" "$source" /Fe:fixture.exe /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $build -Encoding ascii
  $compiled=Run-Bounded $env:ComSpec @('/d','/c',$build) 30000
  ($compiled.stdout+$compiled.stderr)|Set-Content -LiteralPath (Join-Path $archTools 'build.log')
  Check (-not$compiled.timeout-and$compiled.exit-eq0-and($compiled.stdout+$compiled.stderr)-notmatch'(?im)\bwarning (?:C|LNK)\d+') "$arch fixture builds /W4 /WX without warnings"
 }
 foreach($arch in @('x64','x86')){foreach($order in @('below','above')){foreach($pump in @('w','a')){
  $name="$arch.$order.$pump";$log=Join-Path $run ($name+'.jsonl');$exe=(Resolve-Path -LiteralPath (Join-Path $tools "$arch\fixture.exe")).Path
  $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$exe;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  foreach($arg in @($log,$order,$pump)){[void]$start.ArgumentList.Add($arg)}
  $process=[Diagnostics.Process]::Start($start);$identity=[pscustomobject]@{process=$process;path=$exe;created=$process.StartTime.ToUniversalTime().Ticks};$owned.Add($identity)
  try {
   $ready=Wait-Record $log {param($r)$r.kind-eq'ready'}
   Check ($ready-and$ready.pid-eq$process.Id-and$ready.write_spies_terminal-and$ready.foreground_spies_terminal-and$ready.no_real_registration) "$name owns both windows and terminal input/foreground spies"
   $fixture=[pscustomobject]@{process=$process;main=(Ptr $ready.hwnd);raw=(Ptr $ready.raw_hwnd);marker=(Ptr $ready.marker);log=$log}
   $foregroundBefore=Real-Foreground
   Check ($foregroundBefore.pid-ne$process.Id) "$name actual foreground stays outside the synthetic target"
   $injection=Vi $process.Id @('inject','--hooks-only')
   Check (-not$injection.timeout-and$injection.exit-eq0-and$injection.reply.ok) "$name bounded hooks-only injection succeeds"
   $statusBefore=Vi $process.Id @('status')
   Check (-not$statusBefore.timeout-and$statusBefore.exit-eq0-and$statusBefore.reply.ok-and$statusBefore.reply.isolate-and-not$statusBefore.reply.really_foreground) "$name starts with default isolation in background"
   Post $fixture $fixture.main (0x8000+1)
   $armed=Wait-Record $log {param($r)$r.kind-eq'armed'}
   Check ($armed-and$armed.ok-and$armed.order-eq$order) "$name installs $order host decoder and hidden target WndProc"
   $before=Snapshot $fixture 1000
   # These are actual HARNESS-origin posts, not target self-posts. Their address
   # marker is deliberately identical to the later legitimate producer marker.
   Post $fixture $fixture.raw 0xFF 0 $fixture.marker
   Post $fixture $fixture.raw 0xFF 0 ([IntPtr]0x56490000)
   Post $fixture $fixture.raw 0xFF 0 ([IntPtr]0x56490ACE)
   $external=Snapshot $fixture 1001
   Check ($external.received-eq$before.received-and$external.served-eq$before.served-and$external.applied-eq$before.applied) "$name earlier external duplicate address and forged magic never enter host consumer"
   Post $fixture $fixture.main (0x8000+2) 1010 ([IntPtr]0)
   $produced=Wait-Record $log {param($r)$r.kind-eq'produce'-and$r.request-eq1010}
   Check ($produced-and$produced.ok-and$produced.hidden_target-and$produced.target-eq$ready.raw_hwnd) "$name producer resolves synthetic registered hidden HWND and posts RIM_INPUT"
   $first=Wait-Record $log {param($r)$r.kind-eq'packet'-and$r.request-eq1010-and$r.stage-eq'single'}
   Check ($first-and$first.valid-and$first.marker_original-and$first.wparam-eq0-and$first.after_header_dx-eq37-and$first.after_header_dy-eq-19-and$first.dx-eq37-and$first.dy-eq-19-and$first.served_after-$first.served_before-eq1) "$name query/header preserve delta and one data read returns original payload"
   $normal=Snapshot $fixture 1011
   Check ($normal.posted-eq1-and$normal.received-eq1-and$normal.served-eq1-and$normal.nonzero_reads-eq1-and$normal.applied-eq1-and$normal.application_x-eq37-and$normal.application_y-eq-19-and$normal.pending_dx-eq0-and$normal.pending_dy-eq0) "$name isolated self packet advances synthetic application integrator exactly once"
   Post $fixture $fixture.main (0x8000+2) 1020 ([IntPtr]1)
   $single=Wait-Record $log {param($r)$r.kind-eq'packet'-and$r.request-eq1020-and$r.stage-eq'single'}
   $duplicate=Wait-Record $log {param($r)$r.kind-eq'packet'-and$r.request-eq1020-and$r.stage-eq'duplicate'}
   Check ($single-and$duplicate-and$single.valid-and$duplicate.valid-and$single.dx-eq37-and$single.dy-eq-19-and$duplicate.dx-eq0-and$duplicate.dy-eq0-and$duplicate.served_after-$duplicate.served_before-eq1) "$name duplicate RID_INPUT data is served but zero after one-shot consumption"
   $doubled=Snapshot $fixture 1021
   Check ($doubled.served-eq3-and$doubled.nonzero_reads-eq2-and$doubled.applied-eq2-and$doubled.application_x-eq74-and$doubled.application_y-eq-38) "$name served count alone is distinguished from nonzero reads and application progress"
   Post $fixture $fixture.main (0x8000+2) 1030 ([IntPtr]2)
   $deferred=Wait-Record $log {param($r)$r.kind-eq'packet'-and$r.request-eq1030-and$r.stage-eq'deferred'}
   Check ($deferred-and$deferred.scope-eq'outside_dispatch_unsupported') "$name deferred read explicitly reports unsupported dispatch scope"
   if($order-eq'below'){
    Check ($deferred.result-eq[uint32]::MaxValue-and-not$deferred.valid-and$deferred.served_after-eq$deferred.served_before) "$name below decoder is not admitted after trusted dispatch scope ends"
   }else{
    Check ($deferred.valid-and$deferred.dx-eq37-and$deferred.dy-eq-19) "$name above decoder can answer before vinput on unsupported deferred path"
   }
   $after=Snapshot $fixture 1031
   if($order-eq'below'){
    Check ($after.pending_dx-eq37-and$after.pending_dy-eq-19-and$after.applied-eq2) "$name unsupported deferred denial leaves host delta unconsumed"
   }else{
    Check ($after.pending_dx-eq0-and$after.pending_dy-eq0-and$after.applied-eq3) "$name unsupported above-decoder observation records its actual one-shot integration"
   }
   $statusAfter=Vi $process.Id @('status');$foregroundAfter=Real-Foreground
   Check (-not$statusAfter.timeout-and$statusAfter.reply.ok-and$statusAfter.reply.isolate-and-not$statusAfter.reply.really_foreground-and$foregroundAfter.pid-ne$process.Id) "$name keeps default isolation and actual background throughout controls"
   $evidence.Add([pscustomobject]@{case=$name;ready=$ready;injection=$injection;status_before=$statusBefore;status_after=$statusAfter;before=$before;external=$external;normal=$normal;duplicate=$doubled;deferred=$deferred;after=$after;foreground_before=$foregroundBefore;foreground_after=$foregroundAfter;log=$log;scope='synthetic transport and application integrator; no game-engine cause claim'})
  } finally {Stop-Owned $identity}
 }}}
} catch {
 $exception=$_.Exception.Message
 if(-not$checks.Count-or$checks[$checks.Count-1].pass){$checks.Add([pscustomobject]@{pass=$false;name=('runner error: '+$exception)})}
 Write-Host "ERROR $exception"
} finally {
 foreach($identity in $owned){Stop-Owned $identity}
 $failed=@($checks|Where-Object {-not$_.pass}).Count
 $checks|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $run 'checks.json')
 $evidence|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $run 'evidence.json')
 $posts|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $run 'harness_posts.json')
 [pscustomobject]@{pass=$checks.Count-$failed;fail=$failed;out_dir=$run;exception=$exception;expected_red=$false;source=$source;source_sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;cli=$V;cli_sha256=(Get-FileHash -LiteralPath $V -Algorithm SHA256).Hash;scope='synthetic producer transport, not downstream game-engine validation'}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $run 'result.json')
}
Write-Host "$($checks.Count-$failed) PASS / $failed FAIL ($run)"
if($failed){exit 1};exit 0
