param([string]$CliDir="$PSScriptRoot\..\dist", [string]$Out="$PSScriptRoot\..\build\test\activatetest")
$ErrorActionPreference='Stop'
$V=(Resolve-Path "$CliDir\vinput.exe").Path
$T=(Resolve-Path "$PSScriptRoot\..\dist\vi_testinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out=(Resolve-Path $Out).Path
$proc=$null
try {
  $proc=Start-Process -FilePath $T -ArgumentList "`"$Out\target.log`" -28000 600 640 360" -WindowStyle Hidden -PassThru
  $r=& $V inject --pid $proc.Id | ConvertFrom-Json
  if (-not $r.ok) { throw 'Injection failed' }
  Start-Sleep -Milliseconds 300
  & $V --pid $proc.Id config fakefocus=false | Out-Null
  $before=(Get-Content -LiteralPath "$Out\target.log" -Raw)
  $r=& $V --pid $proc.Id activate | ConvertFrom-Json
  Start-Sleep -Milliseconds 150
  $after=(Get-Content -LiteralPath "$Out\target.log" -Raw)
  $result=[ordered]@{ok=($r.ok -and $r.activation -eq 'virtual' -and $r.posts_activation -eq $false -and -not $r.really_foreground); reply=$r; activation_messages_added=([regex]::Matches($after,'WM_ACTIVATEAPP 1').Count - [regex]::Matches($before,'WM_ACTIVATEAPP 1').Count)}
  $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$Out\result.json"
  $result | ConvertTo-Json -Depth 5 -Compress
  if (-not $result.ok -or $result.activation_messages_added -ne 0) { throw 'Virtual activation regression failed' }
  'PASS activation enables virtual focus without activation messages or real foreground'
} finally { if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -Confirm:$false } }
