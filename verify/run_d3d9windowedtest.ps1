param([string]$Out="$PSScriptRoot\..\build\test\d3d9_windowed\after",[string]$DllDir='',[switch]$KeepGoing,[ValidateSet('x64','x86')][string[]]$Architectures=@('x64','x86'),[ValidateSet('9','ex')][string[]]$Apis=@('9','ex'),[switch]$NativeContext)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$cli="$root\dist\vinput.exe";if(!$DllDir){$DllDir="$root\dist"};$DllDir=(Resolve-Path $DllDir).Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
. "$PSScriptRoot\vi_ownedprocess.ps1";New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$runId=[guid]::NewGuid().ToString('N');$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new()
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViD9Test{
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool EnumDisplaySettingsW(string name,int mode,byte[] data);
 public static string Desktop(){byte[] b=new byte[220];b[68]=220;if(!EnumDisplaySettingsW(null,-1,b))throw new Exception("display snapshot unavailable");return BitConverter.ToString(b);}
}
'@
function Check($pass,$name){$checks.Add(@{pass=[bool]$pass;name=$name});"$(if($pass){'PASS'}else{'FAIL'}) $name";if(!$pass-and!$KeepGoing){throw $name}}
function Log($path){if(!(Test-Path $path)){return ''};$f=[IO.FileStream]::new($path,'Open','Read',([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));$r=[IO.StreamReader]::new($f);try{return $r.ReadToEnd()}finally{$r.Dispose();$f.Dispose()}}
function WaitLog($path,$pattern){$until=[Environment]::TickCount64+7000;do{if((Log $path)-match$pattern){return $true};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until);return $false}
function Vi($target,[string[]]$arguments){$raw=&$cli --json --pid $target @arguments;return $raw|ConvertFrom-Json}
try{foreach($arch in $Architectures){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$d="$Out\$arch\run_$runId";New-Item -ItemType Directory -Force "$d\temp"|Out-Null
 $aliases=if($arch-eq'x64'){"Direct3DCreate9=GuardFactory`nDirect3DCreate9Ex=GuardFactoryEx`nViGuardLog=GuardLog`nViGuardStats=GuardStats"}else{"Direct3DCreate9=_GuardFactory@4`nDirect3DCreate9Ex=_GuardFactoryEx@8`nViGuardLog=_GuardLog@4`nViGuardStats=_GuardStats@4"};"EXPORTS`n$aliases"|Set-Content "$d\guard.def" -Encoding ascii
 $batch=@"
@echo off
set "TEMP=$d\temp"
set "TMP=$d\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$root\src" /I"$root\third_party\minhook\include" /LD "$root\test\vi_d3d9guard.cpp" /Fo:"$d\guard.obj" /Fe:"$d\d3d9.dll" /link /DEF:"$d\guard.def" "$root\build\$arch\vimh_buffer.obj" "$root\build\$arch\vimh_hook.obj" "$root\build\$arch\vimh_trampoline.obj" "$root\build\$arch\vimh_hde.obj" || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_d3d9_windowed.cpp" /Fo:"$d\fixture.obj" /Fe:"$d\fixture.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$d\build.cmd",$batch,[Text.Encoding]::ASCII);cmd /d /c "$d\build.cmd" *> "$d\build.log";Check ($LASTEXITCODE-eq0) "$arch D3D9 fixture and native guard W4 WX";if($LASTEXITCODE-ne0){throw 'fixture build failed'}
 foreach($api in $Apis){foreach($policy in @('fullscreen','windowed','refuse')){$p=$null;$log="$d\${api}_$policy.log";$desktop=[ViD9Test]::Desktop();try{
  $p=Start-Process "$d\fixture.exe" -ArgumentList "`"$log`" $api $policy" -WindowStyle Hidden -PassThru;Check (WaitLog $log 'READY_GUARDED') "$arch D3D$api $policy guarded target ready"
  $dll=Join-Path $DllDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'});$inj=&$cli --json inject --pid $p.Id --dll $dll|ConvertFrom-Json;Check $inj.ok "$arch D3D$api $policy inject"
  if($NativeContext){Vi $p.Id @('config','fakefocus=false')|Out-Null};if($policy-eq'refuse'){Vi $p.Id @('config','fullscreen=refuse')|Out-Null};$s=Vi $p.Id @('status');$h=[IntPtr]::new([Convert]::ToInt64($s.hwnd,16));[ViD9Test]::PostMessage($h,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Check (WaitLog $log '(?m)^EXIT \d+') "$arch D3D$api $policy bounded native result"
  $text=Log $log;$s=Vi $p.Id @('status')
  $caseEvidence=@{arch=$arch;api=$api;policy=$policy;native_context=[bool]$NativeContext;target_pid=$p.Id;log_path=$log;dll=$dll;status=$s;target_log=$text;desktop_before=$desktop;desktop_after=$null;desktop_snapshot_error=$null;check_index_before_case_assertions=$checks.Count}
  $evidence.Add($caseEvidence)
  try{$caseEvidence.desktop_after=[ViD9Test]::Desktop()}catch{$caseEvidence.desktop_snapshot_error=$_.Exception.Message}
  if($policy-eq'refuse'){
   Check ($text-match'EXIT 10'-and$text-match'NATIVE_BOUNDARY blocked=0 normal=0 bad_mode=0'-and$s.d3d9.refused-eq1-and$s.d3d9.blocked_before_first_present) "$arch D3D$api explicit refusal stops before native fullscreen"
   $capture=Vi $p.Id @('capture',"$d\refuse_$api.png",'--source','swapchain');Check (-not$capture.ok-and$capture.reason-eq'fullscreen_blocked_before_first_present') "$arch D3D$api refusal capture reports blocked negotiation"
  }else{
   Check ($text-match'(?m)^EXIT 0\r?$') "$arch D3D$api $policy native windowed create/reset/additional succeeds"
   $expected=if($policy-eq'windowed'){1}else{3};Check (@([regex]::Matches($text,'NATIVE_PIXEL_OK')).Count-eq$expected) "$arch D3D$api $policy actual backbuffer pixels independently read"
   Check ($text-match'NATIVE_BOUNDARY blocked=0 normal=[1-9][0-9]* bad_mode=0') "$arch D3D$api $policy no native fullscreen or fullscreen mode"
   if($policy-eq'fullscreen'){Check ($s.d3d9.emulated-eq3-and$s.client_w-eq320-and$s.client_h-eq240) "$arch D3D$api each requested resolution becomes exact client and native PP"}
   else{Check ($s.d3d9.emulated-eq0-or$null-eq$s.d3d9) "$arch D3D$api native windowed positive requires no emulation"}
  }
  $caseEvidence.desktop_after_assert=[ViD9Test]::Desktop()
  Check (-not$s.really_foreground-and$caseEvidence.desktop_after_assert-eq$desktop) "$arch D3D$api $policy preserves background desktop"
 }finally{if($p){$s=Vi $p.Id @('status');if($s.log_path-and(Test-Path $s.log_path)){Copy-Item $s.log_path "$log.vinput.log"};Stop-ViOwnedProcess $p}}}}
}}finally{$checks|ConvertTo-Json -Depth 7|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 18|Set-Content "$Out\evidence.json"}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
