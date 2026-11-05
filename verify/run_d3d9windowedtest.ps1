param([string]$Out="$PSScriptRoot\d3d9_windowed\after",[string]$DllDir='',[string]$CliDir='',[string]$BuildObjectsRoot='',[switch]$BuildOnly,[switch]$KeepGoing,[ValidateSet('x64','x86')][string[]]$Architectures=@('x64','x86'),[ValidateSet('9','ex')][string[]]$Apis=@('9','ex'),[switch]$NativeContext)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;if(!$DllDir){$DllDir="$root\dist"};$DllDir=(Resolve-Path -LiteralPath $DllDir).Path;if(!$CliDir){$CliDir="$root\dist"};$CliDir=(Resolve-Path -LiteralPath $CliDir).Path;if(!$BuildObjectsRoot){$BuildObjectsRoot="$root\build"};$BuildObjectsRoot=(Resolve-Path -LiteralPath $BuildObjectsRoot).Path
$Out=[IO.Path]::GetFullPath($Out);if(-not$Out.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay within private repository'}
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path -LiteralPath $Out).Path;$runId=[guid]::NewGuid().ToString('N');$run=Join-Path $Out ('run_'+$runId);[void](New-Item -ItemType Directory -Force $run,"$run\temp");$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$processes=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new();$current=$null;$serial=0;$token=0;$instrument=$true;$complete=$false
. "$PSScriptRoot\vi_d3d9_fixture_support.ps1"
if(-not('ViD9Test'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViD9Test{
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetPropW(IntPtr h,string name);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool EnumDisplaySettingsW(string name,int mode,byte[] data);
 public static string Desktop(){byte[] b=new byte[220];b[68]=220;if(!EnumDisplaySettingsW(null,-1,b))throw new Exception("display snapshot unavailable");return BitConverter.ToString(b);}
}
'@}
function Check($pass,$name){$checks.Add(@{pass=[bool]$pass;name=$name});"$(if($pass){'PASS'}else{'FAIL'}) $name";if(!$pass-and!$KeepGoing){throw $name}}
function Log($path){if(!(Test-Path $path)){return ''};$f=[IO.FileStream]::new($path,'Open','Read',([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));$r=[IO.StreamReader]::new($f);try{return $r.ReadToEnd()}finally{$r.Dispose();$f.Dispose()}}
function WaitLog($path,$pattern){$until=[Environment]::TickCount64+7000;do{if((Log $path)-match$pattern){return $true};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until);return $false}
function Vi($target,[string[]]$arguments){D9Require ($current-and$current.pid-eq$target) 'CLI target is the retained current fixture';D9Live $current;return D9Reply (D9RunOwned $current.cli (@('--json','--pid',[string]$target)+$arguments))}
try{foreach($source in @($PSCommandPath,"$PSScriptRoot\vi_d3d9_fixture_support.ps1","$PSScriptRoot\vi_r1_identity.ps1","$PSScriptRoot\vi_exited_cli_accounting.ps1","$PSScriptRoot\vi_r1_lifecycle.ps1","$root\test\vi_r1_tail.cs","$root\test\vi_r2_spies.h","$root\test\vi_d3d9_windowed.cpp","$root\test\vi_d3d9guard.cpp","$root\test\vi_d3d9_fixture_geometry.h","$root\test\vi_d3d9_fixture_geometry_test.cpp")){D9Pin $source};foreach($arch in $Architectures){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$d="$Out\$arch\run_$runId";New-Item -ItemType Directory -Force "$d\temp"|Out-Null;$cli=Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'});$mh=Join-Path $BuildObjectsRoot "$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'}
 foreach($object in @("$BuildObjectsRoot\$arch\vimh_buffer.obj","$BuildObjectsRoot\$arch\vimh_hook.obj","$BuildObjectsRoot\$arch\vimh_trampoline.obj","$BuildObjectsRoot\$arch\vimh_hde.obj","$mh\buffer.obj","$mh\hook.obj","$mh\trampoline.obj","$mh\$hde.obj")){D9Pin $object}
 $aliases=if($arch-eq'x64'){"Direct3DCreate9=GuardFactory`nDirect3DCreate9Ex=GuardFactoryEx`nViGuardLog=GuardLog`nViGuardStats=GuardStats"}else{"Direct3DCreate9=_GuardFactory@4`nDirect3DCreate9Ex=_GuardFactoryEx@8`nViGuardLog=_GuardLog@4`nViGuardStats=_GuardStats@4"};"EXPORTS`n$aliases"|Set-Content "$d\guard.def" -Encoding ascii
 $batch=@"
@echo off
set "TEMP=$d\temp"
set "TMP=$d\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$root\src" /I"$root\third_party\minhook\include" /LD "$root\test\vi_d3d9guard.cpp" /Fo:"$d\guard.obj" /Fe:"$d\d3d9.dll" /link /DEF:"$d\guard.def" "$BuildObjectsRoot\$arch\vimh_buffer.obj" "$BuildObjectsRoot\$arch\vimh_hook.obj" "$BuildObjectsRoot\$arch\vimh_trampoline.obj" "$BuildObjectsRoot\$arch\vimh_hde.obj" || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$root\third_party\minhook\include" "$root\test\vi_d3d9_windowed.cpp" /Fo:"$d\fixture.obj" /Fe:"$d\fixture.exe" /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj" || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_d3d9_fixture_geometry_test.cpp" /Fo:"$d\geometry.obj" /Fe:"$d\geometry.exe" /link /SUBSYSTEM:CONSOLE || exit /b 2
"@;[IO.File]::WriteAllText("$d\build.cmd",$batch,[Text.Encoding]::ASCII);$build=D9RunOwned $env:ComSpec @('/d','/c',"$d\build.cmd") 120000;[IO.File]::WriteAllText("$d\build.log",$build.stdout+$build.stderr);Check ($build.exit-eq0-and($build.stdout+$build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') "$arch D3D9 fixture and native guard W4 WX";if($build.exit-ne0){throw 'fixture build failed'}
 foreach($artifact in @("$d\d3d9.dll","$d\fixture.exe","$d\geometry.exe")){D9Pin $artifact};$model=D9RunOwned "$d\geometry.exe" @('--await-start') 15000 $true;D9Require ($model.exit-eq0-and$model.stdout-match'D9_GEOMETRY_RESULT 9 0') 'pure geometry safety model9/0';if($BuildOnly){continue};D9Pin $cli;D9Pin (Join-Path $DllDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'}))
 foreach($api in $Apis){foreach($policy in @('fullscreen','windowed','refuse')){$p=$null;$log="$d\${api}_$policy.log";$desktop=[ViD9Test]::Desktop();try{
  $current=D9Start "$d\fixture.exe" $cli $log $api $policy;$p=$current.process;Check (WaitLog $log 'READY_GUARDED') "$arch D3D$api $policy guarded target ready"
  $dll=Join-Path $DllDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'});D9Live $current;$inj=D9Reply (D9RunOwned $cli @('--json','inject','--pid',[string]$p.Id,'--dll',$dll));Check $inj.ok "$arch D3D$api $policy inject"
  if($NativeContext){Vi $p.Id @('config','fakefocus=false')|Out-Null};if($policy-eq'refuse'){Vi $p.Id @('config','fullscreen=refuse')|Out-Null};$s=Vi $p.Id @('status');$null=D9Control 1;Check (WaitLog $log '(?m)^EXIT \d+') "$arch D3D$api $policy bounded native result"
  $text=Log $log;$s=Vi $p.Id @('status')
  $caseEvidence=@{arch=$arch;api=$api;policy=$policy;native_context=[bool]$NativeContext;target_pid=$p.Id;log_path=$log;dll=$dll;status=$s;target_log=$text;desktop_before=$desktop;desktop_after=$null;desktop_snapshot_error=$null;check_index_before_case_assertions=$checks.Count}
  $evidence.Add($caseEvidence)
  $diagnostics=$s.d3d9.diagnostics
  Check ($diagnostics.version-eq1-and@($diagnostics.operations).Count-eq5) "$arch D3D$api $policy actual native status exposes the versioned five-operation diagnostics"
  $createName=if($api-eq'ex'){'CreateDeviceEx'}else{'CreateDevice'}
  $create=$diagnostics.operations|Where-Object method -eq $createName|Select-Object -First 1
  if($policy-eq'refuse'){
   Check ($create.wrapper_entries-match'^[1-9][0-9]*$'-and$create.wrapper_failures-match'^[1-9][0-9]*$'-and$create.native_calls-eq'0'-and$create.native_returns-eq'0'-and$null-eq$create.last_native_hresult) "$arch D3D$api refusal diagnostics distinguish wrapper failure from an uncalled native create"
  }else{
   Check ($create.native_calls-match'^[1-9][0-9]*$'-and$create.native_returns-match'^[1-9][0-9]*$'-and$create.native_success_returns-match'^[1-9][0-9]*$'-and$null-ne$create.last_native_hresult) "$arch D3D$api $policy diagnostics retain successful original create returns"
  }
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
 }finally{try{if($current-and-not$current.process.HasExited){try{$s=Vi $current.pid @('status');if($s.log_path-and(Test-Path -LiteralPath $s.log_path)){Copy-Item -LiteralPath $s.log_path -Destination "$log.vinput.log"}}catch{$current.final_status_error=$_.Exception.Message}}}finally{D9Stop}}}}
};$complete=$true}finally{D9Stop;$checks|ConvertTo-Json -Depth 7|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 18|Set-Content "$Out\evidence.json";@{completed=$complete-and$instrument;build_only=[bool]$BuildOnly;native_cases_expected=$Architectures.Count*$Apis.Count*3;native_cases_executed=$evidence.Count;checks=$checks;calls=$calls;processes=$processes;pins=$pins;scope='same original native assertions with strict retained lifecycle and terminal desktop-writer guards; only proved own nonactivating offscreen geometry may delegate'}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed-or-not$complete-or-not$instrument){exit 1};exit 0
