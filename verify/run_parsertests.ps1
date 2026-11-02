# Standalone parser regressions. No target processes, injected DLLs, focus, or OS input.
param(
    [string]$Out = "$PSScriptRoot\..\build\test\parsertests",
    [ValidateSet('keys', 'sequence', 'all')][string]$Kind = 'keys'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$outPath = [IO.Path]::GetFullPath($Out)
if (!$outPath.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Parser build output must stay inside this repository.'
}
. "$PSScriptRoot\_vcvars.ps1"; $vcvars = Join-Path (Get-VcVarsDir) 'vcvarsall.bat'
if (!(Test-Path -LiteralPath $vcvars -PathType Leaf)) { throw "MSVC environment missing: $vcvars" }
$failed = $false
$tests = if ($Kind -eq 'all') { @('vi_keystest', 'vi_sequencetest') }
    elseif ($Kind -eq 'sequence') { @('vi_sequencetest') } else { @('vi_keystest') }
foreach ($arch in @('x64', 'x86')) {
    $archOut = Join-Path $outPath $arch
    New-Item -ItemType Directory -Force -Path $archOut | Out-Null
    $batch = Join-Path $archOut 'run.bat'
    $lines = @(
        '@echo off',
        'setlocal',
        ('set "TEMP={0}"' -f $archOut),
        ('set "TMP={0}"' -f $archOut),
        ('call "{0}" {1} >nul || exit /b 2' -f $vcvars, $arch),
        ('cd /d "{0}"' -f $archOut)
    )
    foreach ($test in $tests) {
        $source = Join-Path $repo "test\$test.cpp"
        $lines += ('cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /WX "{0}" /Fo:"{1}.obj" /Fe:"{1}.exe" || exit /b 2' -f $source, $test)
        $lines += ('"{0}\{1}.exe" || exit /b 1' -f $archOut, $test)
    }
    $lines += 'exit /b 0'
    Set-Content -LiteralPath $batch -Value $lines -Encoding ascii
    $result = @(& cmd.exe /d /c $batch 2>&1)
    $code = $LASTEXITCODE
    $result | ForEach-Object { $_.ToString() } | Set-Content -LiteralPath (Join-Path $archOut 'results.txt') -Encoding utf8
    $result | ForEach-Object { Write-Output $_.ToString() }
    if ($code -ne 0) { Write-Output "FAIL parser regressions $arch (exit $code)"; $failed = $true }
    else { Write-Output "PASS parser regressions $arch (/W4 /WX)" }
}
if ($failed) { exit 1 }
exit 0
