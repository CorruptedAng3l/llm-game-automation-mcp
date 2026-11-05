# Finite fake module/export/MinHook endpoints; no native hooks, GUI or input.
param([string]$Out="$PSScriptRoot\_out\terminal_batch")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$cases=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;New-Item -ItemType Directory -Force $folder|Out-Null;$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$root\src" /I"$root\test" /I"$root\third_party\minhook\include" "$root\test\vi_terminal_batch_memory.cpp" /Fo:"$folder\fixture.obj" /Fe:"$folder\fixture.exe" /link /PDB:NONE /INCREMENTAL:NO
"@|Set-Content "$folder\build.cmd" -Encoding ascii
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";if($LASTEXITCODE){throw "terminal batch $arch build failed"}
 foreach($style in @('legacy','batch')){foreach($mode in @('normal','alias','missing','conflict','create-fail','enable-fail','route-fail')){
  & "$folder\fixture.exe" $style $mode *> "$folder\$style.$mode.log";$code=$LASTEXITCODE;$text=Get-Content "$folder\$style.$mode.log" -Raw;Write-Host ($text.Trim().Split("`n")[-1]);$cases+=@{arch=$arch;style=$style;mode=$mode;exit=$code;log=$text}
 }}
}
$failed=@($cases|Where-Object exit -ne 0).Count;@{completed=$true;failed_cases=$failed;cases=$cases;scope='exact installers with finite fake endpoints; zero native hook or input execution'}|ConvertTo-Json -Depth 8|Set-Content "$Out\result.json";if($failed){exit 1}
