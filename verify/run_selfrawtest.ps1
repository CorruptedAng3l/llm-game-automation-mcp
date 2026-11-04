# Background synthetic raw provenance. The fixture owns all windows and posts no OS input.
# Unfiltered PostMessageA/W -> DispatchMessageA/W scope; SendMessage/direct queue consumers excluded.
param([string]$CliDir="$PSScriptRoot\..\dist",
      [string]$Out="$PSScriptRoot\..\build\test\self_raw\after",[switch]$ExpectRed,[switch]$Extended,[switch]$KeepGoing,[switch]$PressureOnly)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $run|Out-Null;$run=(Resolve-Path $run).Path
$D=Join-Path $run 'tools';New-Item -ItemType Directory -Force $D|Out-Null
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=@()
. "$PSScriptRoot\vi_selfraw_pressure.ps1"
if(-not('ViSelfRawNative' -as [type])){Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class ViSelfRawNative {
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessageW(string value);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
}
'@}
function Check($ok,$name){$checks.Add([pscustomobject]@{pass=[bool]$ok;name=$name});Write-Host "$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not$ok-and-not$ExpectRed-and-not$KeepGoing){throw $name}}
function Text($path){if(-not(Test-Path -LiteralPath $path)){return ''};$stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete);$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}}
function Count($path,$pattern){return [regex]::Matches((Text $path),$pattern).Count}
function Wait-Log($path,$pattern,[int]$start=0,[int]$timeout=2500){$until=[Environment]::TickCount64+$timeout;do{$text=Text $path;if($text.Length-ge$start-and$text.Substring($start)-match$pattern){return $true};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until);return $false}
function Vi($targetPid,[string[]]$arguments){$lines=&$V --json --pid $targetPid @arguments;$code=$LASTEXITCODE;return [pscustomobject]@{exit=$code;reply=($lines|ConvertFrom-Json)}}
function Pointer($hex){$value=[Convert]::ToUInt64($hex,16);return [IntPtr][BitConverter]::ToInt64([BitConverter]::GetBytes($value),0)}
function Post($fixture,[uint32]$message,[uint64]$w=0,[IntPtr]$l=[IntPtr]::Zero){$actual=[uint32]0;[void][ViSelfRawNative]::GetWindowThreadProcessId($fixture.window,[ref]$actual);if($actual-ne$fixture.process.Id){throw 'fixture HWND ownership changed'};if(-not[ViSelfRawNative]::PostMessageW($fixture.window,$message,[UIntPtr]::new($w),$l)){throw "fixture post failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}}
$envelope=[ViSelfRawNative]::RegisterWindowMessageW('vinput.self_raw.envelope.v1')
try {
 foreach($arch in @('x64','x86')) {
  $tools=Join-Path $D $arch;New-Item -ItemType Directory -Force $tools,(Join-Path $tools 'temp')|Out-Null
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
  $mh=Join-Path $root "build\$arch\host_mh";$buildFile=Join-Path $tools 'build.cmd'
  foreach($object in @('buffer.obj','hook.obj','trampoline.obj',($hde+'.obj'))){if(-not(Test-Path -LiteralPath (Join-Path $mh $object))){throw "Missing prebuilt MinHook $arch object: $object; build both product arches first"}}
  @"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul
if errorlevel 1 exit /b 1
cd /d "$tools"
cl /nologo /O2 /MT /EHsc /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"$root\third_party\minhook\include" "$root\test\vi_selfraw.cpp" /Fe:fixture.exe /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $buildFile -Encoding ascii
  $buildOutput=&$env:ComSpec /d /c $buildFile 2>&1;$buildExit=$LASTEXITCODE;$buildOutput|Set-Content -LiteralPath (Join-Path $tools 'build.log')
  if($buildExit-ne0-or($buildOutput-join"`n")-match'(?im)\bwarning (?:C|LNK)\d+'){throw "Self-raw $arch fixture build failed/warned; inspect $tools\build.log"}
 }
 $orders=if($PressureOnly){@('below')}else{@('below','above')};$pumps=if($PressureOnly){@('w')}else{@('w','a')}
 foreach($arch in @('x64','x86')){foreach($order in $orders){foreach($pump in $pumps){
  $name="$arch.$order.$pump";$log=Join-Path $run ($name+'.log')
  $exe=Join-Path $D "$arch\fixture.exe"
  $process=Start-Process -FilePath $exe -ArgumentList @(('"'+$log+'"'),$order,$pump) -WindowStyle Hidden -PassThru
  $identity=[pscustomobject]@{process=$process;path=$exe;created=$process.StartTime.ToUniversalTime().Ticks};$owned+=,$identity
  try {
   if(-not(Wait-Log $log 'READY hwnd=0x[0-9A-F]+')){throw "$name fixture not ready"}
   $match=[regex]::Match((Text $log),'READY hwnd=0x([0-9A-F]+)')
   $fixture=[pscustomobject]@{process=$process;window=(Pointer $match.Groups[1].Value);log=$log}
   $injected=&$V --json inject --pid $process.Id|ConvertFrom-Json
   Check ($LASTEXITCODE-eq0-and$injected.ok) "$name injection succeeds"
   $status=$null;for($attempt=0;$attempt-lt80;$attempt++){$status=(Vi $process.Id @('status')).reply;if($status.hwnd-and$status.hwnd-ne'0x0'){break};Start-Sleep -Milliseconds 25}
   Check ($status.ok-and-not$status.really_foreground-and$status.isolate) "$name stays background with isolation enabled"
   Post $fixture (0x8000+1)
   Check (Wait-Log $log "ARMED decoder=$order overlay=outer") "$name installs host decoder and outer WndProc"
   $afterReplay=$null
   if(-not$PressureOnly){
   $before=Count $log 'RAW_RECEIVED count='
   Post $fixture 0xFF 1 ([IntPtr]0x49340001)
   Post $fixture 0xFF 1 ([IntPtr]0x56490000)
   Post $fixture 0xFF 1 ([IntPtr]0x56490ACE)
   # Same marker queued first from outside must not consume a self-post admission slot.
   Post $fixture (0x8000+2) 0
   Check (Wait-Log $log 'RAW_ACCEPT count=1 original_marker=1 original_wparam=1 dx=37 dy=-19') "$name self W post preserves marker, wParam and decoded size/data"
   $afterFirst=Count $log 'RAW_RECEIVED count='
   Check ($afterFirst-$before-eq1) "$name preceding external duplicate and forged vinput/host-prefix markers never reach outer host"
   Post $fixture (0x8000+2) 1
   Check (Wait-Log $log 'RAW_ACCEPT count=2 original_marker=1 original_wparam=1 dx=37 dy=-19') "$name self A post traverses $pump dispatcher with original host parameters"
   Post $fixture (0x8000+21)
   Check (Wait-Log $log 'HOST_PREFIX_ACCEPT original_marker=1 original_wparam=1 dx=91 dy=-23') "$name host self marker inside vinput-looking prefix delegates original decoder"
   $nativeBefore=Count $log 'RAW_RECEIVED count='
   $move=Vi $process.Id @('config','raw=post');$move=Vi $process.Id @('move','11','7','--steps','1')
   Check ($move.reply.ok-and(Wait-Log $log 'VINPUT_ACCEPT dx=11 dy=7')) "$name vinput own synthetic ring post uses trusted envelope"
   $beforeReplay=Count $log 'RAW_RECEIVED count='
   $tickets=[regex]::Matches((Text $log),('ENVELOPE message='+$envelope+' ticket=0x([0-9A-F]+) proof=0x([0-9A-F]+)'))
   if($tickets.Count){$last=$tickets[$tickets.Count-1];Post $fixture $envelope ([Convert]::ToUInt64($last.Groups[1].Value,16)) (Pointer $last.Groups[2].Value)}
   Start-Sleep -Milliseconds 100
   $afterReplay=(Vi $process.Id @('status')).reply
   Check ($tickets.Count-gt0-and(Count $log 'RAW_RECEIVED count=')-eq$beforeReplay-and$afterReplay.self_raw.rejected-ge1) "$name consumed envelope cannot be replayed from outside"
   $generationBefore=Count $log 'RAW_RECEIVED count='
   $offset=(Text $log).Length;Post $fixture (0x8000+7)
   Check (Wait-Log $log 'GENERATION_INVALIDATED post_result=1' $offset) "$name queues self raw then invalidates its window generation"
   Start-Sleep -Milliseconds 80
   Check ((Count $log 'RAW_RECEIVED count=')-eq$generationBefore) "$name invalidated window generation cannot deliver queued raw"
   $after=(Vi $process.Id @('status')).reply
   Check ($after.self_raw.supported-and$after.self_raw.pending-eq0-and$after.self_raw.dispatched-ge4-and$after.counters.wm_input_dropped-ge3) "$name reports bounded self provenance and counted external drops"
   }
   if($order-eq'below'-and$pump-eq'w') {
    $configured=Vi $process.Id @('config','raw=post','msgs=off');$beforePressure=$configured.reply
    Check $beforePressure.ok "$name raw pressure mode configured before pausing its UI"
    $pressureStart=(Text $log).Length;$resume=$log+'.pressure.resume';$pressure=$null
    try {
     Post $fixture (0x8000+20)
     Check (Wait-Log $log 'PRESSURE_PAUSED' $pressureStart) "$name only owned UI pauses for rejected-post pressure"
     $pressure=[ViSelfRawPressure]::Run([uint32]$process.Id,$fixture.window)
    }finally{[IO.File]::WriteAllText($resume,'resume')}
    Check (Wait-Log $log 'PRESSURE_RESUMED' $pressureStart) "$name owned UI resumes after persistent-pipe pressure"
    $queued=$pressure.accepted_reply|ConvertFrom-Json;$rejected=$pressure.rejected_reply|ConvertFrom-Json
    Check ($queued.ok-and$queued.relative_delivery.raw_attempts-eq1-and$queued.relative_delivery.raw_failures-eq0-and$queued.relative_delivery.raw_posted_dx-eq37-and$queued.relative_delivery.raw_posted_dy-eq-19) "$name one original37/-19 raw record was accepted before quota"
    Check ($pressure.filled-gt0-and$pressure.queue_error-eq1816-and$pressure.elapsed_ms-lt8000) "$name own queue reaches native quota within snapshot lifetime"
    Check ($rejected.ok-and$rejected.relative_delivery.raw_attempts-eq5000-and$rejected.relative_delivery.raw_failures-eq5000-and$rejected.relative_delivery.raw_posted_dx-eq0-and$rejected.relative_delivery.raw_failed_dx-eq5000-and-not$rejected.relative_delivery.raw_submission_exact) "$name all5000 later rejected raw attempts retain honest per-channel backpressure"
    $preserved=Wait-Log $log 'VINPUT_ACCEPT dx=37 dy=-19' $pressureStart
    $pressureText=(Text $log).Substring($pressureStart)
    Check ($preserved-and[regex]::Matches($pressureText,'VINPUT_ACCEPT dx=').Count-eq1) "$name rejected marker wrap never corrupts or duplicates the earlier accepted payload"
    $afterPressure=(Vi $process.Id @('status')).reply
    Check ($afterPressure.raw_unread_overwrites-eq$beforePressure.raw_unread_overwrites) "$name rejected posts never overwrite unread accepted raw state"
    if($afterPressure.self_raw.supported){Check ($afterPressure.self_raw.internal_pending-eq0-and$afterPressure.self_raw.pending-eq0) "$name internal snapshot admission retires after resumed dispatch"}
    $evidence.Add([pscustomobject]@{case=$name;pressure=$pressure;accepted_reply=$queued;rejected_reply=$rejected;before=$beforePressure;after=$afterPressure;log_delta=$pressureText})
    [void](Vi $process.Id @('config','msgs=auto'))
    $after=$afterPressure
   }
   if($Extended-and$arch-eq'x64'-and$order-eq'below'-and$pump-eq'w') {
    $offset=(Text $log).Length;Post $fixture (0x8000+3)
    Check (Wait-Log $log 'CAPACITY successes=1024 failures=1 error=1816' $offset) "$name capacity rejects the 1025th queued self packet"
    for($n=0;$n-lt120;$n++){$capacity=(Vi $process.Id @('status')).reply;if($capacity.self_raw.pending-eq0){break};Start-Sleep -Milliseconds 25}
    Check ($capacity.self_raw.pending-eq0) "$name bounded queue drains without retained packets"
    $offset=(Text $log).Length;Post $fixture (0x8000+4)
    Check (Wait-Log $log 'QUEUE_FULL filled=[1-9][0-9]* raw_result=0 error=1816' $offset) "$name native failed raw post retains its queue-full error"
    $failedPost=(Vi $process.Id @('status')).reply
    Check ($failedPost.self_raw.post_failed-ge1-and$failedPost.self_raw.pending-eq0) "$name native failed post retires admission immediately"
    $expiryBefore=Count $log 'RAW_RECEIVED count='
    $offset=(Text $log).Length;Post $fixture (0x8000+5)
    Check (Wait-Log $log 'EXPIRY_RESUME' $offset 13000) "$name expired packet control resumes its own pump"
    Start-Sleep -Milliseconds 80;$expired=(Vi $process.Id @('status')).reply
    Check ($expired.self_raw.expired-ge1-and$expired.self_raw.pending-eq0-and(Count $log 'RAW_RECEIVED count=')-eq$expiryBefore) "$name expired self packet cannot reach host"
   }
   $evidence.Add([pscustomobject]@{case=$name;log=$log;before=$status;after=$after;after_replay=$afterReplay;original_accepts=(Count $log 'RAW_ACCEPT count=');raw_received=(Count $log 'RAW_RECEIVED count=')})
  } finally {
   $fresh=Get-Process -Id $process.Id -ErrorAction SilentlyContinue
   if($fresh-and$fresh.Path-eq$identity.path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$identity.created){Stop-Process -Id $process.Id -Force -Confirm:$false}
  }
 }}}
} finally {
 $checks|ConvertTo-Json -Depth 8|Set-Content (Join-Path $run 'checks.json')
 $evidence|ConvertTo-Json -Depth 20|Set-Content (Join-Path $run 'evidence.json')
 $failed=@($checks|Where-Object {-not$_.pass}).Count
 [pscustomobject]@{pass=$checks.Count-$failed;fail=$failed;out_dir=$run;expected_red=[bool]$ExpectRed}|ConvertTo-Json|Set-Content (Join-Path $run 'result.json')
 foreach($item in $owned){$fresh=Get-Process -Id $item.process.Id -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$item.path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$item.created){Stop-Process -Id $item.process.Id -Force -Confirm:$false}}
}
Write-Host "$($checks.Count-$failed) PASS / $failed FAIL"
if($ExpectRed){if($failed-lt2){throw 'baseline did not expose both self-origin and external-admission defects'};Write-Host 'EXPECTED RED: raw provenance defects observed';$global:LASTEXITCODE=0;exit 0}
if($failed){exit 1};$global:LASTEXITCODE=0;exit 0
