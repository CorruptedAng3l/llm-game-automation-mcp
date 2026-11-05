param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\d3d9_lifetime")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$SourceRoot=(Resolve-Path $SourceRoot).Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$Out=(Resolve-Path $Out).Path;$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP;$cases=@()
$source=Get-Content -LiteralPath "$SourceRoot\src\vi_d3d9windowed.h";$factories=@($source|Where-Object{$_ -match '^(IDirect3D9\* WINAPI h_D9Factory\(|HRESULT WINAPI h_D9FactoryEx\()' -and $_.Contains('{')})
if($factories.Count-ne2){throw 'factory extraction must find exactly two production callbacks'}
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 $factories|Set-Content "$folder\actual_factories.inl" -Encoding ascii;Copy-Item -LiteralPath "$SourceRoot\src\vi_d3d9_late.inl" -Destination "$folder\actual_late.inl"
 if(Test-Path "$SourceRoot\src\vi_d3d9_lifetime.inl"){Copy-Item -LiteralPath "$SourceRoot\src\vi_d3d9_lifetime.inl" -Destination "$folder\actual_lifetime.inl"}else{@($source|Where-Object{$_ -match '^void D9Retain\(\)'})|Set-Content "$folder\actual_lifetime.inl" -Encoding ascii}
 if(Test-Path "$SourceRoot\src\vi_d3d9_probe_policy.inl"){Copy-Item -LiteralPath "$SourceRoot\src\vi_d3d9_probe_policy.inl" -Destination "$folder\actual_probe_policy.inl"}else{'std::atomic<unsigned> g_d9ProbeAdmissions{0}; struct D9ProbeNativeAdmission {explicit operator bool()const{return true;}};'|Set-Content "$folder\actual_probe_policy.inl" -Encoding ascii}
 $config=@(Get-Content -LiteralPath "$SourceRoot\src\vinput_core.cpp"|Where-Object{$_ -match 'if \(JHas\(j, "fullscreen"\)\)'});if($config.Count-ne1){throw 'must extract exactly one actual fullscreen config branch'};$config|Set-Content "$folder\actual_config.inl" -Encoding ascii
 @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W3 /std:c++17 /I`"$folder`" `"$repo\test\vi_d3d9_lifetime_memory.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content "$folder\build.cmd" -Encoding ascii
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";if($LASTEXITCODE){throw "D3D9 lifetime $arch build failed"}; & "$folder\fixture.exe" *> "$folder\test.log";$code=$LASTEXITCODE;Get-Content "$folder\test.log";$cases+=@{arch=$arch;exit_code=$code}
}
@{completed=$true;cases=$cases;scope='actual factory wrappers/late-probe source with finite module/GUI/COM fakes; no GUI/GPU/game/OS input'}|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json"
if(@($cases|Where-Object exit_code -ne 0).Count){exit 1}
