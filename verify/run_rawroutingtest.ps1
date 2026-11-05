param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\raw_routing")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$SourceRoot=(Resolve-Path $SourceRoot).Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$Out=(Resolve-Path $Out).Path;$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP;$cases=@()
$text=Get-Content "$SourceRoot\src\vinput_core.cpp" -Raw
function Extract([string]$start,[string]$end){$i=$text.IndexOf($start);$j=$text.IndexOf($end,$i);if($i-lt0-or$j-lt$i){throw "missing extraction $start"};$text.Substring($i,$j-$i)}
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 (Extract 'int EmitRaw(' 'void RawKey(')|Set-Content "$folder\actual_emit.inl" -Encoding ascii
 (Extract 'UINT WINAPI h_GetRawInputBuffer(' 'BOOL WINAPI h_RegisterRawInputDevices(')|Set-Content "$folder\actual_reader.inl" -Encoding ascii
 if(Test-Path "$SourceRoot\src\vi_raw_buffer_state.h"){
  ('#define VI_RAW_THREAD_ROUTING 1'+"`n"+(Get-Content "$SourceRoot\src\vi_raw_buffer_state.h" -Raw))|Set-Content "$folder\actual_raw_state.inl" -Encoding ascii;Copy-Item "$SourceRoot\src\vi_raw_buffer_routing.inl" "$folder\actual_raw_routing.inl"
 }else{'std::mutex g_rawQMx; std::deque<RAWINPUT> g_rawQ; std::atomic<DWORD> g_lastRIB{0};'|Set-Content "$folder\actual_raw_state.inl" -Encoding ascii;''|Set-Content "$folder\actual_raw_routing.inl" -Encoding ascii}
 @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W3 /std:c++17 /I`"$folder`" `"$repo\test\vi_rawrouting_memory.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content "$folder\build.cmd" -Encoding ascii
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";if($LASTEXITCODE){throw "raw routing $arch compile failed"};& "$folder\fixture.exe" *> "$folder\test.log";$code=$LASTEXITCODE;Get-Content "$folder\test.log";$cases+=@{arch=$arch;exit_code=$code}
}
@{completed=$true;cases=$cases;scope='actual EmitRaw/GetRawInputBuffer and routing helpers with finite thread/window/native-buffer endpoints; no GUI/OS input'}|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json"
if(@($cases|Where-Object exit_code -ne 0).Count){exit 1}
