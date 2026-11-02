# script runner: stops at the first failing line, reports its number; macros\ run end to end on the synthetic target.
param([string]$Out = "$PSScriptRoot\..\build\test\script")
. "$PSScriptRoot\_ui_common.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$M = (Resolve-Path "$PSScriptRoot\..\macros").Path
function Run-Script([int]$TargetPid, [string]$File, [string[]]$Extra = @()) {
  $lines = & $script:V --pid $TargetPid script $File @Extra
  $code = $LASTEXITCODE
  $objs = @($lines | Where-Object { $_ -like '{*' } | ForEach-Object { $_ | ConvertFrom-Json })
  return [pscustomobject]@{ Code = $code; Lines = $objs; Summary = $objs[-1] }
}
$t = $null
try {
  $t = Start-UiTarget $Out 'script1'
  $r = Run-Script $t.Pid "$M\synthetic_menu_to_game.vin"
  Require ($r.Code -eq 0 -and $r.Summary.ok -and $r.Summary.commands_run -eq 6) 'macro synthetic_menu_to_game.vin runs all 6 commands and exits 0'
  Require ((Wait-ScreenInLog $t 'GAME' 5)) 'target reached the GAME screen through the macro'
  $clicks = Get-Clicks $t
  Require ($clicks.Count -eq 2 -and $clicks[0].Name -eq 'PLAY' -and $clicks[1].Name -eq 'SOLO') 'only PLAY and SOLO were clicked, in order'
  Stop-UiTarget $t; $t = $null

  $t = Start-UiTarget $Out 'script2'
  $bad = "$Out\fails.vin"
  Set-Content -LiteralPath $bad -Value @('# comment line', '', 'click-text PLAY', 'sleep 100', 'assert-text "THIS TEXT IS NOT ON SCREEN"', 'click-text SOLO', 'echo never reached')
  $r = Run-Script $t.Pid $bad
  Require ($r.Code -ne 0 -and -not $r.Summary.ok -and $r.Summary.stopped) 'a failing assert makes the script exit non-zero and stop'
  Require ($r.Summary.first_failure.failed_line -eq 5 -and $r.Summary.first_failure.line -like 'assert-text*') 'the failing LINE NUMBER (5) and text are reported (comments and blanks counted)'
  Require ($r.Summary.commands_run -eq 3) 'exactly 3 commands ran (click-text, sleep, assert-text); nothing after the failure'
  $clicks = Get-Clicks $t
  Require ($clicks.Count -eq 1 -and $clicks[0].Name -eq 'PLAY') 'SOLO was NOT clicked after the failed assertion'
  $cont = Run-Script $t.Pid $bad @('--continue')
  Require ($cont.Code -ne 0 -and $cont.Summary.failures -ge 1 -and $cont.Summary.commands_run -eq 5 -and -not $cont.Summary.stopped) '--continue runs past the failure but still exits non-zero and counts it'
  $unknown = "$Out\unknown.vin"
  Set-Content -LiteralPath $unknown -Value @('echo hi', 'frobnicate 1 2')
  $r = Run-Script $t.Pid $unknown
  Require ($r.Code -ne 0 -and $r.Summary.first_failure.failed_line -eq 2) 'an unknown command fails at its own line'
  $nest = "$Out\nested.vin"
  Set-Content -LiteralPath $nest -Value @("script $unknown")
  $r = Run-Script $t.Pid $nest
  Require ($r.Code -ne 0 -and $r.Summary.first_failure.line -like 'script*') 'script inside a script is refused'
  $r = & $V --pid $t.Pid script "$Out\does_not_exist.vin" | ConvertFrom-Json
  Require (-not $r.ok) 'missing script file is an error'
  Stop-UiTarget $t; $t = $null

  $t = Start-UiTarget $Out 'script3'
  $r = Run-Script $t.Pid "$M\synthetic_pick_second_battle.vin"
  $clicks = Get-Clicks $t
  Require ($r.Code -eq 0 -and $clicks.Count -eq 2 -and (In-Rect $clicks[0].X $clicks[0].Y 540 340 740 400) -and $clicks[1].Name -eq 'Load___Game') 'macro synthetic_pick_second_battle.vin: right BATTLE then Load Game'
  # relative paths in a script resolve against the script's folder, not the caller's working directory
  $sub = "$Out\relsub"; New-Item -ItemType Directory -Force $sub | Out-Null
  Remove-Item "$sub\shot_rel.png" -ErrorAction SilentlyContinue
  Set-Content -LiteralPath "$sub\rel.vin" -Value @('capture shot_rel.png --region 0,0,100,60', 'pixel 10 10')
  Push-Location $env:SystemRoot
  try { $r = Run-Script $t.Pid "$sub\rel.vin" } finally { Pop-Location }
  Require ($r.Code -eq 0 -and (Test-Path "$sub\shot_rel.png")) 'relative capture path in a script lands next to the script, whatever the caller cwd was'
  'PASS script'; $global:LASTEXITCODE = 0
} finally { Stop-UiTarget $t }
