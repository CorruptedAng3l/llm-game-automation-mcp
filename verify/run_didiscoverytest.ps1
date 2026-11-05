param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\di_discovery")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$SourceRoot=(Resolve-Path $SourceRoot).Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$Out=(Resolve-Path $Out).Path;$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP;$cases=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 if(Test-Path "$SourceRoot\src\vi_di_discovery.inl"){Copy-Item "$SourceRoot\src\vi_di_discovery.inl" "$folder\actual_discovery.inl"}else{
  $text=Get-Content "$SourceRoot\src\vinput_core.cpp" -Raw;$start=$text.IndexOf('void TryHookDInput() {');$end=$text.IndexOf('// Legacy dinput.dll',$start);if($start-lt0-or$end-lt$start){throw 'baseline probe adapter not found'}
  ($text.Substring($start,$end-$start)+"`nvoid TryDi8Factory(HMODULE=nullptr){TryHookDInput();}")|Set-Content "$folder\actual_discovery.inl" -Encoding ascii
 }
 @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W3 /std:c++17 /I`"$folder`" `"$repo\test\vi_di_discovery_memory.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content "$folder\build.cmd" -Encoding ascii
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";if($LASTEXITCODE){throw "DI discovery $arch build failed"}; & "$folder\fixture.exe" *> "$folder\test.log";$code=$LASTEXITCODE;Get-Content "$folder\test.log";$cases+=@{arch=$arch;exit_code=$code}
}
@{completed=$true;cases=$cases;scope='actual discovery/probe source, finite COM/module/MinHook models; no GUI/device rights/OS input'}|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json"
if(@($cases|Where-Object exit_code -ne 0).Count){exit 1}
