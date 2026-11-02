# Standalone JSON-lock controls. Builds/runs only inside this repository's build\test directory.
param([string]$Out = "$PSScriptRoot\..\build\test\locktest")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$Out = [IO.Path]::GetFullPath($Out)
if (-not $Out.StartsWith("$root\", [StringComparison]::OrdinalIgnoreCase)) { throw 'Lock-test artifacts must stay within this repository' }
. "$PSScriptRoot\_vcvars.ps1"; $vcDir = Get-VcVarsDir
New-Item -ItemType Directory -Force -Path $Out, "$Out\temp" | Out-Null
$oldTemp = $env:TEMP; $oldTmp = $env:TMP
try {
  $env:TEMP = "$Out\temp"; $env:TMP = "$Out\temp"
  foreach ($arch in @('x64','x86')) {
    $archDir = "$Out\$arch"
    New-Item -ItemType Directory -Force -Path $archDir | Out-Null
    $vcvars = if ($arch -eq 'x64') { 'vcvars64.bat' } else { 'vcvars32.bat' }
    $fixture = "$archDir\owned_$([guid]::NewGuid().ToString('N')).lock"
    $batch = @"
@echo off
setlocal
set "TEMP=$Out\temp"
set "TMP=$Out\temp"
call "$vcDir\$vcvars" >nul || exit /b 1
pushd "$archDir"
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /O2 /I"$root\src" "$root\test\vi_locktest.cpp" /Fo:vi_locktest.obj /Fe:vi_locktest.exe || exit /b 1
"$archDir\vi_locktest.exe" "$fixture"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
"@
    $buildScript = "$archDir\run.cmd"
    [IO.File]::WriteAllText($buildScript, $batch, [Text.Encoding]::ASCII)
    & cmd.exe /d /c $buildScript *> "$archDir\result.txt"
    $code = $LASTEXITCODE
    Get-Content -LiteralPath "$archDir\result.txt"
    if ($code -ne 0) { throw "$arch lock test failed (exit $code); see $archDir\result.txt" }
    $content = Get-Content -LiteralPath "$archDir\result.txt" -Raw
    if ($content -notmatch 'PASS: 48 checks, 0 failures' -or $content -match '(?im)^FAIL|warning [A-Z]+\d+|error [A-Z]+\d+') { throw "$arch lock-test output is incomplete or contains compiler diagnostics" }
    if (Test-Path -LiteralPath $fixture) { throw "$arch left an owned test lock behind: $fixture" }
  }
  'PASS JSON locks: 48 controls each on x64 and x86; 0 warnings'
} finally {
  $env:TEMP = $oldTemp; $env:TMP = $oldTmp
}
