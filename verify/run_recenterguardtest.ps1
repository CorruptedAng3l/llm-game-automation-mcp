# Only guarded fixtures may receive the recenter command. No global cursor-equality oracle.
# Negative: no DLL/pipe => zero native calls. Positive: real hooked API counter and virtual delta.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\recenterguard")
$ErrorActionPreference='Stop'
. "$PSScriptRoot\vi_textlog.ps1"
$D=(Resolve-Path $TargetDir).Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
if(-not('ViRecenterGuardNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViRecenterGuardNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);[DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);}'}
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=@()
function SaveResult { @{checks=@($checks.ToArray());evidence=@($records.ToArray())}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json" }
function Check($Ok,[string]$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});SaveResult;"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok){throw $Name}}
function ReadLog($Target){Read-ViSharedText $Target.log}
function ReadOwnedStart([string]$Text,[long]$WorkerPid){
 if($Text-match'(?m)START pid=(\d+) hwnd=(?:0x)?([0-9A-Fa-f]+) noactivate=1\r?$'-and[long]$Matches[1]-eq$WorkerPid){return [IntPtr]::new([Convert]::ToInt64($Matches[2],16))}
 return [IntPtr]::Zero
}
function Post($Target,[int]$Message,[int]$Value=0){if(-not[ViRecenterGuardNative]::PostMessage($Target.h,0x8000+$Message,[IntPtr]$Value,[IntPtr]::Zero)){throw 'owned fixture control post failed'}}
function Stats($Target){
 $old=@([regex]::Matches((ReadLog $Target),'RECENTER_GUARD requests=')).Count;Post $Target 15
 $until=[Environment]::TickCount64+2000
 do{$matches=[regex]::Matches((ReadLog $Target),'RECENTER_GUARD requests=(\d+) refusals=(\d+) native_calls=(\d+)');if($matches.Count-gt$old){$last=$matches[$matches.Count-1];return [pscustomobject]@{requests=[long]$last.Groups[1].Value;refusals=[long]$last.Groups[2].Value;native_calls=[long]$last.Groups[3].Value}};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 throw 'owned fixture guard statistics unavailable'
}
function Disable($Target){
 $before=[regex]::Matches((ReadLog $Target),'RECENTER off generation=(\d+)').Count;Post $Target 2 0;$until=[Environment]::TickCount64+2000
 do{
  $text=ReadLog $Target;$off=[regex]::Matches($text,'RECENTER off generation=(\d+)')
  if($off.Count-gt$before){$generation=$off[$off.Count-1].Groups[1].Value;if($text-match("RECENTER_IDLE generation="+$generation+' native_calls=\d+')){return Stats $Target}}
  Start-Sleep -Milliseconds 5
 }while([Environment]::TickCount64-lt$until)
 throw 'worker OFF quiescence acknowledgement unavailable'
}
function Vi($GamePid,[string[]]$Arguments){$reply=& $V --json --pid $GamePid @Arguments|ConvertFrom-Json;if($LASTEXITCODE-ne0-or-not$reply.ok){throw "vinput failed: $Arguments"};return $reply}
try{foreach($arch in @('','32')){
 # A unique log prevents reading a previous run before the new process truncates its file.
 $log="$Out\guard$arch`_$([guid]::NewGuid().ToString('N')).log";$worker=Start-Process -FilePath "$D\vi_channelprobe$arch.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned+=$worker
 $target=@{pid=$worker.Id;h=[IntPtr]::Zero;log=$log};$until=[Environment]::TickCount64+3000
 do{$text=ReadLog $target;$target.h=ReadOwnedStart $text $worker.Id;if($target.h-ne[IntPtr]::Zero-and$text-match'RECENTER_GUARD_VERSION 1 '){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 # This prerequisite is checked BEFORE enabling anything; old unguarded binaries are safe REDs.
 Check ($target.h-ne[IntPtr]::Zero-and$text-match'RECENTER_GUARD_VERSION 1 ') "guard$arch fail-closed fixture version present before recenter enable"
 $identity=[Collections.Generic.List[object]]::new();$records.Add(@{arch=$arch;log=$log;ownership=$identity});$until=[Environment]::TickCount64+1000
 do{[uint32]$windowOwner=0;$isWindow=[ViRecenterGuardNative]::IsWindow($target.h);[ViRecenterGuardNative]::GetWindowThreadProcessId($target.h,[ref]$windowOwner)|Out-Null
 $identity.Add(@{hwnd=('0x{0:X}'-f$target.h.ToInt64());owner_pid=$windowOwner;worker_pid=$worker.Id;is_window=$isWindow;exited=$worker.HasExited;tick=[Environment]::TickCount64});SaveResult
 if($isWindow-and$windowOwner-eq$worker.Id){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until-and-not$worker.HasExited)
 "ownership HWND=$($target.h) owner=$windowOwner worker=$($worker.Id) IsWindow=$isWindow attempts=$($identity.Count)"
 Check ($windowOwner-eq$worker.Id) "guard$arch controlled HWND is owned"
 Post $target 2 1;Start-Sleep -Milliseconds 120;$noPipe=Disable $target
 Check ($noPipe.requests-gt0-and$noPipe.refusals-gt0-and$noPipe.native_calls-eq0-and(ReadLog $target)-match'RECENTER_REFUSED status_available=0 native_calls=0') "guard$arch unhooked missing-pipe refuses with zero native SetCursorPos calls"
 $idle1=Stats $target;Start-Sleep -Milliseconds 120;$idle2=Stats $target
 Check ($idle1.requests-eq$idle2.requests-and$idle2.native_calls-eq0) "guard$arch disabled recenter performs no status polling"
 $injected=& $V --json inject --pid $worker.Id|ConvertFrom-Json;Check ($LASTEXITCODE-eq0-and$injected.ok) "guard$arch injection positive control"
 $auto=Vi $worker.Id @('config','cursor=auto');Check ($auto.cursor_mode-eq0-and$auto.effective_cursor-eq'virtual'-and-not$auto.really_foreground) "guard$arch auto mode is virtual in background but not forced permission"
 Post $target 2 1;Start-Sleep -Milliseconds 120;$autoDenied=Disable $target
 Check ($autoDenied.refusals-gt$noPipe.refusals-and$autoDenied.native_calls-eq0) "guard$arch hooked auto mode still refuses native call"
 $forced=Vi $worker.Id @('config','cursor=virtual');Check ($forced.cursor_mode-eq1-and$forced.effective_cursor-eq'virtual'-and$forced.hooks_ok-match'(^| )SetCursorPos(\(alias\))?( |$)') "guard$arch exact successful cursor hook and forced virtual mode present"
 Vi $worker.Id @('json','{"cmd":"mouse","x":80,"y":70}')|Out-Null
 $before=Vi $worker.Id @('status');Check ($before.vcur_x-eq80-and$before.vcur_y-eq70) "guard$arch virtual position starts away from centre"
 Post $target 2 1;$until=[Environment]::TickCount64+2000
 do{Start-Sleep -Milliseconds 20;$sample=Stats $target;if($sample.native_calls-gt0){break}}while([Environment]::TickCount64-lt$until)
 $positive=Disable $target;$after=Vi $worker.Id @('status')
 Check ($positive.native_calls-gt0-and$after.counters.scur-$before.counters.scur-eq$positive.native_calls) "guard$arch permitted calls are witnessed by actual injected SetCursorPos counter"
 Check ($after.vcur_x-eq[math]::Floor($after.client_w/2)-and$after.vcur_y-eq[math]::Floor($after.client_h/2)-and-not$after.really_foreground) "guard$arch actual hooked calls recenter virtual coordinates in background"
 Vi $worker.Id @('config','cursor=auto')|Out-Null
 Post $target 2 1;Start-Sleep -Milliseconds 120;$revoked=Disable $target
 Check ($revoked.refusals-gt$positive.refusals-and$revoked.native_calls-eq$positive.native_calls) "guard$arch prior successful permission is never cached after forced mode removed"
 $records.Add(@{arch=$arch;pid=$worker.Id;no_pipe=$noPipe;idle_before=$idle1;idle_after=$idle2;auto_denied=$autoDenied;before=$before;positive=$positive;after=$after;revoked=$revoked})
 Stop-Process -Id $worker.Id -Force -Confirm:$false
}}
finally{SaveResult;foreach($worker in $owned){if(-not$worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false}}}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
