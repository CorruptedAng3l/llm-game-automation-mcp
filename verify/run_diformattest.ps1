param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\di_format")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$SourceRoot=(Resolve-Path $SourceRoot).Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$Out=(Resolve-Path $Out).Path;$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP;$cases=@()
$text=Get-Content "$SourceRoot\src\vinput_core.cpp" -Raw
function Extract([string]$start,[string]$end){$i=$text.IndexOf($start);$j=$text.IndexOf($end,$i);if($i-lt0-or$j-lt$i){throw "missing extraction $start"};$text.Substring($i,$j-$i)}
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 (Extract 'HRESULT DIGetDeviceStateImpl(' 'HRESULT DIGetDeviceDataImpl(')|Set-Content "$folder\actual_state_reader.inl" -Encoding ascii
 (Extract 'HRESULT DIGetDeviceDataImpl(' 'HRESULT STDMETHODCALLTYPE h_DIAcquire(')|Set-Content "$folder\actual_buffer_reader.inl" -Encoding ascii
 Copy-Item "$SourceRoot\src\vi_di_discovery.inl" "$folder\actual_discovery.inl"
 if(Test-Path "$SourceRoot\src\vi_di_format_hooks.inl"){Copy-Item "$SourceRoot\src\vi_di_format_hooks.inl" "$folder\actual_format_hooks.inl"}else{'std::shared_ptr<DiFormatLifetime> DiFormatFind(void*){return nullptr;}'|Set-Content "$folder\actual_format_hooks.inl" -Encoding ascii}
 @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W3 /std:c++17 /I`"$folder`" /I`"$repo\src`" `"$repo\test\vi_diformat_memory.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO || exit /b 2","dumpbin /nologo /imports `"$folder\fixture.exe`" > `"$folder\imports.log`" || exit /b 2")|Set-Content "$folder\build.cmd" -Encoding ascii
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";if($LASTEXITCODE){throw "DI format $arch compile failed"};if(Select-String -LiteralPath "$folder\imports.log" -Pattern '^\s*DINPUT8\.dll\s*$'){throw 'pure fixture unexpectedly imports native DirectInput APIs'};& "$folder\fixture.exe" *> "$folder\test.log";$code=$LASTEXITCODE;Get-Content "$folder\test.log";$cases+=@{arch=$arch;exit_code=$code;native_directinput_import=$false}
}
@{completed=$true;cases=$cases;scope='exact discovery/lifetime/format/state/buffer source and finite COM/hook/memory endpoints; no GUI/native device calls'}|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json"
if(@($cases|Where-Object exit_code -ne 0).Count){exit 1}
