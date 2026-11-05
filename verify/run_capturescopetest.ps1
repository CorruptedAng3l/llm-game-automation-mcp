# Owned offscreen geometry tests. Lower terminal spies absorb every cursor/input/
# foreground write. PrintWindow is an owned memory-DC surrogate, never native.
param([string]$RepoRoot='', [string]$CliDir='', [string]$Out="$PSScriptRoot\..\build\test\capture_scope", [switch]$KeepGoing)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.Drawing
if(-not$RepoRoot){$d=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($i=0;$i-lt7-and$d;$i++){if(Test-Path (Join-Path $d.FullName 'build\x64\vimh_buffer.obj')){$RepoRoot=$d.FullName;break};$d=$d.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot for tested MinHook objects'};$RepoRoot=(Resolve-Path $RepoRoot).Path;if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$V=(Resolve-Path (Join-Path $CliDir 'vinput.exe')).Path
$run=Join-Path ([IO.Path]::GetFullPath($Out)) ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $run,"$run\calls","$run\tools"|Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$owned=$null;$serial=0;$token=0;$complete=$false;$failures=0
function Check($Good,[string]$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good){$script:failures++;if(-not$KeepGoing){throw $Name}}}
function Require($Good,[string]$Name){Check $Good $Name;if(-not$Good){throw $Name}}
if(-not('ViCaptureScopeNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViCaptureScopeNative{[DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();}'}
function Text($T){if(Test-Path $T.log){Get-Content -LiteralPath $T.log -Raw}else{''}}
function Records($T){foreach($line in ((Text $T)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Await($T,[string]$Event,[long]$Token,[int]$Budget=4000){$end=[Environment]::TickCount64+$Budget;do{foreach($r in @(Records $T)){if($r.event-eq$Event-and$r.token-eq$Token){return $r}};if($T.process.HasExited){throw 'Owned fixture exited'};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end);throw "Missing $Event token$Token acknowledgement"}
function Control($T,[uint32]$Message,[string]$Event){if($T.process.HasExited){throw 'Owned process exited'};[uint32]$owner=0;$thread=[ViCaptureScopeNative]::GetWindowThreadProcessId($T.control,[ref]$owner);if(-not$thread-or$owner-ne$T.process.Id-or$thread-ne$T.thread){throw 'Owned control HWND/PID/thread changed'};$script:token++;if(-not[ViCaptureScopeNative]::PostMessageW($T.control,$Message,[UIntPtr]::new([uint64]$script:token),[IntPtr]::Zero)){throw 'Owned control post failed'};return Await $T $Event $script:token}
function Snap($T){Control $T 0x8050 snapshot}
function Foreground{[uint32]$owner=0;$h=[ViCaptureScopeNative]::GetForegroundWindow();[void][ViCaptureScopeNative]::GetWindowThreadProcessId($h,[ref]$owner);return @{hwnd=$h.ToInt64();pid=$owner;read_only=$true}}
function Call($T,[string[]]$Arguments,[int]$Budget=9000){$s=[Diagnostics.ProcessStartInfo]::new();$s.FileName=$V;$s.UseShellExecute=$false;$s.CreateNoWindow=$true;$s.RedirectStandardOutput=$true;$s.RedirectStandardError=$true;foreach($arg in @('--json','--pid',[string]$T.process.Id)+$Arguments){[void]$s.ArgumentList.Add([string]$arg)};$p=[Diagnostics.Process]::new();$p.StartInfo=$s
 try{if(-not$p.Start()){throw 'CLI start failed'};$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();if(-not$p.WaitForExit($Budget)){$p.Kill();$p.WaitForExit();throw 'Owned CLI deadline'};$raw=$out.GetAwaiter().GetResult();$lines=@($raw-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}};$script:serial++;$r=@{arguments=$Arguments;exit=$p.ExitCode;reply=$reply;stdout=$raw;stderr=$err.GetAwaiter().GetResult()};$r|ConvertTo-Json -Depth 25|Set-Content "$run\calls\$script:serial.json" -Encoding utf8NoBOM;Require ($lines.Count-eq1-and$null-ne$reply) "$($T.arch) one JSON reply";$T.calls.Add($r);return $r}finally{$p.Dispose()}}
function Json($T,$Value){Call $T @('json',($Value|ConvertTo-Json -Compress -Depth 6))}
function Capture($T,[string]$Name,[switch]$Crop){$path="$run\$($T.arch).$Name.png";$a=@('capture',$path,'--method','printwindow','--scale','0.5');if($Crop){$a+=@('--region','80,60,320,240')};$r=Call $T $a;Require ($r.exit-eq0-and$r.reply.ok-and(Test-Path $path)) "$($T.arch) fresh $Name capture";return $r}
function Click($T){Call $T @('click','left','40','30','--capture-space','--hover-frames','0','--settle-ms','0','--ms','1','--di-poll-timeout-ms','0')}
function Refuse($T,[string]$Name,[switch]$Hover){$b=Snap $T;$sb=Call $T @('status');$tb=Json $T @{cmd='trace';key='lbutton'};$r=Click $T;$a=Snap $T;$sa=Call $T @('status');$ta=Json $T @{cmd='trace';key='lbutton'};$ok=$r.exit-ne0-and-not$r.reply.ok-and($a.adown+$a.bdown)-eq($b.adown+$b.bdown)-and$ta.reply.injected_down-eq$tb.reply.injected_down
 if(-not$Hover){$ok=$ok-and$r.reply.input_injected-eq$false-and$a.amoves-eq$b.amoves-and$a.bmoves-eq$b.bmoves-and$sa.reply.cursor_guard.agent_cursor_epoch-eq$sb.reply.cursor_guard.agent_cursor_epoch};Check $ok "$($T.arch) $Name refuses stale screenshot input before any down";$T.evidence.Add(@{name=$Name;before=$b;after=$a;before_status=$sb.reply;after_status=$sa.reply;reply=$r.reply;hover_allowed=[bool]$Hover});return $r}
function StopOwned($T){if(-not$T-or-not$T.process-or$T.process.HasExited){return};$p=Get-Process -Id $T.process.Id -ErrorAction SilentlyContinue;if($p-and$p.Path-eq$T.path-and$p.StartTime.ToUniversalTime().Ticks-eq$T.created){$T.process.Kill();$T.process.WaitForExit()}}
# Retain all original assertions; strengthen lifecycle authority for both the old
# controls and appended text controls using original process handles only.
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
$cliProcesses=[Collections.Generic.List[object]]::new()
function TrackOriginal($P,[string]$Image){
 $t=@{process=$P;pid=0;path=$Image;created=0L;retained_handle=0L;cleanup=$null;cache=$null;startup_error=$null}
 try{$t.pid=$P.Id;$t.retained_handle=$P.Handle.ToInt64();$t.created=$P.StartTime.ToUniversalTime().Ticks;$seen=ObserveStartupImage $P $Image $t.created $t.retained_handle;if(-not$seen.matched){throw ('original startup identity failed: '+$seen.reason)};$t.cache=New-ViStrictCliStartupCache (ReadRetainedImage $P $Image $t.created $t.retained_handle) $t.retained_handle ([uint32]$t.pid) $t.created $Image}catch{$t.startup_error=$_.Exception.Message}
 return $t
}
function LiveOriginal($T){$proof=ReadRetainedImage $T.process $T.path $T.created $T.retained_handle;if(-not$proof.matched){throw ('retained original fixture identity failed: '+$proof.reason)}}
function Control($T,[uint32]$Message,[string]$Event){LiveOriginal $T;[uint32]$owner=0;$thread=[ViCaptureScopeNative]::GetWindowThreadProcessId($T.control,[ref]$owner);if(-not$thread-or$owner-ne$T.process.Id-or$thread-ne$T.thread){throw 'Owned control HWND/PID/thread changed'};$script:token++;if(-not[ViCaptureScopeNative]::PostMessageW($T.control,$Message,[UIntPtr]::new([uint64]$script:token),[IntPtr]::Zero)){throw 'Owned control post failed'};return Await $T $Event $script:token}
function StopOwned($T){
 if(-not$T-or-not$T.process-or$T.cleanup){return}
 $T.cleanup=if($T.cache){AccountOrReapCli $T.process $T.path $T.created $T.retained_handle ([uint32]$T.pid) $T.cache}else{Reap $T.process $T.path $T.created $T.retained_handle}
 if($T.startup_error){$T.process.Dispose();throw ('owned fixture startup failed: '+$T.startup_error)}
 if(-not$T.cleanup.completion_owned-or$T.cleanup.error){throw ('owned fixture cleanup failed: '+$T.cleanup.error)}
 $T.process.Dispose()
}
function Call($T,[string[]]$Arguments,[int]$Budget=9000){
 LiveOriginal $T
 $s=[Diagnostics.ProcessStartInfo]::new();$s.FileName=$V;$s.UseShellExecute=$false;$s.CreateNoWindow=$true;$s.RedirectStandardOutput=$true;$s.RedirectStandardError=$true
 foreach($arg in @('--json','--pid',[string]$T.process.Id)+$Arguments){[void]$s.ArgumentList.Add([string]$arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$s;$tracked=$null;$out=$null;$err=$null
 try{
  if(-not$p.Start()){throw 'CLI start failed'};$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();$tracked=TrackOriginal $p $V;$cliProcesses.Add($tracked);if($tracked.startup_error){throw $tracked.startup_error}
  if(-not$p.WaitForExit($Budget)){throw 'Owned CLI deadline'}
  if(-not$out.Wait(3000)-or-not$err.Wait(3000)){throw 'Owned CLI output drain deadline'}
  $raw=$out.Result;$lines=@($raw-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
  $script:serial++;$r=@{arguments=$Arguments;exit=$p.ExitCode;reply=$reply;stdout=$raw;stderr=$err.Result};$r|ConvertTo-Json -Depth 25|Set-Content "$run\calls\$script:serial.json" -Encoding utf8NoBOM
  Require ($lines.Count-eq1-and$null-ne$reply) "$($T.arch) one JSON reply";$T.calls.Add($r);return $r
 }finally{
  if($tracked){$tracked.cleanup=if($tracked.cache){AccountOrReapCli $p $tracked.path $tracked.created $tracked.retained_handle ([uint32]$tracked.pid) $tracked.cache}else{Reap $p $tracked.path $tracked.created $tracked.retained_handle};$tracked.Remove('process');if($tracked.startup_error-or-not$tracked.cleanup.completion_owned-or$tracked.cleanup.error){throw ('owned CLI accounting failed: '+$tracked.startup_error+' '+$tracked.cleanup.error)}}
  $p.Dispose()
 }
}
function PrintRows($T){@(Records $T|Where-Object event -eq print_callback)}
function LabelMode($T,[uint32]$Message,[string]$Event){$r=Control $T $Message $Event;Require $r.ok "$($T.arch) guarded $Event control";return $r}
function GeometryWitness($T,[string]$Name){$path=Join-Path $run ($T.arch+'.'+$Name+'.png');$r=Json $T @{cmd='capture';path=$path;method='printwindow';preserve_capture_space=$true};Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.capture_space_updated-eq$false-and$r.reply.capture_scope-and(Test-Path -LiteralPath $path)) "$($T.arch) independent geometry scope witness preserves the saved map";return $r.reply.capture_scope}
function GeometryWarning($Saved,$Current){return "client size or observed geometry changed since the last capture: $($Saved.client_w)x$($Saved.client_h) -> $($Current.client_w)x$($Current.client_h), geometry epoch $($Saved.geometry_epoch) -> $($Current.geometry_epoch); capture again before choosing coordinates"}
function TestPlacementScope($T){
 $arch=$T.arch;$fresh=Capture $T placement_scope;$scope=$fresh.reply.capture_scope|ConvertTo-Json -Compress -Depth 6;$before=Snap $T
 $arguments=@('placement-drag','40','50','240','170','--steps','1','--ms','1','--hold-ms','1','--dwell-ms','0','--poll-timeout-ms','50','--scope-json',$scope)
 $r=Call $T $arguments;$after=Snap $T;$terminal=Call $T @('job-status',[string]$r.reply.job_id);$released=Json $T @{cmd='trace';key='lbutton'}
 Check ($r.exit-ne0-and-not$r.reply.ok-and$r.reply.capture_scope_checked-eq$true-and$r.reply.job_kind-eq'placement'-and$r.reply.job_complete-eq$true-and$r.reply.job_state-eq'failed'-and$r.reply.job_reason-eq'start_position_not_observed'-and$r.reply.button_released-eq$true-and$terminal.reply.job.job_id-eq$r.reply.job_id-and$terminal.reply.job.job_complete-eq$true-and$terminal.reply.job.capture_scope_checked-eq$true-and$terminal.reply.job.button_released-eq$true-and-not$released.reply.virtual_down-and$after.amoves-gt$before.amoves-and$after.adown-eq$before.adown-and$after.aup-eq$before.aup) "$arch fresh placement scope reaches bounded polling then exact terminal release without claiming a placement"
 $T.evidence.Add(@{name='fresh explicit placement scope';before=$before;after=$after;scope=$fresh.reply.capture_scope;reply=$r.reply;terminal=$terminal.reply;released=$released.reply})
 $null=Control $T 0x8051 resize;$before=Snap $T;$statusBefore=(Call $T @('status')).reply;$r=Call $T $arguments;$after=Snap $T;$statusAfter=(Call $T @('status')).reply
 Check ($r.exit-ne0-and-not$r.reply.ok-and$r.reply.input_injected-eq$false-and$r.reply.error-match'scope'-and-not$r.reply.job_id-and-not$r.reply.placement_preflight_refused-and$statusBefore.placement_scope_version-eq1-and$after.amoves-eq$before.amoves-and$after.bmoves-eq$before.bmoves-and$after.adown-eq$before.adown-and$after.bdown-eq$before.bdown-and$after.aup-eq$before.aup-and$after.bup-eq$before.bup-and$statusAfter.cursor_guard.agent_cursor_epoch-eq$statusBefore.cursor_guard.agent_cursor_epoch) "$arch stale explicit placement scope refuses before any movement or button edge"
 $T.evidence.Add(@{name='stale explicit placement scope';before=$before;after=$after;status_before=$statusBefore;status_after=$statusAfter;scope=$fresh.reply.capture_scope;reply=$r.reply})
 $null=Control $T 0x8058 reset_size
}
function TestTextAndPointer($T){
 $arch=$T.arch;$before=Snap $T
 # Three ordinary movements cannot establish a DirectInput consumer.
 foreach($x in @(120,140,160)){$r=Call $T @('mouse',[string]$x,'220','--steps','1');Require ($r.exit-eq0-and$r.reply.ok) "$arch ordinary message mouse movement"}
 $status=(Call $T @('status')).reply
 Check ($status.directinput_mouse.poll_channel-eq'none'-and$status.directinput_mouse.consumer_observed-eq$false-and$status.directinput_mouse.moves_since_poll-ge3-and$status.directinput_mouse.note-and-not$status.directinput_mouse.warning) "$arch three moves with no DI consumer report a note rather than a reader-failure warning"
 $b=Snap $T;$r=Call $T @('click','left','10000','-100','--hover-frames','0','--settle-ms','0','--ms','1','--di-poll-timeout-ms','0');$a=Snap $T
 Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.coordinate_warning-and$r.reply.resolved_x-eq10000-and$r.reply.resolved_y-eq-100-and$r.reply.clamped_x-eq639-and$r.reply.clamped_y-eq0-and$r.reply.button_down_count-eq1-and$a.adown-eq$b.adown+1-and$a.aup-eq$b.aup+1-and$a.ax-eq639-and$a.ay-eq0) "$arch outside click reports requested/clamped coordinates and one exact guarded edge press"
 $base=@('PLAY','--method','printwindow','--preprocess','none','--scale','1','--no-tiles','--exact-line=true','--whole-word=true')
 $null=LabelMode $T 0x8060 label_static;$b=Snap $T;$prior=(PrintRows $T).Count;$r=Call $T (@('click-text')+$base+@('--timeout','0','--hover-frames','0','--settle-ms','0','--ms','1'));$a=Snap $T;$printed=@(PrintRows $T|Select-Object -Skip $prior)
 Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.clicked-and$r.reply.polls-eq1-and$printed.Count-eq1-and$a.adown-eq$b.adown+1-and$a.aup-eq$b.aup+1-and$a.ax-eq$r.reply.match.center[0]-and$a.ay-eq$r.reply.match.center[1]) "$arch zero dwell keeps one captured PLAY observation and one ordinary click"
 $null=LabelMode $T 0x8061 label_finite;$b=Snap $T;$prior=(PrintRows $T).Count;$r=Call $T (@('click-text')+$base+@('--stable-ms','300','--timeout','3000','--hover-frames','0','--settle-ms','0','--ms','1'));$a=Snap $T;$printed=@(PrintRows $T|Select-Object -Skip $prior)
 $positions=@($printed|ForEach-Object label_x|Select-Object -Unique);$latest=$printed|Select-Object -Last 1
 Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.clicked-and$r.reply.box_stability_ready-and$r.reply.box_stability_ms_observed-ge300-and$r.reply.box_stability_samples-ge2-and$r.reply.polls-ge4-and$printed.Count-eq$r.reply.polls-and$positions.Count-eq3-and$latest.label_x-eq320) "$arch moving PLAY requires distinct captures before a measured stable dwell"
 Check ($r.reply.click_acknowledgement_confirmed-and$r.reply.do_not_repeat-and$r.reply.click.input_admission_version-eq1-and$r.reply.click.capture_scope_checked-and$r.reply.click.button_down_count-eq1-and$r.reply.click.button_released-and$r.reply.click.admission_deadline_qpc_ticks-eq$r.reply.admission_deadline_qpc_ticks-and$a.adown-eq$b.adown+1-and$a.aup-eq$b.aup+1-and$a.ax-eq$r.reply.match.center[0]-and$a.ay-eq$r.reply.match.center[1]-and$a.ax-gt320-and$a.ax-lt520-and$a.ay-gt104-and$a.ay-lt184) "$arch stable PLAY sends one deadline-scoped click at the latest matched box with exact acknowledgement"
 $T.evidence.Add(@{name='finite moving PLAY then stable';before=$b;after=$a;reply=$r.reply;printed=$printed})
 $null=LabelMode $T 0x8062 label_moving;$b=Snap $T;$prior=(PrintRows $T).Count;$r=Call $T (@('click-text')+$base+@('--stable-ms','300','--timeout','1000','--hover-frames','0','--settle-ms','0','--ms','1'));$a=Snap $T;$printed=@(PrintRows $T|Select-Object -Skip $prior)
 Check ($r.exit-ne0-and-not$r.reply.ok-and$r.reply.timed_out-and$r.reply.match_count-ge1-and$r.reply.error-match'position-stable'-and$printed.Count-ge3-and@($printed|ForEach-Object label_x|Select-Object -Unique).Count-eq2-and$a.adown-eq$b.adown-and$a.aup-eq$b.aup) "$arch continuously moving PLAY times out without any press"
 $T.evidence.Add(@{name='continually moving PLAY';before=$b;after=$a;reply=$r.reply;printed=$printed})
 $null=LabelMode $T 0x8060 label_static;$b=Snap $T;$prior=(PrintRows $T).Count;$proof=Join-Path $run ($arch+'.wait_PLAY.png');$r=Call $T (@('wait-text')+$base+@('--timeout','1000','--observation-capture',$proof));$completedQpc=[Diagnostics.Stopwatch]::GetTimestamp();$a=Snap $T;$printed=@(PrintRows $T|Select-Object -Skip $prior);$latest=$printed|Select-Object -Last 1
 $image=$null;$ink=0;$dimensions=$null;$stamp=$null;$plaque=$null
 try{$image=[Drawing.Bitmap]::new($proof);$dimensions=@($image.Width,$image.Height);$stamp=$image.GetPixel(12,12);$plaque=$image.GetPixel($latest.label_x+2,106);$box=$r.reply.match.box;for($y=[int]$box[1];$y-lt$box[1]+$box[3];$y++){for($x=[int]$box[0];$x-lt$box[0]+$box[2];$x++){$pixel=$image.GetPixel($x,$y);if($pixel.R-lt40-and$pixel.G-lt40-and$pixel.B-lt40){$ink++}}}}finally{if($image){$image.Dispose()}}
 Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.found-and$r.reply.polls-eq1-and$printed.Count-eq1-and$r.reply.observation_frame_path-eq$proof-and$dimensions[0]-eq640-and$dimensions[1]-eq480-and$stamp.R-eq$latest.stamp_r-and$stamp.G-eq$latest.stamp_g-and$stamp.B-eq$latest.stamp_b-and$plaque.R-eq255-and$plaque.G-eq255-and$plaque.B-eq255-and$ink-gt30) "$arch wait-text proof contains the exact matched PLAY frame pixels/dimensions and capture stamp without recapture"
 $ready=@(Records $T|Where-Object event -eq ready)|Select-Object -First 1
 Check ($r.reply.qpc_frequency-eq$latest.qpc_frequency-and[uint64]$r.reply.capture_qpc_ticks-ge[uint64]$latest.qpc_after-and[uint64]$r.reply.capture_qpc_ticks-le[uint64]$completedQpc-and$r.reply.capture_scope.hwnd-eq('0x{0:X}'-f[uint64]$ready.primary)-and$r.reply.capture_scope.thread-eq$T.thread-and$r.reply.capture_scope.client_w-eq640-and$r.reply.capture_scope.client_h-eq480-and$a.adown-eq$b.adown-and$a.aup-eq$b.aup) "$arch wait-text image retains its original scoped QPC receipt and sends no click"
 $T.evidence.Add(@{name='same-frame wait PLAY proof';before=$b;after=$a;reply=$r.reply;printed=$printed;dimensions=$dimensions;black_ink_pixels=$ink;completed_qpc=[string]$completedQpc})
 $prior=(PrintRows $T).Count;$r=Call $T (@('wait-text')+$base+@('--timeout','1000','--observation-capture',$run));$a=Snap $T;$printed=@(PrintRows $T|Select-Object -Skip $prior)
 Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.found-and$r.reply.observation_frame_error-and-not$r.reply.observation_frame_path-and$printed.Count-eq1-and$a.adown-eq$b.adown-and$a.aup-eq$b.aup) "$arch proof save failure is advisory to the successful read-only match"
 $null=LabelMode $T 0x8063 label_off;$T.evidence.Add(@{name='pointer and text controls complete';before=$before;after=(Snap $T)})
}
try{foreach($arch in @('x64','x86')){
 $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$folder="$run\tools\$arch";New-Item -ItemType Directory -Force "$folder\temp"|Out-Null;$source=(Resolve-Path "$PSScriptRoot\..\test\vi_capturescope.cpp").Path
 $batch=@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\src" /I"$RepoRoot\third_party\minhook\include" "$source" /Fo:"$folder\fixture.obj" /Fe:"$folder\fixture.exe" /link /SUBSYSTEM:WINDOWS "$RepoRoot\build\$arch\vimh_buffer.obj" "$RepoRoot\build\$arch\vimh_hook.obj" "$RepoRoot\build\$arch\vimh_trampoline.obj" "$RepoRoot\build\$arch\vimh_hde.obj"
exit /b %errorlevel%
"@;[IO.File]::WriteAllText("$folder\build.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$folder\build.cmd" *> "$folder\build.log";Require ($LASTEXITCODE-eq0) "$arch safe fixture builds W4 WX"
 $exe=(Resolve-Path "$folder\fixture.exe").Path;$log="$run\$arch.fixture.jsonl";$p=Start-Process $exe -ArgumentList ('"'+$log+'"') -WindowStyle Hidden -PassThru;$owned=TrackOriginal $p $exe;$owned.arch=$arch;$owned.log=$log;$owned.calls=[Collections.Generic.List[object]]::new();$owned.evidence=[Collections.Generic.List[object]]::new();$cases.Add($owned);if($owned.startup_error){throw $owned.startup_error}
 try{$end=[Environment]::TickCount64+4000;$ready=$null;do{$ready=@(Records $owned|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
  Require ($ready-and$ready.pid-eq$p.Id-and$ready.safe_spies-and$ready.owned_print_surrogate-and$ready.native_write_trampolines-eq$false-and$ready.native_printwindow_called-eq$false) "$arch absorbing spies and memory-DC backend verified before injection"
  Require ($ready.terminal_writer_guards-eq19-and$ready.labels_default_off) "$arch all19 writer guards and labels-off original-pixel prerequisite"
  $owned.control=[IntPtr]::new([long]$ready.secondary);[uint32]$owner=0;$owned.thread=[ViCaptureScopeNative]::GetWindowThreadProcessId($owned.control,[ref]$owner);Require ($owner-eq$p.Id-and$owned.thread-ne0) "$arch control identity";$owned.before=Foreground;Require ($owned.before.pid-ne$p.Id) "$arch actual foreground outside fixture"
  $r=Call $owned @('inject') 30000;Require ($r.exit-eq0-and$r.reply.ok) "$arch injection";$r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.primary),'msgs=on','raw=off','blockfocus=true');Require ($r.exit-eq0-and$r.reply.ok) "$arch exact primary adoption"
  $r=Json $owned @{cmd='mouse';x=0.5;y=0.5;norm=$true;space='capture'};Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.x-eq320-and$r.reply.y-eq240) "$arch norm precedence without a saved map"
  $saved=Capture $owned scaled_crop -Crop;$b=Snap $owned;$r=Click $owned;$a=Snap $owned;Check ($r.exit-eq0-and$r.reply.ok-and$saved.reply.w-eq160-and$saved.reply.h-eq120-and$a.adown-eq$b.adown+1-and$a.ax-eq160-and$a.ay-eq120) "$arch fresh crop/scale maps PNG40,30 to client160,120"
  $second=Capture $owned repeated_crop -Crop;$third=Capture $owned repeated_crop_third -Crop
  Check ($second.reply.capture_observation.pixel_hash_changed-eq$false-and$third.reply.capture_observation.identical_captures-ge3-and$third.reply.capture_observation.frame_freshness-eq'unmeasured_printwindow'-and$null-eq$third.reply.capture_observation.rendered_frame_age_ms-and$third.reply.capture_freshness_warning) "$arch repeated source hashes retain comparison and unknown PrintWindow frame age"
  $beforeTypes=(Call $owned @('status')).reply;Require ([ViCaptureScopeNative]::PostMessageW([IntPtr]::new([long]$ready.primary),0x0200,[UIntPtr]::Zero,[IntPtr]::Zero)) "$arch own unrecognized move post";$null=Snap $owned;$afterTypes=(Call $owned @('status')).reply
  Check ($afterTypes.counters.wm_mouse_unrecognized_dropped-gt$beforeTypes.counters.wm_mouse_unrecognized_dropped-and$afterTypes.mouse_message_unrecognized_types.WM_MOUSEMOVE-gt$beforeTypes.mouse_message_unrecognized_types.WM_MOUSEMOVE) "$arch actual isolated message names its unrecognized type without attributing it to agent delivery"
  $bitmap=[Drawing.Bitmap]::new("$run\$arch.scaled_crop.png");try{$pixel=$bitmap.GetPixel(5,5);$png=@{width=$bitmap.Width;height=$bitmap.Height;r=$pixel.R;g=$pixel.G;b=$pixel.B}}finally{$bitmap.Dispose()};$owned.evidence.Add(@{name='independent PNG source/encoder witness';png=$png});Check ($png.width-eq160-and$png.height-eq120-and$png.r-eq30-and$png.g-eq80-and$png.b-eq150) "$arch independently decoded crop PNG has expected dimensions and blue interior pixel"
  $resize=Control $owned 0x8051 resize;Require ($resize.ok-and$resize.aw-eq800-and$resize.ah-eq600) "$arch resize witness"
  $geometry=(Call $owned @('status')).reply;$currentScope=GeometryWitness $owned resized_scope;$savedScope=$third.reply.capture_scope
  Check ($geometry.geometry_warning-ceq(GeometryWarning $savedScope $currentScope)-and$savedScope.client_w-eq640-and$savedScope.client_h-eq480-and$currentScope.client_w-eq$resize.aw-and$currentScope.client_h-eq$resize.ah-and[uint64]$currentScope.geometry_epoch-gt[uint64]$savedScope.geometry_epoch) "$arch resize status identifies saved capture and current client geometry before refusal"
  Check (@($geometry.PSObject.Properties).Count-le128-and-not$geometry.PSObject.Properties['capture_geometry_before']-and-not$geometry.PSObject.Properties['client_geometry_now']) "$arch resized native status remains within the existing128-field config parser budget"
  $owned.evidence.Add(@{name='consolidated resize geometry warning';status=$geometry;saved_scope=$savedScope;current_scope=$currentScope;top_level_fields=@($geometry.PSObject.Properties).Count})
  $null=Refuse $owned resized
  $resizedCapture=Capture $owned after_resize;$r=Click $owned;$a=Snap $owned;Check ($r.exit-eq0-and$r.reply.ok-and$a.ax-eq80-and$a.ay-eq60) "$arch fresh capture restores transformed input"
  $b=Snap $owned;$aba=Control $owned 0x8052 resize_aba;Require ($aba.ok-and$aba.aw-eq$b.aw-and$aba.ah-eq$b.ah-and$aba.asizes-ge$b.asizes+2) "$arch actual observed resize-away/back witness"
  $geometry=(Call $owned @('status')).reply;$currentScope=GeometryWitness $owned aba_scope;$savedScope=$resizedCapture.reply.capture_scope
  Check ($geometry.geometry_warning-ceq(GeometryWarning $savedScope $currentScope)-and$savedScope.client_w-eq800-and$savedScope.client_h-eq600-and$currentScope.client_w-eq800-and$currentScope.client_h-eq600-and[uint64]$currentScope.geometry_epoch-gt[uint64]$savedScope.geometry_epoch) "$arch same-size resize ABA reports its changed observed geometry epoch"
  $owned.evidence.Add(@{name='consolidated same-size ABA warning';status=$geometry;saved_scope=$savedScope;current_scope=$currentScope;top_level_fields=@($geometry.PSObject.Properties).Count})
  $null=Refuse $owned resize_away_back
  $down=Call $owned @('mousedown','right');$held=Json $owned @{cmd='trace';key='rbutton'};$up=Call $owned @('mouseup','right','40','30','--capture-space');$released=Json $owned @{cmd='trace';key='rbutton'};Check ($down.reply.ok-and$held.reply.virtual_down-and-not$up.reply.ok-and$up.reply.button_released-and-not$released.reply.virtual_down) "$arch stale capture-space mouseup still releases"
  $r=Json $owned @{cmd='mouse';x=0.5;y=0.5;norm=$true;space='capture'};Check ($r.reply.ok-and$r.reply.x-eq400-and$r.reply.y-eq300) "$arch norm precedence after stale capture"
  $null=Control $owned 0x8058 reset_size;$null=Capture $owned before_hover;$null=Control $owned 0x8054 arm_hover;$previous=@(Records $owned|Where-Object event -eq hover_resize).Count;$null=Refuse $owned resize_during_hover -Hover;$witness=@(Records $owned|Where-Object event -eq hover_resize);Check ($witness.Count-eq$previous+1-and$witness[-1].ok-and$witness[-1].aw-eq800) "$arch outer hover-resize callback precedes root delivery acknowledgement";$null=Control $owned 0x8055 restore_outer
  $null=Control $owned 0x8058 reset_size;$prior=Capture $owned before_readback;$null=Control $owned 0x8056 arm_print;$previous=@(Records $owned|Where-Object event -eq print_callback).Count;$inflight=Call $owned @('capture',"$run\$arch.inflight.png",'--method','printwindow');$callbacks=@(Records $owned|Where-Object event -eq print_callback);$witness=@(Records $owned|Where-Object event -eq print_resize)|Select-Object -Last 1
  Require ($callbacks.Count-gt$previous-and$witness.ok-and$witness.aw-eq900-and$witness.ah-eq700-and$callbacks[-1].native_printwindow_called-eq$false) "$arch inflight scope change has positive owned backend callback witness"
  Check ($inflight.exit-ne0-and-not$inflight.reply.ok-and$inflight.reply.capture_space_updated-eq$false-and$inflight.reply.capture_scope_reason) "$arch readback scope change cannot commit mixed-geometry map";$null=Refuse $owned after_failed_readback
  $null=Control $owned 0x8058 reset_size;$null=Capture $owned before_retarget;$shown=Control $owned 0x8053 show_secondary;Require ($shown.aw-eq640-and$shown.bw-eq640-and$shown.ah-eq480-and$shown.bh-eq480) "$arch owned retarget windows have identical client sizes";$r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.secondary));Require ($r.reply.ok) "$arch secondary retarget";$null=Refuse $owned retarget_same_geometry
  $r=Call $owned @('config',('hwnd=0x{0:X}'-f[uint64]$ready.primary));Require ($r.reply.ok) "$arch primary readoption";$null=Refuse $owned readopt_same_hwnd
  $null=Capture $owned before_cookie;$cookie=Control $owned 0x8057 rotate_lifetime;Require ($cookie.ok-and$cookie.old_cookie-ne$cookie.new_cookie-and$cookie.numeric_hwnd_reuse-eq$false) "$arch lifetime-cookie replacement witness is explicitly not numeric HWND reuse";$null=Refuse $owned changed_lifetime_witness
  $null=Capture $owned final;$r=Click $owned;Check ($r.exit-eq0-and$r.reply.ok) "$arch fresh capture after witness replacement works"
  TestTextAndPointer $owned
  TestPlacementScope $owned
  $owned.after=Foreground;$last=Snap $owned;Check ($owned.after.pid-ne$p.Id) "$arch actual foreground remains outside fixture";$owned.evidence.Add(@{name='safety';before=$owned.before;after=$owned.after;state=$last;native_printwindow_used=$false;numeric_hwnd_reuse_measured=$false;geometry_scope='forwarded WM_SIZE/WM_DPICHANGED plus current dimensions; lifecycle swallowed above vinput does not provide an ABA witness'})
 }finally{StopOwned $owned;$owned=$null}
};$complete=$true}catch{$failures++;$checks.Add(@{pass=$false;name=$_.Exception.Message});Write-Host $_.Exception.Message}
finally{if($owned){StopOwned $owned};foreach($case in $cases){$case.Remove('process');$case.target_log=Text $case};@{schema=1;completed=$complete;status=if($failures){'failed'}else{'passed'};failed_check_count=$failures;positive_case_count=@($checks|Where-Object pass -eq $true).Count;checks=$checks;cases=$cases;cli_processes=$cliProcesses;capture_backend='owned lower PrintWindow memory-DC surrogate';native_printwindow_measured=$false;numeric_hwnd_reuse_measured=$false;actual_human_foreground_takeover_measured=$false;tool_issues=@()}|ConvertTo-Json -Depth 30|Set-Content "$run\result.json" -Encoding utf8NoBOM}
Write-Host "$($checks.Count-$failures) PASS / $failures FAIL ($run)";if($failures-or-not$complete){exit 1};exit 0
