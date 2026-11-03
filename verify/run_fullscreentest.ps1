param([string]$Out="$PSScriptRoot\..\build\test\fullscreen\after",[string]$CliDir="$PSScriptRoot\..\dist",
 [ValidateSet('visible','hidden','refuse','plain')][string[]]$Modes=@('visible','hidden','refuse','plain'),[ValidateSet('x64','x86')][string[]]$Architectures=@('x64','x86'),[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$dist=Join-Path $root 'dist';$CliDir=(Resolve-Path $CliDir).Path
$env:NoDefaultCurrentDirectoryInExePath='1';$env:TEMP=Join-Path $root 'build\temp';$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $Out,$env:TEMP|Out-Null;$Out=(Resolve-Path $Out).Path
if(-not('ViFsControl' -as [type])){Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class ViFsControl{[DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);}'}
. "$PSScriptRoot\vi_patterncheck.ps1"
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new();$fails=0
function Check($pass,$name){$checks.Add([pscustomobject]@{pass=[bool]$pass;name=$name});Write-Host "$(if($pass){'PASS'}else{'FAIL'}) $name";if(-not$pass){$script:fails++;if(-not$KeepGoing){throw $name}}}
function Read-Log($path){if(-not(Test-Path -LiteralPath $path)){return ''};$stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete);$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}}
function Wait-Log($path,$pattern,[int]$timeout=2500){$until=[Environment]::TickCount64+$timeout;do{$text=Read-Log $path;if($text-match$pattern){return $true};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$until);return $false}
function Remember($p,$path){$owned.Add([pscustomobject]@{process=$p;pid=$p.Id;path=$path;creation=$p.StartTime.ToUniversalTime().Ticks});return $p}
function Clean {foreach($item in $owned){$fresh=Get-Process -Id $item.pid -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$item.path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$item.creation){Stop-Process -Id $item.pid -Force -Confirm:$false}};$owned.Clear()}
function Vi($cli,$targetPid,[string[]]$arguments){$text=&$cli --json --pid $targetPid @arguments;$exit=$LASTEXITCODE;return [pscustomobject]@{exit=$exit;reply=($text|ConvertFrom-Json)}}
function Command($window,$targetPid,$number){$actual=[uint32]0;[void][ViFsControl]::GetWindowThreadProcessId($window,[ref]$actual);if($actual-ne$targetPid){throw 'fixture HWND ownership changed'};if(-not[ViFsControl]::PostMessageW($window,0x8000+$number,[UIntPtr]::Zero,[IntPtr]::Zero)){throw 'owned fixture control post failed'}}
try{
 foreach($arch in $Architectures){foreach($mode in $Modes){
  $name="$arch.$mode";$caseOut=Join-Path $Out $name;New-Item -ItemType Directory -Force $caseOut|Out-Null;$log=Join-Path $caseOut 'target.log'
  $cli=Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'});$exe=Join-Path $dist $(if($arch-eq'x64'){'vi_fullscreen_retry.exe'}else{'vi_fullscreen_retry32.exe'});$target=$null;$status=$null;$barrierPosted=$false
  try{
   $si=[Diagnostics.ProcessStartInfo]::new($cli);$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
   foreach($a in @('--json','launch','--exe',$exe,'--args',('"'+$log+'" '+$mode),'--early','--settle','1','--fg-guard','0','--timeout','20','--lock',(Join-Path $caseOut 'private-launch.lock'))){$si.ArgumentList.Add($a)}
   $launcher=[Diagnostics.Process]::new();$launcher.StartInfo=$si;[void]$launcher.Start();[void](Remember $launcher $cli);$stdout=$launcher.StandardOutput.ReadToEndAsync();$stderr=$launcher.StandardError.ReadToEndAsync();$until=[Environment]::TickCount64+35000
   do{if(-not$target){$child=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($launcher.Id)"|Where-Object ExecutablePath -eq $exe|Select-Object -First 1;if($child){$target=Remember (Get-Process -Id $child.ProcessId) $exe}};if($target-and-not$barrierPosted){$match=[regex]::Match((Read-Log $log),'HOOK_BARRIER_READY hwnd=0x([0-9A-F]+)');if($match.Success){$probe=(Vi $cli $target.Id @('status')).reply;if($probe.ok-and$probe.hooks_ok.Contains('IDXGISwapChain::SetFullscreenState ')){if($mode-eq'refuse'){[void](Vi $cli $target.Id @('config','fullscreen=refuse'))};Command ([IntPtr][Convert]::ToInt64($match.Groups[1].Value,16)) $target.Id 10;$barrierPosted=$true}}};if($launcher.HasExited){break};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$until)
   if(-not$launcher.HasExited){throw 'early fullscreen launch timed out'};$raw=$stdout.GetAwaiter().GetResult();$raw|Set-Content (Join-Path $caseOut 'launch.stdout.log');$stderr.GetAwaiter().GetResult()|Set-Content (Join-Path $caseOut 'launch.stderr.log');$launch=$raw|ConvertFrom-Json
   Check $barrierPosted "$name external controller confirms hook and authorizes first TRUE request while UI pumps"
   Check ($launcher.ExitCode-eq0-and$launch.ok-and$launch.early_injected) "$name suspended injection confirmed before TRUE fullscreen requests"
   if(-not$target){$target=Remember (Get-Process -Id $launch.pid) $exe}
   Check (Wait-Log $log 'READY hwnd=0x[0-9A-F]+') "$name fixture ready"
   $status=(Vi $cli $target.Id @('status')).reply;$record=[ordered]@{case=$name;pid=$target.Id;launch=$launch;before=$status};$evidence.Add($record)
   Check ($status.ok-and-not$status.really_foreground) "$name owned target remains background"
   if($mode-eq'refuse'){
    Check ($launch.render_ready-eq$false-and$launch.render_warning-match'fullscreen'-and$launch.fullscreen.blocked_before_first_present) "$name launch warns on repeated refusal with zero target presents"
    $clock=[Diagnostics.Stopwatch]::StartNew();$cap=Vi $cli $target.Id @('capture',(Join-Path $caseOut 'frame.png'),'--timeout-ms','5000');$clock.Stop();$record.capture=$cap;$record.capture_ms=$clock.ElapsedMilliseconds
    Check (-not$cap.reply.ok-and$cap.reply.error-match'fullscreen negotiation blocked'-and$clock.ElapsedMilliseconds-lt800-and-not(Test-Path (Join-Path $caseOut 'frame.png'))) "$name capture fails quickly with fullscreen reason and no black fallback"
    Check ($status.frames-eq0-and$status.fullscreen.refused-ge3) "$name fixture actually retries and never presents"
   }elseif($mode-eq'plain'){
    Check (-not$launch.render_ready-and-not$launch.render_warning-and-not$status.fullscreen.blocked_before_first_present) "$name ordinary no-present startup is not diagnosed as fullscreen failure"
    $cap=Vi $cli $target.Id @('capture',(Join-Path $caseOut 'frame.png'));$record.capture=$cap
    Check ($cap.reply.ok-and$cap.reply.capture_path-eq'printwindow') "$name non-rendering window retains automatic PrintWindow path"
   }else{
    $success=Wait-Log $log 'FULLSCREEN_OK sequence=1 query=00000000 fullscreen=1 output=1 refs100=1 native_windowed=1 outer_same=1 visibility_same=1 safety_exstyle_same=1 borderless=1'
    Check $success "$name virtual state/output refs coherent; native windowed; placement/visibility/topmost/noactivate preserved"
    $cap=Vi $cli $target.Id @('capture',(Join-Path $caseOut 'frame.png'),'--method','native','--timeout-ms','500');$pattern=if($cap.reply.ok){Test-VinputPattern (Join-Path $caseOut 'frame.png')}else{$null};$record.capture=$cap;$record.pattern=$pattern
    Check ($cap.reply.ok-and$cap.reply.capture_path-eq'd3d11'-and$pattern.ok) "$name native presented pixels decode actual pattern"
    if($success){
     $window=[IntPtr][Convert]::ToInt64($status.hwnd,16);Command $window $target.Id 1
     Check (Wait-Log $log 'RESIZE_STATE hr=00000000 fullscreen=1 output=1') "$name virtual state survives ResizeBuffers/window resize"
     $second=Vi $cli $target.Id @('capture',(Join-Path $caseOut 'frame2.png'),'--method','native','--after-frames','10');$pattern2=if($second.reply.ok){Test-VinputPattern (Join-Path $caseOut 'frame2.png')}else{$null};$record.second=$second;$record.pattern2=$pattern2
     Check ($second.reply.ok-and$pattern2.ok-and$pattern2.frame-ge$pattern.frame+10-and$second.reply.captured_present-ge$cap.reply.captured_present+10) "$name resized source/captured frames advance by10 with valid pixels"
     Command $window $target.Id 2;Check (Wait-Log $log 'FALSE_STATE hr=00000000 query=00000000 fullscreen=0 output=0 restored_frame=1') "$name FALSE restores owned frame bits, preserving newer visibility, and native windowed query"
     Command $window $target.Id 4;Check (Wait-Log $log 'FULLSCREEN_OK sequence=2') "$name virtual fullscreen can be entered again"
     Command $window $target.Id 3;Check (Wait-Log $log 'RECREATED query=00000000 fullscreen=0 output=0') "$name recreated swapchain cannot inherit virtual flag/output"
     $released=(Vi $cli $target.Id @('status')).reply;Check ($released.fullscreen.states_live-eq0-and$released.fullscreen.output_refs-eq0) "$name release balances private state/output refs"
     [void](Vi $cli $target.Id @('config','nofullscreen=false'));Command $window $target.Id 5;Start-Sleep -Milliseconds 100
     $native=(Vi $cli $target.Id @('status')).reply;Check ($native.fullscreen.policy-eq'native'-and$native.fullscreen.passthrough-ge1) "$name nofullscreen=false performs safe native FALSE passthrough"
    }
   }
   $record.after=(Vi $cli $target.Id @('status')).reply
  }finally{if($status-and$status.log_path-and(Test-Path -LiteralPath $status.log_path)){Copy-Item -LiteralPath $status.log_path -Destination (Join-Path $caseOut 'vinput.log')};Clean}
 }}
}finally{$checks|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Out 'checks.json');$evidence|ConvertTo-Json -Depth 18|Set-Content (Join-Path $Out 'evidence.json');Clean}
Write-Host "$($checks.Count-$fails) PASS / $fails FAIL";if($fails){exit 1};exit 0
