param([string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),[string]$Out="$PSScriptRoot\_out\text_line")
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$SourceRoot=(Resolve-Path $SourceRoot).Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP;$cases=@()
foreach($arch in @('x64','x86')){
  $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  $header=if(Test-Path "$SourceRoot\src\vi_text.h"){"$SourceRoot\src"}else{$SourceRoot}
  @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I`"$header`" `"$repo\test\vi_textlinetest.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content -LiteralPath "$folder\build.cmd" -Encoding ascii
  & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";$build=$LASTEXITCODE;if($build){throw "text line $arch compile failed"}
  & "$folder\fixture.exe" *> "$folder\test.log";$exit=$LASTEXITCODE;Get-Content "$folder\test.log";$cases+=@{arch=$arch;exit_code=$exit;header_hash=(Get-FileHash "$header\vi_text.h").Hash}
}
@{completed=$true;cases=$cases;scope='actual pure vi_text header with fabricated OCR lines; no window or OCR API'}|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json"
if(@($cases|Where-Object exit_code -ne 0).Count){exit 1}
