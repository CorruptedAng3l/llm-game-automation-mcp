param([string]$Out="$PSScriptRoot\_out\ocr_read",[switch]$RealOcr)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$Out=(Resolve-Path $Out).Path;$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP;$cases=@()
foreach($arch in @('x64','x86')){
  $folder=Join-Path $Out $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  @('@echo off',"call `"$vcDir\$vc`" >nul || exit /b 2","cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++20 /I`"$repo\src`" `"$repo\test\vi_ocrreadtest.cpp`" /Fo:`"$folder\fixture.obj`" /Fe:`"$folder\fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO")|Set-Content -LiteralPath "$folder\build.cmd" -Encoding ascii
  & cmd /d /c "$folder\build.cmd" *> "$folder\build.log";if($LASTEXITCODE){throw "OCR read $arch compile failed"}
  & "$folder\fixture.exe" *> "$folder\test.log";$exit=$LASTEXITCODE;Get-Content "$folder\test.log";$cases+=@{name="pure_$arch";exit_code=$exit;log="$folder\test.log"}
}
if($RealOcr){
  $folder=Join-Path $Out 'x64';$proc=Start-Process -FilePath "$folder\fixture.exe" -ArgumentList '--real' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$folder\real.log" -RedirectStandardError "$folder\real.stderr.log"
  [void]$proc.Handle # Retain the process handle so Windows PowerShell 5 can read ExitCode after WaitForExit.
  if(!$proc.WaitForExit(45000)){$proc.Kill();$proc.WaitForExit();throw 'memory-only OCR exceeded 45s bound'};$proc.WaitForExit();Get-Content "$folder\real.log";$cases+=@{name='real_x64';exit_code=$proc.ExitCode;log="$folder\real.log"}
}
Push-Location "$repo\mcp"
try{& npm run build *> "$Out\mcp_build.log";if($LASTEXITCODE){throw 'TypeScript compile failed'};& node test/t_ocr_read.mjs *> "$Out\mcp_test.log";$code=$LASTEXITCODE;Get-Content "$Out\mcp_test.log";$cases+=@{name='fake_mcp';exit_code=$code;log="$Out\mcp_test.log"}}finally{Pop-Location}
$passed=0;$failed=0;foreach($case in $cases){$passed+=@(Select-String -LiteralPath $case.log -Pattern '^PASS ').Count;$failed+=@(Select-String -LiteralPath $case.log -Pattern '^FAIL ').Count}
@{completed=$true;passed=$passed;failed=$failed;cases=$cases;scope='pure callback pipeline, pixel preprocessing and fake MCP; optional RealOcr renders a memory bitmap only, without windows/focus/GPU/OS input'}|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json"
if(@($cases|Where-Object exit_code -ne 0).Count){exit 1}
