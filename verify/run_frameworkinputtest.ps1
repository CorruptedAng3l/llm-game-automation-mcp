param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\framework_input")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$SourceRoot=(Resolve-Path $SourceRoot).Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$Out=(Resolve-Path $Out).Path;$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP
$text=Get-Content "$SourceRoot\src\vinput_core.cpp" -Raw;$cases=@();$passes=0;$fails=0
function Extract([string]$start,[string]$end){$i=$text.IndexOf($start);if($i-lt0){throw "missing extraction $start"};$j=$text.IndexOf($end,$i);if($j-lt$i){throw "missing end $end"};$text.Substring($i,$j-$i)}
$modern=$text.Contains('struct FrameworkWindow ')
foreach($arch in @('x64','x86')){
 $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
 if($modern){("#define VI_FRAMEWORK_POLICY 1`n"+(Extract 'struct FrameworkWindow ' 'void MouseButton(int vk, bool down);'))|Set-Content "$folder\actual_framework_routes.inl";Copy-Item "$SourceRoot\src\vi_framework_window.inl" "$folder\actual_framework_probe.inl"}
 else{('struct FrameworkWindow{bool chromium=false;};FrameworkWindow FrameworkProfileForCurrent(){return {};}'+"`n"+(Extract 'bool MsgsWanted(' 'void MouseButton(int vk, bool down);'))|Set-Content "$folder\actual_framework_routes.inl";''|Set-Content "$folder\actual_framework_probe.inl"}
 $hold=($text-split'\r?\n'|Where-Object {$_-like'*const double countValue=JNum(j,"count",1),ms=*'}|Select-Object -First 1).Trim()
 $hover=($text-split'\r?\n'|Where-Object {$_-like'*double timeoutValue = JNum(j, "hover_timeout_ms", 1500), framesValue*'}|Select-Object -First 1).Trim()
 if(-not$hold-or-not$hover){throw 'timing expression extraction unavailable'}
 @("static double ActualHold(const std::string& cmd,const JObj& j){const auto clickFramework=FrameworkProfileForCurrent();(void)clickFramework;$hold (void)countValue;(void)gap;(void)pollCapValue;return ms;}","static double ActualHover(const JObj& j){const auto clickFramework=FrameworkProfileForCurrent();(void)clickFramework;$hover (void)timeoutValue;(void)framesValue;return settleValue;}")|Set-Content "$folder\actual_framework_timing.inl"
 @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I`"$folder`" /I`"$repo\src`" `"$repo\test\vi_framework_input_memory.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content "$folder\build.cmd"
 & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";if($LASTEXITCODE){Get-Content "$folder\build.log";throw "framework input $arch compile failed"}
 & "$folder\fixture.exe" *> "$folder\test.log";$code=$LASTEXITCODE;$log=Get-Content "$folder\test.log" -Raw;Write-Output $log
 if($log-notmatch'framework input: (\d+) PASS / (\d+) FAIL'){throw 'missing complete framework input footer'};$passes+=[int]$Matches[1];$fails+=[int]$Matches[2];$cases+=@{arch=$arch;exit=$code;passed=[int]$Matches[1];failed=[int]$Matches[2]}
}
@{completed=$true;passed=$passes;failed=$fails;cases=$cases;scope='actual current framework probe/routes/click timing expressions with finite read-only HWND/binding endpoints; no OS input, GUI or retail gameplay'}|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json"
Write-Output "$passes PASS / $fails FAIL";if($fails){exit 1};exit 0
