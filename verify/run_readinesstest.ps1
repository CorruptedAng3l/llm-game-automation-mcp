param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\readiness\after",[switch]$KeepGoing)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_ownedprocess.ps1"
$root=(Resolve-Path "$PSScriptRoot\..").Path;$v=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
New-Item -ItemType Directory -Force "$Out\tools\temp"|Out-Null
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViReadyNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'
$checks=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$retargets=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new();$targetId=0;$log=''
function Check($ok,$name){$checks.Add(@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not$ok-and-not$KeepGoing){throw $name}}
function Call([string[]]$argv){$watch=[Diagnostics.Stopwatch]::StartNew();$raw=@(& $v --json @argv);$code=$LASTEXITCODE;$watch.Stop();$reply=($raw-join[Environment]::NewLine)|ConvertFrom-Json;$calls.Add(@{args=$argv;exit=$code;elapsed_ms=$watch.ElapsedMilliseconds;reply=$reply});return $reply}
function TargetCall([string[]]$argv){return Call (@('--pid',"$script:targetId")+$argv)}
function LogText {if(Test-Path -LiteralPath $script:log){return Get-Content -LiteralPath $script:log -Raw};return ''}
function WaitLog([string]$pattern,[int]$timeout=2500){$until=[Environment]::TickCount64+$timeout;do{if((LogText)-match$pattern){return $true};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);return $false}
function StartObserver([string]$case,[int]$delay){$script:log="$Out\$case.log";$process=Start-Process "$Out\tools\consumer$suffix.exe" -ArgumentList ('"{0}" {1}'-f$log,$delay) -WindowStyle Hidden -PassThru;$owned.Add($process);$script:targetId=$process.Id;return $process}
function Ready($reply){return $reply.ok-and$reply.readiness.readiness_version-eq1-and$reply.readiness.hook_ready-and$reply.readiness.window_ready-and$reply.readiness.queue_ready}
try {
 foreach($arch in @('x64','x86')) {
  $suffix=if($arch-eq'x64'){''}else{'32'};$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  $batch=@"
@echo off
set "TEMP=$Out\tools\temp"
set "TMP=$Out\tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_readinessconsumer.cpp" /Fo:"$Out\tools\consumer$suffix.obj" /Fe:"$Out\tools\consumer$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_ready_jsontest.cpp" /Fo:"$Out\tools\ready_json$suffix.obj" /Fe:"$Out\tools\ready_json$suffix.exe" || exit /b 2
"$Out\tools\ready_json$suffix.exe" || exit /b 3
"@
  [IO.File]::WriteAllText("$Out\tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII)
  & cmd /d /c "$Out\tools\build_$arch.cmd" *> "$Out\tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch readiness fixture zero warnings"

  # A legacy ping-only injector returns success before this window exists.
  $process=StartObserver "delayed_$arch" 3500
  $inject=Call @('inject','--pid',"$targetId")
  $calls[-1].log_at_return=LogText
  Check ((Ready $inject)-and(LogText)-match'READY hwnd=') "$arch inject waits for delayed adoption and actual posted queue progress"
  Check ($null-eq$inject.readiness.application_ready) "$arch infrastructure readiness does not claim host/application startup"
  $again=Call @('inject','--pid',"$targetId")
  Check ((Ready $again)-and$again.already_injected) "$arch already-injected path performs full readiness again"
  $full=Call @('inject','--pid',"$targetId",'--hooks-only=false')
  Check ((Ready $full)-and$full.readiness.scope-eq'window_queue') "$arch hooks-only=false preserves full handshake"
  $direct=TargetCall @('readiness','--wait-ms','500','--hooks-only=false')
  Check ($direct.ok-and$direct.window_ready-and$direct.queue_ready-and$direct.scope-eq'window_queue') "$arch direct readiness maps wait-ms and hooks-only=false"
  $direct=TargetCall @('readiness','--wait-ms','0','--hooks-only=true')
  Check ($direct.ok-and$direct.hook_ready-and$direct.scope-eq'hooks') "$arch direct readiness maps explicit hooks-only=true"
  foreach($flag in @('--hooks-only=0','--hooks-only=TRUE','--hooks-only=','--hooks-only=invalid')) {
   $bad=Call @('inject','--pid',"$targetId",$flag)
   Check (-not$bad.ok-and$bad.input_injected-eq$false-and$bad.error-match'--hooks-only') "$arch invalid hooks-only boolean rejected before inject"
  }
  $bad=Call @('inject','--pid',"$targetId",'--hooks-only','false')
  Check (-not$bad.ok-and$bad.input_injected-eq$false-and$bad.error-match'positional') "$arch bare hooks-only never consumes positional token"
  $key=TargetCall @('key','insert','180')
  Check ($key.ok-and(WaitLog 'OBS insert=1')) "$arch first INSERT is observed after inject without a retry"
  Check ((TargetCall @('status')).really_foreground-eq$false) "$arch delayed observer remains background"
  Stop-ViOwnedProcess $process

  # This hooks-only handshake is also what launch --early must use while its
  # main thread is suspended. An immediate command still requires adoption.
  $process=StartObserver "first_command_$arch" 3500
  $hooks=Call @('inject','--pid',"$targetId",'--hooks-only')
  Check ($hooks.ok-and$hooks.readiness.hook_ready-and$hooks.readiness.scope-eq'hooks') "$arch hooks-only handshake can finish before a target window"
  $key=TargetCall @('key','insert','180')
  Check ($key.ok-and(WaitLog 'OBS insert=1')) "$arch first command independently waits for delayed window and queue"
  Stop-ViOwnedProcess $process

  $process=StartObserver "first_sequence_$arch" 3500
  $hooks=Call @('inject','--pid',"$targetId",'--hooks-only')
  Check ($hooks.ok-and$hooks.readiness.hook_ready) "$arch first sequence starts after hooks-only handshake"
  $sequenceFile="$Out\first_sequence_$arch.vin";[IO.File]::WriteAllText($sequenceFile,"key insert 180"+[Environment]::NewLine)
  $sequence=TargetCall @('seq',$sequenceFile)
  Check ($sequence.ok-and$sequence.accepted-and$sequence.job_id-and-not$sequence.delivery_unknown-and(WaitLog 'OBS insert=1')) "$arch first sequence waits for delayed adoption within its scheduling acknowledgement budget"
  Stop-ViOwnedProcess $process

  $process=StartObserver "hung_$arch" 0
  $inject=Call @('inject','--pid',"$targetId");Check (Ready $inject) "$arch queue negative starts from a ready observer"
  $status=TargetCall @('status');$window=[IntPtr]::new([Convert]::ToInt64($status.hwnd.Substring(2),16))
  [ViReadyNative]::PostMessage($window,0x8001,[IntPtr]::new(20000),[IntPtr]::Zero)|Out-Null
  Check (WaitLog 'PAUSED') "$arch only owned observer queue paused"
  $before=(TargetCall @('trace','key','insert')).injected_down
  $r=TargetCall @('readiness','--wait-ms','100')
  Check (-not$r.ok-and$r.hook_ready-and$r.window_ready-and-not$r.queue_ready-and$r.readiness_reason-eq'queue_not_progressing') "$arch readiness reports hooks/window present with queue stopped"
  $key=TargetCall @('key','insert','180')
  $after=(TargetCall @('trace','key','insert')).injected_down
  Check (-not$key.ok-and$key.input_injected-eq$false-and$before-eq$after-and(LogText)-notmatch'OBS insert=1') "$arch stopped queue refuses first key before any injection"
  Check ($calls[$calls.Count-2].elapsed_ms-lt6500) "$arch first-key readiness refusal bounded"
  $again=Call @('inject','--pid',"$targetId",'--hooks-only=false')
  Check (-not$again.ok-and$again.readiness.window_ready-and-not$again.readiness.queue_ready) "$arch already-injected stopped queue cannot report success"
  $release=TargetCall @('release');Check ($release.ok-and$calls[-1].elapsed_ms-lt1000) "$arch cleanup bypasses queue readiness"
  Stop-ViOwnedProcess $process

  # Arm the first window with an actual key, then change the owned target while
  # its UI is responsive. A successful explicit probe must not arm the command
  # cache for that new generation. Pause only after config/probe have returned.
  $process=StartObserver "retarget_$arch" 0
  $case=@{arch=$arch;pid=$targetId;log=$log};$retargets.Add($case)
  $inject=Call @('inject','--pid',"$targetId");Check (Ready $inject) "$arch retarget fixture starts with full readiness"
  $key=TargetCall @('key','insert','180')
  Check ($key.ok-and(WaitLog 'OBS insert=1')) "$arch first owned window is armed by an actual key"
  $initial=TargetCall @('readiness','--wait-ms','500');$case.before_readiness=$initial
  Check ($initial.ok-and$initial.queue_ready) "$arch initial window/generation positively witnessed"
  $oldWindow=[IntPtr]::new([Convert]::ToInt64($initial.hwnd.Substring(2),16))
  [ViReadyNative]::PostMessage($oldWindow,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Check (WaitLog 'RETARGET_READY hwnd=') "$arch second larger owned offscreen window created"
  $created=[regex]::Match((LogText),'RETARGET_READY hwnd=(0x[0-9A-Fa-f]+) pid=(\d+) client_w=(\d+) client_h=(\d+)')
  $case.created_window=$created.Value;$newHandle=$created.Groups[1].Value
  Check ($created.Success-and[long]$created.Groups[2].Value-eq$targetId-and$newHandle-ne$initial.hwnd) "$arch replacement HWND is distinct and belongs to the owned fixture"
  $configured=TargetCall @('config',("hwnd="+$newHandle));$case.config_reply=$configured
  Check ($configured.ok-and$configured.hwnd-eq$newHandle) "$arch responsive config adopts the replacement before any pause"
  $probe=TargetCall @('readiness','--wait-ms','500');$case.after_readiness=$probe
  Check ($probe.ok-and$probe.queue_ready-and$probe.hwnd-eq$newHandle-and$probe.window_generation-gt$initial.window_generation) "$arch retarget has a new generation and fresh explicit queue witness"
  Check ((TargetCall @('status')).really_foreground-eq$false) "$arch retarget remains background"
  $newWindow=[IntPtr]::new([Convert]::ToInt64($newHandle.Substring(2),16))
  [ViReadyNative]::PostMessage($newWindow,0x8001,[IntPtr]::new(20000),[IntPtr]::Zero)|Out-Null
  Check (WaitLog 'PAUSED') "$arch replacement queue paused after config and explicit probe completed"
  $before=(TargetCall @('trace','key','insert')).injected_down;$case.before_injected_down=$before
  $key=TargetCall @('key','insert','180');$case.first_key_after_retarget=$key;$case.first_key_elapsed_ms=$calls[-1].elapsed_ms
  $after=(TargetCall @('trace','key','insert')).injected_down;$case.after_injected_down=$after;$case.log_after_first_key=LogText
  Check (-not$key.ok-and$key.input_injected-eq$false-and$before-eq$after) "$arch old command cache and explicit probe cannot arm the first key after retarget"
  Check ($key.window_generation-eq$probe.window_generation-and$key.hwnd-eq$newHandle-and$key.readiness_reason-eq'queue_not_progressing'-and$case.first_key_elapsed_ms-lt6500) "$arch refusal describes the paused replacement generation within its bound"
  TargetCall @('release')|Out-Null
  Stop-ViOwnedProcess $process

  # The CLI owns this synthetic child. --early must resume it after a hooks-only
  # handshake, then use the full readiness path once its window is running.
  $script:log="$Out\early_$arch.log"
  $launch=Call @('launch','--exe',"$Out\tools\consumer$suffix.exe",'--args',('"{0}" 0'-f$log),'--early','--timeout','15','--settle','0','--fg-guard','0')
  if($launch.pid){$early=Get-Process -Id $launch.pid -ErrorAction SilentlyContinue;if($early){$owned.Add($early)}}
  Check ($launch.ok-and$launch.early_injected-and(WaitLog 'READY hwnd=')) "$arch suspended early launch resumes before waiting for window"
  if($launch.pid){$script:targetId=[int]$launch.pid;$r=TargetCall @('readiness','--wait-ms','500');Check ($r.ok-and$r.hook_ready-and$r.window_ready-and$r.queue_ready) "$arch early launch ends with fully ready input";if($early){Stop-ViOwnedProcess $early}}
 }
} finally {
 foreach($process in $owned){Stop-ViOwnedProcess $process}
 @{checks=$checks;calls=$calls;retarget_evidence=$retargets}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json" -Encoding utf8NoBOM
}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
