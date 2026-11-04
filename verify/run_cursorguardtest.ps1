# Lower spies absorb EVERY write, including old-product red controls. Sentinel is read-only.
param([string]$Out="$PSScriptRoot\..\build\test\cursor_guard",[string]$Vinput="$PSScriptRoot\..\dist\vinput.exe",[switch]$KeepGoing,[switch]$BaselineOnly)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path $Vinput).Path;$Out=[IO.Path]::GetFullPath($Out)
$nativeRoot=$root;if(-not(Test-Path "$nativeRoot\build\x64\vimh_buffer.obj")-or-not(Test-Path "$nativeRoot\src\vi_mh_rename.h")){throw 'Tested workspace MinHook objects/header unavailable'}
if(-not$Out.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cursor proof output must stay inside this checkout'}
New-Item -ItemType Directory -Force $Out|Out-Null;$run=[guid]::NewGuid().ToString('N');$tools="$Out\tools_$run";New-Item -ItemType Directory -Force "$tools\temp","$Out\calls"|Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$failures=0;$notMeasured=0;$complete=$false;$owned=$null;$serial=0
function Check($Good,$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good){$script:failures++;if(-not$KeepGoing){throw $Name}}}
function NM($Name){$script:notMeasured++;$checks.Add(@{pass=$null;status='not_measured';name=$Name});Write-Host "NOT MEASURED $Name"}
if(-not('ViCursorProofNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViCursorProofNative{
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct Last{public uint size,time;}
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")]public static extern bool GetClipCursor(out Rect clip);
 [DllImport("user32.dll")]public static extern bool GetLastInputInfo(ref Last last);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",SetLastError=true)]public static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,UIntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
}
'@}
function Sentinel{
 $point=[ViCursorProofNative+Point]::new();$clip=[ViCursorProofNative+Rect]::new();$last=[ViCursorProofNative+Last]::new();$last.size=8
 $positionOk=[ViCursorProofNative]::GetCursorPos([ref]$point);$clipOk=[ViCursorProofNative]::GetClipCursor([ref]$clip);$inputOk=[ViCursorProofNative]::GetLastInputInfo([ref]$last)
 @{tick=[Environment]::TickCount64;position_ok=$positionOk;clip_ok=$clipOk;input_ok=$inputOk;x=$point.x;y=$point.y;clip="$($clip.left),$($clip.top),$($clip.right),$($clip.bottom)";last_input=$last.time}
}
function Call([int]$TargetPid,[string[]]$Arguments,[int]$Budget=10000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$V;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;foreach($argument in @('--json','--pid',[string]$TargetPid)+$Arguments){$info.ArgumentList.Add([string]$argument)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 try{if(-not$process.Start()){throw 'owned CLI failed to start'};$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();if(-not$process.WaitForExit($Budget)){$process.Kill();$process.WaitForExit();throw 'owned CLI deadline'}
  $text=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult();$lines=@($text-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
  $script:serial++;$record=@{arguments=$Arguments;exit=$process.ExitCode;reply=$reply;stdout=$text;stderr=$err};$record|ConvertTo-Json -Depth 24|Set-Content "$Out\calls\$serial.json" -Encoding utf8NoBOM;Check ($lines.Count-eq1-and$null-ne$reply) 'one JSON reply';return $record
 }finally{$process.Dispose()}
}
function Text($Target){if(Test-Path $Target.log){Get-Content $Target.log -Raw}else{''}}
function Await($Target,[string]$Pattern,[int]$Budget=5000){$until=[Environment]::TickCount64+$Budget;do{if($Target.process.HasExited){throw 'owned fixture exited'};$text=Text $Target;if($text-match$Pattern){return $Matches};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "owned fixture witness unavailable: $Pattern"}
function Control($Target,[uint32]$Message,[uint32]$Value=0){
 [uint32]$owner=0;$thread=[ViCursorProofNative]::GetWindowThreadProcessId([IntPtr]$Target.primary,[ref]$owner);if(-not$thread-or$owner-ne$Target.pid-or$Target.process.HasExited){throw 'controlled fixture HWND/PID identity changed'}
 [UIntPtr]$result=[UIntPtr]::Zero;$ok=[ViCursorProofNative]::SendMessageTimeoutW([IntPtr]$Target.primary,$Message,[UIntPtr]$Value,[IntPtr]::Zero,2,1500,[ref]$result);if($ok-eq[IntPtr]::Zero-or$result.ToUInt64()-ne1){throw 'owned fixture control failed/expired'}
}
function Probe($Target,[string]$Name,[long]$ExpectedDelta=0,[switch]$Event,[switch]$Late,[switch]$Modeled){
 $old=@([regex]::Matches((Text $Target),'(?m)^(PROBE|LATE) seq=')).Count;$before=Sentinel
 if($Event){if($Target.process.HasExited){throw 'owned event target exited'};$request=[Threading.EventWaitHandle]::OpenExisting($Target.prefix+'.request');try{$request.Set()|Out-Null}finally{$request.Dispose()}}
 else{Control $Target $(if($Late){0x8004}else{0x8001})}
 $until=[Environment]::TickCount64+3000;$line=$null;do{$matches=[regex]::Matches((Text $Target),'(?m)^(PROBE|LATE) seq=.*$');if($matches.Count-gt$old){$line=$matches[$matches.Count-1].Value;break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 if(-not$line){throw 'absorbing boundary probe acknowledgement missing'};$after=Sentinel;$delta=-1;if($line-match' delta=(\d+)'){$delta=[long]$Matches[1]}
 $record=@{name=$Name;modeled_foreground=[bool]$Modeled;boundary_line=$line;delta=$delta;before=$before;after=$after;expected_delta=$ExpectedDelta;desktop_write_performed_by_spies=$false};$Target.evidence.Add($record)
 Check ($delta-eq$ExpectedDelta) "$Name exact absorbed native-write boundary count=$delta"
 if(-not$before.position_ok-or-not$after.position_ok-or-not$before.clip_ok-or-not$after.clip_ok){NM "$Name passive cursor/clip witness unavailable"}
 elseif($before.x-ne$after.x-or$before.y-ne$after.y-or$before.clip-ne$after.clip){NM "$Name passive desktop changed; activity or attribution unknown; never restored"}
 else{Check $true "$Name passive desktop cursor and clip observed stable"}
 return $record
}
try{foreach($arch in @('x64','x86')){
 $suffix=if($arch-eq'x64'){''}else{'32'};$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$root\src" /I"$nativeRoot\src" /I"$root\third_party\minhook\include" "$root\test\vi_cursorguard.cpp" /Fo:"$tools\fixture$suffix.obj" /Fe:"$tools\fixture$suffix.exe" /link /SUBSYSTEM:WINDOWS "$nativeRoot\build\$arch\vimh_buffer.obj" "$nativeRoot\build\$arch\vimh_hook.obj" "$nativeRoot\build\$arch\vimh_trampoline.obj" "$nativeRoot\build\$arch\vimh_hde.obj" || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /LD "$root\test\vi_cursor_late.cpp" /Fo:"$tools\late$suffix.obj" /Fe:"$tools\late$suffix.dll" || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_cursorguardstate.cpp" /Fo:"$tools\state$suffix.obj" /Fe:"$tools\state$suffix.exe" || exit /b 2
"@
 [IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch absorbing spies/fixture/late wrapper/state compile W4 WX"
 & "$tools\state$suffix.exe" *> "$tools\state_$arch.log";Check ($LASTEXITCODE-eq0) "$arch owner/lease/virtual stand-down/Clip state matrix"
 for($generation=0;$generation-lt2;$generation++){
  $prefix='Local\vi_cursor_'+[guid]::NewGuid().ToString('N');$target=@{arch=$arch;generation=$generation;pid=0;primary=0;prefix=$prefix;log="$Out\$arch.$generation.$run.log";process=$null;spies_verified=$false;evidence=[Collections.Generic.List[object]]::new()};$cases.Add($target)
  $delay=if($generation-eq0){8000}else{0};$owned=Start-Process "$tools\fixture$suffix.exe" -ArgumentList "`"$($target.log)`" `"$prefix`" --delay $delay --late `"$tools\late$suffix.dll`"" -WindowStyle Hidden -PassThru;$target.process=$owned;$target.pid=$owned.Id
  try{
   $start=Await $target 'START pid=(\d+) guard_version=1 safe_spies=1 no_native_write_trampoline_calls=1';Check ([int]$start[1]-eq$target.pid) "$arch generation$generation verified absorbing spies before ANY probe";$target.spies_verified=[int]$start[1]-eq$target.pid
   Probe $target "$arch generation$generation before injection is explicitly unprotected but absorbed" 6 -Event|Out-Null
   $inject=Call $target.pid @('inject','--hooks-only') 30000;Check ($inject.reply.ok) "$arch generation$generation hooks-only injection"
   if($generation-eq0){$status=Call $target.pid @('status');if($status.reply.hwnd-eq'0x0'){Probe $target "$arch pre-adoption cannot forward a write" 0 -Event|Out-Null}else{NM "$arch no-window startup branch not witnessed; no branch-coverage claim"}}
   $ready=Await $target 'READY .*primary=(\d+)' 12000;$target.primary=[long]$ready[1];Call $target.pid @('inject') 30000|Out-Null
   Call $target.pid @('config','blockfocus=false','cursor=real')|Out-Null
   Probe $target "$arch generation$generation real-mode background cannot write desktop" 0|Out-Null
   if($BaselineOnly){continue}
   $status=Call $target.pid @('status');Check ($status.reply.cursor_guard.hooks_ready-and$status.reply.cursor_guard.real_cursor_writes_leaked-eq$null-and-not$status.reply.cursor_guard.leak_observation_complete) "$arch generation$generation guard coverage plus honest unmeasured leak scope"
   if($generation-eq1){continue}
   Control $target 0x8003;$proxy=Await $target 'PROXY .*proxy=(\d+)';$proxyHwnd='0x{0:X}'-f[long]$proxy[1];Call $target.pid @('config',('hwnd='+$proxyHwnd),'cursor=auto')|Out-Null
   Probe $target "$arch proxy retarget grants no native permission" 0|Out-Null
   Probe $target "$arch late-loaded cached API wrapper remains guarded" 0 -Late|Out-Null
   $invalidIndex=0
   foreach($invalid in @(@{field='ms';value=-1},@{field='count';value=0},@{field='di_poll_timeout_ms';value=-1},@{field='di_release_timeout_ms';value=-1})){
    $invalidIndex++;$beforeInvalid=Call $target.pid @('status');$badWire=@{cmd='click';x=90+$invalidIndex;y=70+$invalidIndex;ms=1;gap=0;hover_frames=0;settle_ms=0;di_poll_timeout_ms=0};$badWire[$invalid.field]=$invalid.value
    $rejected=Call $target.pid @('json',($badWire|ConvertTo-Json -Compress));$afterInvalid=Call $target.pid @('status');$target.evidence.Add(@{name='invalid positioned click before hover';field=$invalid.field;before=$beforeInvalid.reply;reply=$rejected.reply;after=$afterInvalid.reply})
    Check ($rejected.exit-ne0-and-not$rejected.reply.ok-and$rejected.reply.button_down_count-eq0-and$afterInvalid.reply.cursor_guard.agent_cursor_epoch-eq$beforeInvalid.reply.cursor_guard.agent_cursor_epoch-and$afterInvalid.reply.vcur_x-eq$beforeInvalid.reply.vcur_x-and$afterInvalid.reply.vcur_y-eq$beforeInvalid.reply.vcur_y) "$arch invalid $($invalid.field) never publishes hover or admits a cursor epoch"
   }
   Control $target 0x8002 1;Start-Sleep -Milliseconds 80;$status=Call $target.pid @('status');$target.evidence.Add(@{name='misleading upstream foreground';status=$status.reply});Check (-not$status.reply.cursor_guard.foreground_verified) "$arch upstream fake foreground disagrees with independent GUI witness"
   Probe $target "$arch upstream fake foreground cannot authorize native writes" 0|Out-Null
   Control $target 0x8002 2;Call $target.pid @('json','{"cmd":"foreground-lease","ms":2000}')|Out-Null;Call $target.pid @('config','cursor=auto')|Out-Null
   $positive=Probe $target "$arch modeled foreground plus explicit lease forwards only into spies" 4 -Modeled;Check ($positive.boundary_line-match'clip_query_matched=1'-and$positive.boundary_line-match'd_clip=0'-and$positive.boundary_line-match'd_send=1'-and$positive.boundary_line-match'd_mouse=1') "$arch owner-active Clip set/query/null remains virtual"
   Call $target.pid @('config','cursor=virtual')|Out-Null;Probe $target "$arch deliberate virtual lease remains virtual" 0 -Modeled|Out-Null
   Call $target.pid @('json','{"cmd":"foreground-lease","ms":1}')|Out-Null;Start-Sleep -Milliseconds 30;Call $target.pid @('config','cursor=auto')|Out-Null;Probe $target "$arch expired lease cannot authorize native writes" 0 -Modeled|Out-Null
   $held=Call $target.pid @('pad','a','--hold');Check ($held.reply.ok-and$held.reply.hold) "$arch internal pad hold is armed before lost-cursor coverage cleanup"
   Control $target 0x8002 0;Control $target 0x8005;$status=Call $target.pid @('status');Check (-not$status.reply.cursor_guard.hooks_ready) "$arch replaced entry is reported unarmed rather than protected"
   $trace=Call $target.pid @('trace','F8');$r=Call $target.pid @('key','F8','20');$after=Call $target.pid @('trace','F8');Check ($r.exit-ne0-and$r.reply.input_injected-eq$false-and$after.reply.injected_down-eq$trace.reply.injected_down) "$arch lost coverage refuses input before any press"
   $off=Call $target.pid @('pad','OFF');$offStatus=Call $target.pid @('status');Check ($off.exit-eq0-and$off.reply.ok-and$off.reply.pad_active-eq$false-and$offStatus.reply.cursor_guard.hooks_ready-eq$false) "$arch case-insensitive OFF reports cleanup success while cursor guards remain unarmed (reply-level proof)"
  }finally{if($owned-and-not$owned.HasExited){$owned.Kill();$owned.WaitForExit()};$owned=$null}
 }
};$complete=$true}catch{$failures++;$checks.Add(@{pass=$false;name=$_.Exception.Message});Write-Host $_.Exception.Message}
finally{
 if($owned-and-not$owned.HasExited){$owned.Kill();$owned.WaitForExit()}
 foreach($case in $cases){$case.Remove('process');$case.target_log=Text $case}
 @{schema=1;completed=$complete;status=if($failures){'failed'}elseif($notMeasured){'not_measured'}else{'passed'};failed_check_count=$failures;not_measured_check_count=$notMeasured;positive_case_count=@($checks|Where-Object pass -eq $true).Count;actual_owner_foreground_verified=$false;all_native_write_spies_verified=($cases.Count-gt0-and@($cases|Where-Object{-not$_.spies_verified}).Count-eq0);write_spy_source_contract='no physical-write native trampoline is called';checks=$checks;cases=$cases;tool_issues=@()}|ConvertTo-Json -Depth 28|Set-Content "$Out\result.json" -Encoding utf8NoBOM
}
if($failures-or-not$complete){exit 1};if($notMeasured){exit 3};exit 0
