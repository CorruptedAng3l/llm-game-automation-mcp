# Pure-algorithm unit tests (both architectures): text matching and NCC template matching.
param([string]$Out = "$PSScriptRoot\..\build\test\unit")
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
foreach ($name in 'vi_uitest.exe', 'vi_uitest32.exe', 'vi_ncctest.exe', 'vi_ncctest32.exe') {
  if (-not (Test-Path "$D\$name")) { continue }   # the NCC tests exist from the image-matching commit on
  $text = & "$D\$name"
  $code = $LASTEXITCODE
  $text | Set-Content -LiteralPath "$Out\$name.txt"
  if ($code -ne 0 -or -not ($text -contains 'ALL PASS')) { $text | Select-String 'FAIL'; throw "$name failed (exit $code)" }
  "PASS $name ($(@($text | Select-String '^PASS').Count) checks)"
}
$global:LASTEXITCODE = 0
