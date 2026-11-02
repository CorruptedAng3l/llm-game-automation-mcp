param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\movetest")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
$results=@()
foreach($name in @('vi_testinput.exe','vi_testinput32.exe')) {
  $p=$null
  try {
    $p=Start-Process -FilePath "$D\$name" -ArgumentList "`"$Out\$name.log`" -28000 600 640 360" -WindowStyle Hidden -PassThru
    $r=& $V inject --pid $p.Id | ConvertFrom-Json; if(-not $r.ok){throw 'Injection failed'}
    Start-Sleep -Milliseconds 300
    & $V --pid $p.Id mouse 100 100 | Out-Null
    $s=& $V --pid $p.Id status | ConvertFrom-Json
    $clamped=& $V --pid $p.Id move 1000 -800 | ConvertFrom-Json
    $results += [pscustomobject]@{target=$name;clamped=$clamped}
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$Out\result.json"
    if(-not $clamped.ok -or $clamped.applied_dx -ne ($s.client_w-1-100) -or $clamped.applied_dy -ne -100 -or $clamped.raw_dx -ne 1000){throw 'Clamped actual-delta reporting unsupported or wrong'}
    $raw=& $V --pid $p.Id move --raw 1003 -999 --steps 7 | ConvertFrom-Json
    if(-not $raw.ok -or $raw.applied_dx -ne 1003 -or $raw.applied_dy -ne -999 -or $raw.cursor -ne 'virtual' -or -not $raw.unbounded -or $raw.x -le $s.client_w -or $raw.y -ge 0){throw 'Unbounded cursor move failed'}
    # the target logs GetCursorPos once per frame; under load a fixed 100 ms wait was flaky, so poll up to 2 s
    $seen=$false
    for($w=0; $w -lt 40 -and -not $seen; $w++){ Start-Sleep -Milliseconds 50; $seen=(Get-Content -LiteralPath "$Out\$name.log" -Raw).Contains("CURSOR $($raw.x) $($raw.y)") }
    if(-not $seen){throw 'Actual GetCursorPos observer did not see unbounded position'}
    $results[-1] | Add-Member -NotePropertyName raw -NotePropertyValue $raw
  } finally { if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false} }
}
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$Out\result.json"
'PASS x64/x86 clamped applied deltas and unbounded GetCursorPos mouse-look'
