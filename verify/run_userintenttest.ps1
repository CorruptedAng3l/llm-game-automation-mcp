# Real mouse/Alt+Tab selection of owned onscreen windows. ALL real input is under the canonical lock.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\user_intent",[int]$LockWaitSec=300,[switch]$InputSourceProbeOnly,[switch]$BaselineControl,[switch]$LaunchControl,[switch]$ValidateHarness,[switch]$KeepForegroundSentinel,[switch]$AltTabOnly,[switch]$BootstrapOnly,[switch]$BootstrapAtCursor)
if($BootstrapAtCursor){$BootstrapOnly=$true}
if($BootstrapOnly -and ($InputSourceProbeOnly -or $BaselineControl -or $LaunchControl -or $AltTabOnly)){throw 'BootstrapOnly cannot combine with input, launch, or baseline control modes'}
$ErrorActionPreference='Stop';$D=(Resolve-Path "$PSScriptRoot\..\dist").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class UserIntentNative {
 [StructLayout(LayoutKind.Sequential)]public struct Pt {public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct Rc {public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct Mouse {public int x,y;public uint data,flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Sequential)]public struct Key {public ushort vk,scan;public uint flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Explicit)]public struct Union {[FieldOffset(0)]public Mouse mouse;[FieldOffset(0)]public Key key;}
 [StructLayout(LayoutKind.Sequential)]public struct Input {public uint type;public Union value;}
 [DllImport("user32.dll",SetLastError=true)]static extern uint SendInput(uint n,Input[] input,int size);
 public static uint Button(bool up){var i=new Input();i.value.mouse.flags=up?4u:2u;return SendInput(1,new[]{i},Marshal.SizeOf<Input>());}
 public static uint Keyboard(int vk,bool up){var i=new Input();i.type=1;i.value.key.vk=(ushort)vk;i.value.key.flags=up?2u:0u;return SendInput(1,new[]{i},Marshal.SizeOf<Input>());}
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Pt p);
 [DllImport("user32.dll")]public static extern bool GetPhysicalCursorPos(out Pt p);
 [DllImport("user32.dll")]public static extern IntPtr GetThreadDpiAwarenessContext();
 [DllImport("user32.dll")]public static extern int GetAwarenessFromDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll",SetLastError=true)]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(Pt p);
 [DllImport("user32.dll")]public static extern IntPtr GetAncestor(IntPtr h,uint f);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rc r);
 [DllImport("user32.dll")]public static extern bool GetClipCursor(out Rc r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int z,uint flags);
 [DllImport("user32.dll")]public static extern short GetAsyncKeyState(int v);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int index);
 public static string ForegroundFacts(){var h=GetForegroundWindow();uint pid;GetWindowThreadProcessId(h,out pid);var name=new System.Text.StringBuilder(128);GetClassName(h,name,128);return $"tick={Environment.TickCount64} hwnd=0x{h.ToInt64():X} pid={pid} class={name} visible={IsWindowVisible(h)} style=0x{GetWindowLongPtrW(h,-16).ToInt64():X} ex=0x{GetWindowLongPtrW(h,-20).ToInt64():X}";}
 [StructLayout(LayoutKind.Sequential)]struct LastInput {public uint size,time;}
 [DllImport("user32.dll")]static extern bool GetLastInputInfo(ref LastInput input);
 public static uint LastInputTick(){var input=new LastInput();input.size=(uint)Marshal.SizeOf<LastInput>();if(!GetLastInputInfo(ref input))throw new Exception("GetLastInputInfo failed");return input.time;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,System.Text.StringBuilder value,int length);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
 [StructLayout(LayoutKind.Sequential)]struct Msg {public IntPtr h;public uint m;public UIntPtr w;public IntPtr l;public uint t;public Pt p;public uint extra;}
 delegate void EventProc(IntPtr hook,uint kind,IntPtr h,int obj,int child,uint thread,uint time);
 [DllImport("user32.dll")]static extern IntPtr SetWinEventHook(uint a,uint b,IntPtr module,EventProc callback,uint pid,uint tid,uint flags);
 [DllImport("user32.dll")]static extern bool UnhookWinEvent(IntPtr hook);
 [DllImport("user32.dll")]static extern bool PeekMessage(out Msg msg,IntPtr h,uint a,uint b,uint remove);
 [DllImport("user32.dll")]static extern bool TranslateMessage(ref Msg msg);
 [DllImport("user32.dll")]static extern IntPtr DispatchMessage(ref Msg msg);
 static System.Threading.Thread observer;static volatile bool stop;static string observed="";
 public static void ObserveSwitch(IntPtr target){stop=false;var ready=new System.Threading.ManualResetEventSlim();observer=new System.Threading.Thread(()=>{
   int foreground=0,startTarget=0,startOther=0,endTarget=0,endOther=0,chord=0;var timeline=new System.Collections.Generic.List<string>();
   EventProc callback=(hook,kind,h,obj,child,thread,time)=>{if(kind==3){if(h==target)foreground++;if(timeline.Count<64){uint owner;GetWindowThreadProcessId(h,out owner);var cls=new System.Text.StringBuilder(128);GetClassName(h,cls,128);timeline.Add($"eventtime={time} hwnd=0x{h.ToInt64():X} pid={owner} class={cls} visible={IsWindowVisible(h)} ex=0x{GetWindowLongPtrW(h,-20).ToInt64():X}");}}if(kind==20){if(h==target)startTarget++;else startOther++;}if(kind==21){if(h==target)endTarget++;else endOther++;}};
   var fg=SetWinEventHook(3,3,IntPtr.Zero,callback,0,0,0);var sw=SetWinEventHook(20,21,IntPtr.Zero,callback,0,0,0);ready.Set();
   while(!stop){Msg msg;while(PeekMessage(out msg,IntPtr.Zero,0,0,1)){TranslateMessage(ref msg);DispatchMessage(ref msg);}if((GetAsyncKeyState(18)&0x8000)!=0&&(GetAsyncKeyState(9)&0x8000)!=0)chord++;System.Threading.Thread.Sleep(2);}
   UnhookWinEvent(fg);UnhookWinEvent(sw);GC.KeepAlive(callback);observed=$"foreground_target={foreground} switch_start_target={startTarget} switch_start_other={startOther} switch_end_target={endTarget} switch_end_other={endOther} physical_alt_tab_samples={chord} timeline=[{string.Join("; ",timeline)}]";
 });observer.IsBackground=true;observer.Start();if(!ready.Wait(1000))throw new Exception("observer setup timeout");}
 public static string EndObservation(){stop=true;if(!observer.Join(1000))throw new Exception("observer shutdown timeout");return observed;}
}
'@
$checks=@();$records=@();$owned=@();$uninjected=[Collections.Generic.HashSet[int]]::new();$inputEvents=[Collections.Generic.List[object]]::new();$keyLedger=@{};$mouseOwned=$false;$inputUnsafe=$false;$inputStarted=$false;$originalFg=[IntPtr]::Zero;$originalCursor=[UserIntentNative+Pt]::new();$lastVerifiedCursor=$null
function InputKey([int]$vk,[bool]$up) {
 if($up -and -not $script:keyLedger.ContainsKey($vk)){return}
 if(-not $up){[uint32]$owner=0;[UserIntentNative]::GetWindowThreadProcessId([UserIntentNative]::GetForegroundWindow(),[ref]$owner)|Out-Null;if($owner -notin @($script:owned|ForEach-Object Id)){throw 'refusing keyboard down: actual foreground is outside owned recipient processes'}}
 if(-not $up -and -not $script:keyLedger.ContainsKey($vk) -and ([UserIntentNative]::GetAsyncKeyState($vk)-band0x8000)){throw 'refusing input: key is already held and is not ours'}
 $accepted=[UserIntentNative]::Keyboard($vk,$up);$nativeError=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
 $fg=[UserIntentNative]::GetForegroundWindow();[uint32]$fp=0;[UserIntentNative]::GetWindowThreadProcessId($fg,[ref]$fp)|Out-Null
 $script:inputEvents.Add([pscustomobject]@{time=[Environment]::TickCount64;kind='key';vk=$vk;up=$up;accepted=$accepted;error=$nativeError;foreground_pid=$fp;foreground_facts=[UserIntentNative]::ForegroundFacts()})
 if($accepted -eq 1){if($up){$script:keyLedger.Remove($vk)}else{$script:keyLedger[$vk]=$true}}
 else{$script:inputUnsafe=$true;throw 'SendInput keyboard insertion refused; stopping experiments until owned foreground/cleanup is verified'}
}
function InputButton([bool]$up) {
 if($up -and -not $script:mouseOwned){return}
 if(-not $up -and -not $script:mouseOwned -and ([UserIntentNative]::GetAsyncKeyState(1)-band0x8000)){throw 'refusing input: mouse button is already held and is not ours'}
 $accepted=[UserIntentNative]::Button($up);$nativeError=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
 $script:inputEvents.Add([pscustomobject]@{time=[Environment]::TickCount64;kind='left_mouse';up=$up;accepted=$accepted;error=$nativeError})
 if($accepted -eq 1){$script:mouseOwned=-not $up}else{$script:inputUnsafe=$true;throw 'SendInput mouse insertion refused; stopping experiments'}
}
function Check($ok,$what){$script:checks += [pscustomobject]@{pass=[bool]$ok;test=$what};"$(if($ok){'PASS'}else{'FAIL'}) $what"}
function Vi($id,[string[]]$a){&$V --json --pid $id @a|ConvertFrom-Json}
function Hwnd($id){$s=Vi $id @('status');[IntPtr][long]::Parse($s.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)}
function Click($h){
 [uint32]$windowPid=0;[UserIntentNative]::GetWindowThreadProcessId($h,[ref]$windowPid)|Out-Null
 if($windowPid -notin @($script:owned|ForEach-Object Id)){throw 'refusing real input/window mutation: HWND is not owned by this run'}
 [UserIntentNative]::SetWindowPos($h,[IntPtr](-1),0,0,0,0,0x53)|Out-Null
 $r=[UserIntentNative+Rc]::new();[UserIntentNative]::GetWindowRect($h,[ref]$r)|Out-Null
 $p=[UserIntentNative+Pt]::new();$p.x=$r.l+100;$p.y=$r.t+100
 $beforeInput=[UserIntentNative]::LastInputTick();$positioned=[UserIntentNative]::SetCursorPos($p.x,$p.y);$positionError=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
 $immediate=[UserIntentNative+Pt]::new();[UserIntentNative]::GetCursorPos([ref]$immediate)|Out-Null
 Start-Sleep -Milliseconds 40
 $actual=[UserIntentNative+Pt]::new();$readActual=[UserIntentNative]::GetCursorPos([ref]$actual)
 $hit=[UserIntentNative]::WindowFromPoint($actual)
 if(-not $positioned -or -not $readActual -or $immediate.x -ne $p.x -or $immediate.y -ne $p.y -or $actual.x -ne $p.x -or $actual.y -ne $p.y -or [UserIntentNative]::GetAncestor($hit,2) -ne [UserIntentNative]::GetAncestor($h,2)){
  $clip=[UserIntentNative+Rc]::new();[UserIntentNative]::GetClipCursor([ref]$clip)|Out-Null
  $fg=[UserIntentNative]::GetForegroundWindow();$class=[Text.StringBuilder]::new(128);[UserIntentNative]::GetClassName($fg,$class,128)|Out-Null
  $diagnostic=[pscustomobject]@{position_return=$positioned;position_error=$positionError;wanted=$p;immediate=$immediate;after40ms=$actual;rect=$r;clip=$clip;foreground=('0x{0:X}' -f $fg.ToInt64());foreground_class=$class.ToString();input_before=$beforeInput;input_after=[UserIntentNative]::LastInputTick()}
  $diagnostic|ConvertTo-Json -Depth 5|Set-Content "$Out\cursor_context_unavailable.json"
  throw "refusing real input: owned window not at coordinates; SetCursorPos=$positioned error=$positionError wanted=$($p.x),$($p.y) immediate=$($immediate.x),$($immediate.y) actual=$($actual.x),$($actual.y)"
 }
 # Recheck the ACTUAL point immediately before the global button event, never only the requested point.
 $atDown=[UserIntentNative+Pt]::new();$atDownOk=[UserIntentNative]::GetCursorPos([ref]$atDown)
 if(-not $atDownOk -or $atDown.x -ne $p.x -or $atDown.y -ne $p.y -or [UserIntentNative]::GetAncestor([UserIntentNative]::WindowFromPoint($atDown),2) -ne [UserIntentNative]::GetAncestor($h,2)){throw 'cursor/context changed immediately before down; no injection attempted'}
 $script:lastVerifiedCursor=$atDown
 $physical=[UserIntentNative+Pt]::new();[UserIntentNative]::GetPhysicalCursorPos([ref]$physical)|Out-Null
 [pscustomobject]@{time=[Environment]::TickCount64;target_pid=$windowPid;set_return=$positioned;set_error=$positionError;requested=$p;immediate=$immediate;before_down=$atDown;physical=$physical;thread_dpi_awareness=[UserIntentNative]::GetAwarenessFromDpiAwarenessContext([UserIntentNative]::GetThreadDpiAwarenessContext());window_dpi=[UserIntentNative]::GetDpiForWindow($h);last_input_before=$beforeInput;last_input_before_down=[UserIntentNative]::LastInputTick()}|ConvertTo-Json -Depth 6 -Compress|Add-Content "$Out\cursor_setup.jsonl"
 InputButton $false;Start-Sleep -Milliseconds 100
 InputButton $true;Start-Sleep -Milliseconds 200
}
function ClickAtExistingCursor($h,$point){
 [uint32]$windowPid=0;[UserIntentNative]::GetWindowThreadProcessId($h,[ref]$windowPid)|Out-Null
 if($windowPid -notin @($script:owned|ForEach-Object Id)){throw 'refusing bootstrap positioning: HWND is not owned by this run'}
 $before=[UserIntentNative+Pt]::new()
 if(-not [UserIntentNative]::GetCursorPos([ref]$before) -or $before.x -ne $point.x -or $before.y -ne $point.y){throw 'original cursor drifted before own-window setup; no click attempted'}
 # Move only this owned fixture around the existing cursor. Never move the cursor.
 $positioned=[UserIntentNative]::SetWindowPos($h,[IntPtr](-1),$point.x-100,$point.y-100,0,0,0x51)
 $immediate=[UserIntentNative+Pt]::new();$immediateOk=[UserIntentNative]::GetCursorPos([ref]$immediate)
 Start-Sleep -Milliseconds 40
 $actual=[UserIntentNative+Pt]::new();$actualOk=[UserIntentNative]::GetCursorPos([ref]$actual)
 $root=[UserIntentNative]::GetAncestor($h,2)
 if(-not $positioned -or -not $immediateOk -or -not $actualOk -or $immediate.x -ne $point.x -or $immediate.y -ne $point.y -or $actual.x -ne $point.x -or $actual.y -ne $point.y -or [UserIntentNative]::GetAncestor([UserIntentNative]::WindowFromPoint($actual),2) -ne $root){throw 'bootstrap at original cursor drifted or was occluded; no click attempted'}
 $atDown=[UserIntentNative+Pt]::new();$atDownOk=[UserIntentNative]::GetCursorPos([ref]$atDown)
 [uint32]$freshPid=0;[UserIntentNative]::GetWindowThreadProcessId($h,[ref]$freshPid)|Out-Null
 if($freshPid -ne $windowPid -or -not $atDownOk -or $atDown.x -ne $point.x -or $atDown.y -ne $point.y -or [UserIntentNative]::GetAncestor([UserIntentNative]::WindowFromPoint($atDown),2) -ne $root){throw 'cursor/context changed before bootstrap down; no click attempted'}
 [pscustomobject]@{mode='BootstrapAtCursor';original=$point;before_window_move=$before;immediate=$immediate;after40ms=$actual;before_down=$atDown;owned_hwnd=('0x{0:X}' -f $h.ToInt64());owned_pid=$windowPid;cursor_moved=$false}|ConvertTo-Json -Depth 5|Set-Content "$Out\bootstrap_cursor.json"
 InputButton $false;Start-Sleep -Milliseconds 100
 InputButton $true;Start-Sleep -Milliseconds 200
}
function AltTab(){InputKey 0x12 $false;Start-Sleep -Milliseconds 30;InputKey 9 $false;Start-Sleep -Milliseconds 130;InputKey 9 $true;Start-Sleep -Milliseconds 30;InputKey 0x12 $true;Start-Sleep -Milliseconds 250}
function StartProbe($exe,$log,$x){Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue;$sourceFlag=if($InputSourceProbeOnly){'--input-source-probe'}else{''};$p=Start-Process -FilePath "$D\$exe" -ArgumentList "`"$log`" --interactive --x $x --y 100 $sourceFlag" -WindowStyle Hidden -PassThru;$script:owned+=$p;[void]$script:uninjected.Add($p.Id);return $p}
function StopOwned($processes,[bool]$lastArchitecture=$false){
 [uint32]$foregroundPid=0;[UserIntentNative]::GetWindowThreadProcessId([UserIntentNative]::GetForegroundWindow(),[ref]$foregroundPid)|Out-Null
 foreach($p in $processes){if(-not $p.HasExited){if($KeepForegroundSentinel -and $lastArchitecture -and -not $script:mouseOwned -and $script:keyLedger.Count -eq 0 -and $p.Id -eq $foregroundPid -and $script:uninjected.Contains($p.Id)){continue};Stop-Process -Id $p.Id -Force -Confirm:$false}}
}
function HandleFromLog($log,$expectedPid){for($i=0;$i -lt 100;$i++){$s=Get-Content $log -Raw -ErrorAction SilentlyContinue;if($s -match 'WINDOW hwnd=0x([0-9A-F]+)'){$h=[IntPtr][long]::Parse($Matches[1],[Globalization.NumberStyles]::HexNumber);[uint32]$windowPid=0;[UserIntentNative]::GetWindowThreadProcessId($h,[ref]$windowPid)|Out-Null;if($windowPid -eq $expectedPid){return $h}};Start-Sleep -Milliseconds 30};throw 'no fresh owned probe HWND'}
function Steal($exe,$h,$label){$log="$Out\$label.witness.log";$p=Start-Process -FilePath "$D\$exe" -ArgumentList ('--steal 0x{0:X} "{1}"' -f $h.ToInt64(),$log) -WindowStyle Hidden -PassThru;$script:owned+=$p;if(-not $p.WaitForExit(3000)){throw 'owned thief timeout'};Start-Sleep -Milliseconds 250;return [pscustomobject]@{exit=$p.ExitCode;log=(Get-Content $log -Raw)}}
function LaunchNoInject($exe,$log){
 Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
 $native=if($exe -eq 'vi_focusprobe32.exe'){(Resolve-Path "$CliDir\vinput32.exe").Path}else{$V}
 $si=[Diagnostics.ProcessStartInfo]::new($native);$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
 # The enclosing script owns the shared foreground lock (VINPUT_LOCK); the CLI owns this private test lock.
 foreach($arg in @('--json','launch','--exe',"$D\$exe",'--args',"`"$log`" --interactive --x 100 --y 100",'--timeout','8','--settle','0','--fg-guard','3','--no-inject','--lock',"$Out\$exe.private-launch.lock")){$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;[void]$p.Start();$script:owned+=$p
 return [pscustomobject]@{p=$p;stdout=$p.StandardOutput.ReadToEndAsync();stderr=$p.StandardError.ReadToEndAsync()}
}
function LaunchedHandle($log,$launcherPid,$exe){
 for($n=0;$n -lt 100;$n++){
  $s=Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
  if($s -match 'WINDOW hwnd=0x([0-9A-F]+)'){
   $h=[IntPtr][long]::Parse($Matches[1],[Globalization.NumberStyles]::HexNumber);[uint32]$owner=0;[UserIntentNative]::GetWindowThreadProcessId($h,[ref]$owner)|Out-Null
   $process=Get-CimInstance Win32_Process -Filter "ProcessId=$owner"
   if($process.ParentProcessId -eq $launcherPid -and $process.ExecutablePath -eq "$D\$exe"){$script:owned+=Get-Process -Id $owner;[void]$script:uninjected.Add([int]$owner);return $h}
  }
  Start-Sleep -Milliseconds 20
 }
 throw 'no fresh HWND owned by the exact synthetic process started by our launcher'
}
if($ValidateHarness){'PASS user intent harness compiles; no lock/input/foreground mutation';return}
$lock=$(if($env:VINPUT_LOCK){$env:VINPUT_LOCK}else{New-Item -ItemType Directory -Force "$PSScriptRoot\..\build"|Out-Null;"$PSScriptRoot\..\build\foreground.lock"});$token=[guid]::NewGuid().ToString('N');$stream=$null;$start=[Environment]::TickCount64
while(-not $stream){try{$stream=[IO.File]::Open($lock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)}catch [IO.IOException]{if([Environment]::TickCount64-$start -gt $LockWaitSec*1000){throw 'canonical LIVE lock busy; never removed'};Start-Sleep -Seconds 1}}
$bytes=[Text.Encoding]::UTF8.GetBytes((@{owner='vinput user-intent real input controls';pid=$PID;token=$token;time=(Get-Date -Format o)}|ConvertTo-Json -Compress));$stream.Write($bytes,0,$bytes.Length);$stream.Dispose()
try {
 $quietStart=[Environment]::TickCount64;$stableAt=$quietStart;$lastInput=[UserIntentNative]::LastInputTick()
 while([Environment]::TickCount64-$stableAt -lt 2000){Start-Sleep -Milliseconds 100;$nextInput=[UserIntentNative]::LastInputTick();if($nextInput -ne $lastInput){$lastInput=$nextInput;$stableAt=[Environment]::TickCount64};if([Environment]::TickCount64-$quietStart -gt 30000){throw 'no two-second quiet OS input interval; no cursor mutation or injection attempted'}}
 foreach($key in @(1,2,0x10,0x11,0x12)){if([UserIntentNative]::GetAsyncKeyState($key) -band 0x8000){throw 'real modifier/mouse already held; refusing to interfere'}}
 $originalFg=[UserIntentNative]::GetForegroundWindow();[UserIntentNative]::GetCursorPos([ref]$originalCursor)|Out-Null;$inputStarted=$true
 $targets=@('vi_focusprobe.exe','vi_focusprobe32.exe')
 if($BootstrapOnly){
  # Explicit one-click verification setup only. Never injects or sends keyboard input.
  if([UserIntentNative]::GetAsyncKeyState(9) -band 0x8000){throw 'real Tab already held; refusing bootstrap'}
  $bootstrap=StartProbe 'vi_focusprobe.exe' "$Out\bootstrap.log" 650;$h=HandleFromLog "$Out\bootstrap.log" $bootstrap.Id
  [UserIntentNative]::ObserveSwitch($h)
  try{if($BootstrapAtCursor){ClickAtExistingCursor $h $originalCursor}else{Click $h}}finally{$witness=[UserIntentNative]::EndObservation()}
  $ready=$witness -match 'foreground_target=([1-9][0-9]*)' -and [UserIntentNative]::GetForegroundWindow() -eq $h
  Check $ready 'BootstrapOnly exact owned uninjected normal window received one real click and holds foreground'
  if(-not $ready){throw 'owned bootstrap foreground unavailable; no retry'}
  $records += [pscustomobject]@{mode=if($BootstrapAtCursor){'BootstrapAtCursor'}else{'BootstrapOnly'};pid=$bootstrap.Id;hwnd=('0x{0:X}' -f $h.ToInt64());independent_event=$witness;uninjected=$true}
  $KeepForegroundSentinel=$true;$targets=@()
 }
 foreach($exe in $targets){
  if($LaunchControl){
   $sentinel=StartProbe $exe "$Out\$exe.launch-sentinel.log" 650;$sh=HandleFromLog "$Out\$exe.launch-sentinel.log" $sentinel.Id
   Click $sh;Check ([UserIntentNative]::GetForegroundWindow() -eq $sh) "$exe launch control owned sentinel is foreground"
   if([UserIntentNative]::GetForegroundWindow() -ne $sh){throw 'launch control sentinel unavailable'}
   $log="$Out\$exe.launch-target.log";$launcher=LaunchNoInject $exe $log;$h=LaunchedHandle $log $launcher.p.Id $exe
   [UserIntentNative]::ObserveSwitch($h);Click $h;$switchEvidence=[UserIntentNative]::EndObservation()
   $actualGain=$switchEvidence -match 'foreground_target=([1-9][0-9]*)'
   Check $actualGain "$exe launch click produced an independent exact-target foreground event"
   if(-not $launcher.p.WaitForExit(6000)){throw 'owned launch control did not complete'}
   $stdout=$launcher.stdout.GetAwaiter().GetResult();$stderr=$launcher.stderr.GetAwaiter().GetResult();$stderr|Set-Content "$Out\$exe.launch.stderr"
   $reply=$stdout|ConvertFrom-Json;$held=[UserIntentNative]::GetForegroundWindow() -eq $h
   Check ($reply.ok -and $reply.user_took_foreground -and -not $reply.foreground_restored -and $held) "$exe launch watcher and final cleanup preserve validated human selection"
   $records += [pscustomobject]@{target=$exe;launch_reply=$reply;real_foreground_held=$held;independent_event=$switchEvidence;exit=$launcher.p.ExitCode}
   StopOwned @($owned) ($exe -eq 'vi_focusprobe32.exe')
   continue
  }
  $target=StartProbe $exe "$Out\$exe.target.log" 100;$h=HandleFromLog "$Out\$exe.target.log" $target.Id
  $sentinel=StartProbe $exe "$Out\$exe.sentinel.log" 650;$sh=HandleFromLog "$Out\$exe.sentinel.log" $sentinel.Id
  if($InputSourceProbeOnly){
   Click $h;Click $sh;[UserIntentNative]::ObserveSwitch($h);AltTab;$switchEvidence=[UserIntentNative]::EndObservation()
   $selected=[UserIntentNative]::GetForegroundWindow() -eq $h
   $afterAltTab=Get-Content "$Out\$exe.target.log" -Raw
   InputKey 0x12 $false;Start-Sleep -Milliseconds 50;InputKey 0x12 $true;Start-Sleep -Milliseconds 100
   $afterRealAltTap=Get-Content "$Out\$exe.target.log" -Raw
   [UserIntentNative]::PostMessage($h,0x105,[IntPtr]0x12,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 100
   $records += [pscustomobject]@{target=$exe;physical_alt_tab_selected_target=$selected;switch_witness=$switchEvidence;after_alt_tab=$afterAltTab;after_real_alt_tap=$afterRealAltTap;after_posted_alt_up=(Get-Content "$Out\$exe.target.log" -Raw)}
   Check $selected "$exe independent uninjected real Alt+Tab selected the owned target"
   Stop-Process -Id $target.Id,$sentinel.Id -Force -Confirm:$false;continue
  }
  $inject=&$V --json inject --pid $target.Id|ConvertFrom-Json;if(-not $inject.ok){throw 'inject failed'};[void]$uninjected.Remove($target.Id);Start-Sleep -Milliseconds 300
  if($AltTabOnly){
   # API-only setup between owned normal windows; no cursor mutation or mouse event.
   Vi $target.Id @('config','blockfocus=false')|Out-Null
   $prime=Steal $exe $h "$exe.alt_only_prime"
   if($prime.exit -ne 0 -or [UserIntentNative]::GetForegroundWindow() -ne $h){throw 'AltTab-only owned target MRU setup unavailable; no keyboard insertion attempted'}
   $sentinelPrime=Steal $exe $sh "$exe.alt_only_sentinel"
   if($sentinelPrime.exit -ne 0 -or [UserIntentNative]::GetForegroundWindow() -ne $sh){throw 'AltTab-only owned sentinel foreground unavailable; no keyboard insertion attempted'}
   Vi $target.Id @('config','blockfocus=true')|Out-Null
   [UserIntentNative]::ObserveSwitch($h);AltTab;$alt=Vi $target.Id @('status');$switchEvidence=[UserIntentNative]::EndObservation()
   Check ($switchEvidence -match 'foreground_target=([1-9][0-9]*)') "$exe AltTab-only exact-target independent gain witnessed"
   Check ([UserIntentNative]::GetForegroundWindow() -eq $h -and $alt.foreground_guard.user_took_foreground -and $alt.foreground_guard.user_takeover_reason -eq 'physical_alt_tab') "$exe AltTab-only physical selection preserves foreground"
   Start-Sleep -Milliseconds 250;Check ([UserIntentNative]::GetForegroundWindow() -eq $h) "$exe AltTab-only foreground persists beyond restore interval"
   $records += [pscustomobject]@{target=$exe;alt_tab=$alt;switch_witness=$switchEvidence;prime=$prime;sentinel_prime=$sentinelPrime}
   Vi $target.Id @('config','blockfocus=false')|Out-Null
   $returnSentinel=Steal $exe $sh "$exe.alt_only_return"
   StopOwned @($target,$sentinel) ($exe -eq 'vi_focusprobe32.exe')
   continue
  }
  Click $sh;$sentinelForeground=[UserIntentNative]::GetForegroundWindow() -eq $sh;Check $sentinelForeground "$exe owned sentinel received the real click"
  if(-not $sentinelForeground){throw 'owned sentinel activation control unavailable; stopping before more real input'}
  [UserIntentNative]::ObserveSwitch($h);Click $h;$mouseWitness=[UserIntentNative]::EndObservation();$s=Vi $target.Id @('status');$mouseHeld=[UserIntentNative]::GetForegroundWindow() -eq $h
  $mouseGain=$mouseWitness -match 'foreground_target=([1-9][0-9]*)'
  Check $mouseGain "$exe physical click produced an independent exact-target foreground event"
  if(-not $mouseGain){throw 'physical target activation positive unavailable; stopping before more input'}
  Check ($mouseHeld -and $s.foreground_guard.user_took_foreground -and -not $s.foreground_guard.armed) "$exe deliberate real click preserves foreground and suspends guard"
  if($BaselineControl){$records += [pscustomobject]@{target=$exe;mouse=$s;held=$mouseHeld;independent_event=$mouseWitness};Stop-Process -Id $target.Id,$sentinel.Id -Force -Confirm:$false;continue}
  Start-Sleep -Milliseconds 350;Check ([UserIntentNative]::GetForegroundWindow() -eq $h) "$exe human selection persists beyond restore interval"
  Vi $target.Id @('config','cursor=virtual')|Out-Null;$cursor=Vi $target.Id @('status')
  Check ($cursor.cursor_mode_name -eq 'virtual' -and $cursor.effective_cursor -eq 'virtual') "$exe explicit virtual cursor configuration is preserved"
  Click $sh;Start-Sleep -Milliseconds 100;$lost=Vi $target.Id @('status')
  Check (-not $lost.foreground_guard.user_took_foreground -and $lost.foreground_guard.armed) "$exe loss clears human state and rearms"
  # Recent real input belongs to SENTINEL; it must not authorize a later game self-activation.
  if([UserIntentNative]::GetForegroundWindow() -ne $sh){throw 'sentinel is not foreground before ordinary input'}
  InputKey 0x41 $false;Start-Sleep -Milliseconds 40;InputKey 0x41 $true
  $witness=Steal $exe $h "$exe.recent_input";$after=Vi $target.Id @('status')
  Check ($witness.exit -eq 0 -and -not $after.really_foreground -and -not $after.foreground_guard.user_took_foreground -and $after.foreground_guard.restored_count -gt $lost.foreground_guard.restored_count) "$exe recent ordinary input elsewhere cannot authorize external game gain"
  # Physical Alt+Tab selects the MRU game window after the sentinel was clicked.
  Click $h;Click $sh;[UserIntentNative]::ObserveSwitch($h);AltTab;$alt=Vi $target.Id @('status');$switchEvidence=[UserIntentNative]::EndObservation()
  Check ([UserIntentNative]::GetForegroundWindow() -eq $h -and $alt.foreground_guard.user_took_foreground -and $alt.foreground_guard.user_takeover_reason -eq 'physical_alt_tab') "$exe physical Alt+Tab plus exact OS switch target preserves foreground"
  Click $sh;Start-Sleep -Milliseconds 100;$rearmed=Vi $target.Id @('status')
  Check (-not $rearmed.foreground_guard.user_took_foreground -and $rearmed.foreground_guard.armed) "$exe Alt+Tab takeover rearms on loss"
  # Select another owned application by Alt+Tab, then have the game activate externally.
  # This recent physical chord must not give the nonselected game permanent authorization.
  $other=StartProbe $exe "$Out\$exe.other.log" 1200;$oh=HandleFromLog "$Out\$exe.other.log" $other.Id
  Click $oh;Click $sh;AltTab
  $otherSelected=[UserIntentNative]::GetForegroundWindow() -eq $oh
  Check $otherSelected "$exe negative control Alt+Tab selected the other owned application"
  if(-not $otherSelected){throw 'Alt+Tab-other positive unavailable; refusing further input'}
  $otherWitness=Steal $exe $h "$exe.other_alt_tab";$afterOther=Vi $target.Id @('status')
  Check ($otherWitness.exit -eq 0 -and -not $afterOther.really_foreground -and -not $afterOther.foreground_guard.user_took_foreground -and $afterOther.foreground_guard.restored_count -gt $rearmed.foreground_guard.restored_count) "$exe Alt+Tab elsewhere cannot authorize external game gain"
  $records += [pscustomobject]@{target=$exe;mouse=$s;mouse_witness=$mouseWitness;after_loss=$lost;ordinary_input_external_gain=$after;witness=$witness;alt_tab=$alt;cursor=$cursor;switch_witness=$switchEvidence;other_alt_tab_external_gain=$afterOther;other_alt_tab_witness=$otherWitness}
  StopOwned @($target,$sentinel,$other) ($exe -eq 'vi_focusprobe32.exe')
 }
} finally {
 if($inputStarted){
  try{if($mouseOwned){InputButton $true};foreach($vk in @($keyLedger.Keys)){InputKey $vk $true}}catch{Write-Warning $_}
  if(-not $BootstrapAtCursor -and -not $mouseOwned -and $keyLedger.Count -eq 0 -and $lastVerifiedCursor){
   $currentCursor=[UserIntentNative+Pt]::new();$readCursor=[UserIntentNative]::GetCursorPos([ref]$currentCursor)
   $restoreCursor=$readCursor -and $currentCursor.x -eq $lastVerifiedCursor.x -and $currentCursor.y -eq $lastVerifiedCursor.y
   if($restoreCursor){[UserIntentNative]::SetCursorPos($originalCursor.x,$originalCursor.y)|Out-Null}
   [pscustomobject]@{observed=$currentCursor;last_verified_own_point=$lastVerifiedCursor;restored_original=$restoreCursor}|ConvertTo-Json -Depth 4|Set-Content "$Out\cursor_cleanup.json"
  }
 }
 $unreleased=$mouseOwned -or $keyLedger.Count -gt 0
 $current=[UserIntentNative]::GetForegroundWindow();[uint32]$currentPid=0;[UserIntentNative]::GetWindowThreadProcessId($current,[ref]$currentPid)|Out-Null
 $keepPid=0
 if($KeepForegroundSentinel -and -not $unreleased -and $currentPid -in @($owned|ForEach-Object Id) -and $uninjected.Contains([int]$currentPid) -and [UserIntentNative]::IsWindowVisible($current)){
  $retained=Get-Process -Id $currentPid
  if($retained.Path -in @("$D\vi_focusprobe.exe","$D\vi_focusprobe32.exe")){$keepPid=$currentPid;[pscustomobject]@{pid=$keepPid;hwnd=('0x{0:X}' -f $current.ToInt64());path=$retained.Path;creation_time=$retained.StartTime.ToUniversalTime().ToString('o');original_user_hwnd=('0x{0:X}' -f $originalFg.ToInt64());uninjected=$true;owned_input_ledger_empty=$true;foreground_facts=[UserIntentNative]::ForegroundFacts()}|ConvertTo-Json -Depth 5|Set-Content "$Out\retained_sentinel.json"}
 }
 if(-not $unreleased -and -not $keepPid -and $currentPid -in @($owned|ForEach-Object Id) -and [UserIntentNative]::IsWindow($originalFg)){$restore=Steal 'vi_focusprobe.exe' $originalFg 'restore_original'}
 if(-not $unreleased){foreach($p in $owned){if(-not $p.HasExited -and $p.Id -ne $keepPid){Stop-Process -Id $p.Id -Force -Confirm:$false}}}
 $body=Get-Content $lock -Raw -ErrorAction SilentlyContinue;if(-not $unreleased -and $body -and $body.Contains($token)){Remove-Item -LiteralPath $lock}
 ConvertTo-Json -InputObject $inputEvents.ToArray() -Depth 5|Set-Content "$Out\input_events.json"
 [pscustomobject]@{final_physical_state=@{alt=([UserIntentNative]::GetAsyncKeyState(0x12) -band 0x8000) -ne 0;tab=([UserIntentNative]::GetAsyncKeyState(9) -band 0x8000) -ne 0;left_mouse=([UserIntentNative]::GetAsyncKeyState(1) -band 0x8000) -ne 0};owned_key_ledger=@($keyLedger.Keys);owned_mouse_down=$mouseOwned;insertion_refused=$inputUnsafe;unreleased=$unreleased;retained_owned_pids=if($unreleased){@($owned|Where-Object{-not $_.HasExited}|ForEach-Object Id)}else{@()};lock_token=if($unreleased){$token}else{''}}|ConvertTo-Json -Depth 5|Set-Content "$Out\input_ledger.json"
 if($unreleased){Write-Warning 'OWN INJECTED INPUT IS NOT RELEASED: canonical lock and owned recipient windows retained; no further experiments'}
 if($keepPid){Write-Output "RETAINED uninjected owned foreground sentinel PID=$keepPid; exact cleanup identity in $Out\retained_sentinel.json"}
 $checks|ConvertTo-Json -Depth 5|Set-Content "$Out\checks.json";$records|ConvertTo-Json -Depth 14|Set-Content "$Out\evidence.json"
}
$failed=@($checks|Where-Object{-not $_.pass}).Count;if($failed){throw "FAIL user intent $failed / $($checks.Count)"};"PASS user intent $($checks.Count) checks";$global:LASTEXITCODE=0
