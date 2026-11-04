param([string]$Out="$PSScriptRoot\..\build\test\ddraw_windowed\after",[string]$DllDir='', [switch]$KeepGoing,[ValidateSet('x64','x86')][string[]]$Architectures=@('x64','x86'),[ValidateSet(7,4,2,1)][int[]]$Apis=@(7,4,2,1),[ValidateSet('ex','create','com7','com1')][string[]]$Factories=@('ex','create','com7','com1'),[switch]$NativeContext)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$cli="$root\dist\vinput.exe";if(!$DllDir){$DllDir="$root\dist"};$DllDir=(Resolve-Path $DllDir).Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
. "$PSScriptRoot\vi_ownedprocess.ps1";. "$PSScriptRoot\vi_patterncheck.ps1"
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$runId=[guid]::NewGuid().ToString('N');$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new()
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViDdTest{
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int i);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool EnumDisplaySettingsW(string name,int mode,byte[] data);
 public static string Desktop(){byte[] b=new byte[220];b[68]=220;if(!EnumDisplaySettingsW(null,-1,b))throw new Exception("display snapshot unavailable");return BitConverter.ToString(b);}
}
'@
function Check($pass,$name){$checks.Add(@{pass=[bool]$pass;name=$name});"$(if($pass){'PASS'}else{'FAIL'}) $name";if(!$pass-and!$KeepGoing){throw $name}}
function Log($p){if(!(Test-Path $p)){return ''};$f=[IO.FileStream]::new($p,'Open','Read',([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));$r=[IO.StreamReader]::new($f);try{return $r.ReadToEnd()}finally{$r.Dispose();$f.Dispose()}}
function WaitLog($p,$pattern){$until=[Environment]::TickCount64+6000;do{if((Log $p)-match$pattern){return $true};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until);return $false}
function Vi($target,[string[]]$arguments){$raw=&$cli --json --pid $target @arguments;return $raw|ConvertFrom-Json}
try{
 foreach($arch in $Architectures){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$d="$Out\$arch\run_$runId";New-Item -ItemType Directory -Force "$d\temp"|Out-Null
 $aliases=if($arch-eq'x64'){"DirectDrawCreate=GuardCreate`nDirectDrawCreateEx=GuardCreateEx`nViGuardLog=GuardLog`nViGuardStats=GuardStats`nViGuardPrime=GuardPrime"}else{"DirectDrawCreate=_GuardCreate@12`nDirectDrawCreateEx=_GuardCreateEx@16`nViGuardLog=_GuardLog@4`nViGuardStats=_GuardStats@4`nViGuardPrime=_GuardPrime@0"}
 "EXPORTS`n$aliases"|Set-Content "$d\guard.def" -Encoding ascii
 $batch=@"
@echo off
set "TEMP=$d\temp"
set "TMP=$d\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$root\src" /I"$root\third_party\minhook\include" /LD "$root\test\vi_ddrawguard.cpp" /Fo:"$d\guard.obj" /Fe:"$d\ddraw.dll" /link /DEF:"$d\guard.def" "$root\build\$arch\vimh_buffer.obj" "$root\build\$arch\vimh_hook.obj" "$root\build\$arch\vimh_trampoline.obj" "$root\build\$arch\vimh_hde.obj" || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_ddraw_windowed.cpp" /Fo:"$d\fixture.obj" /Fe:"$d\fixture.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$d\build.cmd",$batch,[Text.Encoding]::ASCII);cmd /d /c "$d\build.cmd" *> "$d\build.log";Check ($LASTEXITCODE-eq0) "$arch fixture and native boundary guard W4 WX"
 foreach($factory in $Factories){foreach($api in $Apis){$p=$null;$log="$d\${factory}_api$api.log";$desktop=[ViDdTest]::Desktop();try{
  $p=Start-Process "$d\fixture.exe" -ArgumentList "`"$log`" $api $factory" -WindowStyle Hidden -PassThru;Check (WaitLog $log 'READY_GUARDED') "$arch DD$api guarded fixture ready ($factory)"
  $dll=Join-Path $DllDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'});$inj=&$cli --json inject --pid $p.Id --dll $dll|ConvertFrom-Json;Check $inj.ok "$arch DD$api inject"
  if($NativeContext){Vi $p.Id @('config','fakefocus=false')|Out-Null};$s=Vi $p.Id @('status');$evidence.Add(@{arch=$arch;api=$api;pid=$p.Id;startup_status=$s});$h=[IntPtr]::new([Convert]::ToInt64($s.hwnd,16));[ViDdTest]::PostMessage($h,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $ready=WaitLog $log 'RENDER_READY';Check $ready "$arch DD$api exclusive request becomes real 800x600 windowed surface chain"
  if($ready){Check ((Log $log)-match'SWAP_OK') "$arch DD$api actual front and back contents swap"
   Check ((Log $log)-match'ALIASES_OK') "$arch DD$api surface1 2 3 4 7 aliases retain coherent IUnknown and mode"
   foreach($phase in @(0,1,2)){[ViDdTest]::PostMessage($h,0x8002,[IntPtr]$phase,[IntPtr]::Zero)|Out-Null;Check (WaitLog $log "DRAWING phase=$phase ") "$arch DD$api phase$phase rendered"
    $path="$d\${factory}_api${api}_phase$phase.png";$capture=Vi $p.Id @('capture',$path,'--source','swapchain','--after-frames','3');$pattern=if($capture.ok){Test-VinputPattern $path}else{@{ok=$false}}
    Check ($capture.ok-and$capture.capture_path-eq'ddraw'-and$pattern.ok-and$capture.w-eq800-and$capture.h-eq600) "$arch DD$api phase$phase native known pixels at game resolution";$evidence.Add(@{arch=$arch;api=$api;phase=$phase;capture=$capture;pattern=$pattern})
   }
   $s=Vi $p.Id @('status');$topmost=([ViDdTest]::GetWindowLongPtrW($h,-20).ToInt64()-band8)-ne0;Check (-not$s.really_foreground-and!$topmost-and$s.client_w-eq800-and$s.client_h-eq600) "$arch DD$api background window exact client size"
   [ViDdTest]::PostMessage($h,0x8003,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Check (WaitLog $log 'SURFACES_RELEASED') "$arch DD$api releases surface chain";Check ((Log $log)-match'BACK_RETAINS_FRONT_OK') "$arch DD$api backbuffer keeps front alive after last front release";Check (WaitLog $log 'NATIVE_BOUNDARY exclusive=0 mode=0 normal=[1-9]') "$arch DD$api no native exclusive or mode call"
   $s=Vi $p.Id @('status');Check ($s.ddraw.objects-eq0) "$arch DD$api COM proxies drain on release";$evidence.Add(@{arch=$arch;api=$api;released_status=$s})
  }
  $desktopAfter=[ViDdTest]::Desktop()
  $evidence.Add(@{kind='desktop_mode_exact_snapshot';check_index=$checks.Count;arch=$arch;factory=$factory;api=$api;pid=$p.Id;native_context=[bool]$NativeContext;log=$log;desktop_snapshot_format='220-byte EnumDisplaySettingsW DEVMODEW buffer, full hexadecimal bytes';desktop_before=$desktop;desktop_after=$desktopAfter;desktop_exact_equal=[bool]($desktopAfter-eq$desktop);really_foreground=$s.really_foreground;ddraw_status=$s.ddraw})
  Check ($desktopAfter-eq$desktop) "$arch DD$api desktop mode unchanged"
 }finally{if($p){$s=Vi $p.Id @('status');if($s.log_path-and(Test-Path $s.log_path)){Copy-Item $s.log_path "$log.vinput.log"};Stop-ViOwnedProcess $p}}}}
 foreach($policy in @('refuse','native')){$p=$null;$log="$d\policy_$policy.log";$desktop=[ViDdTest]::Desktop();try{
  $p=Start-Process "$d\fixture.exe" -ArgumentList "`"$log`" 7 ex" -WindowStyle Hidden -PassThru;Check (WaitLog $log 'READY_GUARDED') "$arch $policy policy target guarded"
  $dll=Join-Path $DllDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'});$inj=&$cli --json inject --pid $p.Id --dll $dll|ConvertFrom-Json;Check $inj.ok "$arch $policy policy inject"
  $option=if($policy-eq'native'){'nofullscreen=false'}else{'fullscreen=refuse'};$configured=Vi $p.Id @('config',$option);Check $configured.ok "$arch $policy policy configured before factory"
  $s=Vi $p.Id @('status');$h=[IntPtr]::new([Convert]::ToInt64($s.hwnd,16));[ViDdTest]::PostMessage($h,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Check (WaitLog $log 'EXIT 10') "$arch $policy guard/refusal stops the fullscreen request"
  $s=Vi $p.Id @('status');if($policy-eq'native'){Check ((Log $log)-match'NATIVE_BOUNDARY exclusive=1 mode=1 normal=1'-and$s.ddraw.objects-eq0) "$arch explicit native optout passes untouched interfaces only to the rejecting test guard"}
  else{Check ((Log $log)-match'NATIVE_BOUNDARY exclusive=0 mode=0 normal=1'-and$s.ddraw.refused-eq2-and$s.ddraw.blocked_before_first_present) "$arch explicit refusal reaches no native fullscreen boundary and reports blocked rendering"
   $capture=Vi $p.Id @('capture',"$d\refused.png",'--source','swapchain');Check (-not$capture.ok-and$capture.reason-eq'fullscreen_blocked_before_first_present') "$arch blocked DirectDraw capture fails before a doomed frame wait"
  }
  $desktopAfter=[ViDdTest]::Desktop()
  $evidence.Add(@{kind='desktop_mode_exact_snapshot';check_index=$checks.Count;arch=$arch;factory='ex';api=7;policy=$policy;pid=$p.Id;native_context=[bool]$NativeContext;log=$log;desktop_snapshot_format='220-byte EnumDisplaySettingsW DEVMODEW buffer, full hexadecimal bytes';desktop_before=$desktop;desktop_after=$desktopAfter;desktop_exact_equal=[bool]($desktopAfter-eq$desktop);really_foreground=$s.really_foreground;ddraw_status=$s.ddraw})
  Check (-not$s.really_foreground-and$desktopAfter-eq$desktop) "$arch $policy policy preserves background desktop";$evidence.Add(@{arch=$arch;policy=$policy;status=$s})
 }finally{if($p){Stop-ViOwnedProcess $p}}}
 }
}finally{$checks|ConvertTo-Json -Depth 6|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 20|Set-Content "$Out\evidence.json"}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
