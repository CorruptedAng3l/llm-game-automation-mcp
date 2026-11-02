# Anti-cheat refusal red/green test (src\vi_acguard.h). Synthetic only: the "anti-cheat" is test\vi_acdummy.cpp, an EMPTY
# DLL that merely carries an anti-cheat-like file name, and a copy of the plain test target renamed like an AC service.
# Every refusal case is paired with a positive control that shows the same target IS injectable without the trigger,
# so a pass cannot come from a broken injector. Run with -CliDir pointing at an older build to see it RED.
param([string]$CliDir = "$PSScriptRoot\..\dist", [string]$Out = "$PSScriptRoot\..\build\test\actest", [switch]$KeepGoing)
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path
$V = (Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
$script:pass = 0; $script:fail = 0; $owned = @()
function Check($cond, $what, $detail = '') {
  if ($cond) { $script:pass++; "PASS $what" } else { $script:fail++; "FAIL $what $detail"; if (-not $KeepGoing) { throw "FAIL $what $detail" } }
}
function Invoke-V([string[]]$a) {
  $raw = & $V @a 2> "$Out\last.stderr.txt"; $code = $LASTEXITCODE
  $line = @($raw | Where-Object { $_ -like '{*' }) | Select-Object -Last 1
  $j = $null; if ($line) { $j = $line | ConvertFrom-Json }
  [pscustomobject]@{ code = $code; j = $j; raw = ($raw -join "`n") }
}
function StartTarget([string]$exe, [string]$name, [string[]]$extra = @()) {
  $log = "$Out\$name.log"; Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
  $argList = @("`"$log`"") + $extra
  $p = Start-Process -FilePath $exe -ArgumentList $argList -WindowStyle Hidden -PassThru
  $script:owned += $p
  for ($i = 0; $i -lt 50 -and -not (Test-Path -LiteralPath $log -PathType Leaf); $i++) { Start-Sleep -Milliseconds 100 }
  for ($i = 0; $i -lt 50 -and -not (Select-String -LiteralPath $log -Pattern 'WINDOW ready' -Quiet -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 100 }
  [pscustomobject]@{ p = $p; log = $log }
}
function HasVinput($p) { $p.Refresh(); @($p.Modules | Where-Object { $_.ModuleName -like 'vinput*' }).Count -gt 0 }
function Refused($r, $product, $kind) { $r.code -eq 4 -and $r.j -and $r.j.ok -eq $false -and $r.j.anticheat_refused -eq $true -and $r.j.anticheat.product -eq $product -and $r.j.anticheat.kind -eq $kind }

$dummy64 = "$D\acdummy\x64\EasyAntiCheat_x64.dll"; $dummy32 = "$D\acdummy\x86\BEClient.dll"
foreach ($f in "$D\vi_actarget.exe", "$D\vi_actarget32.exe", $dummy64, $dummy32) { if (-not (Test-Path -LiteralPath $f)) { throw "missing $f - run build.bat" } }
try {
  # A. positive control: a clean target is injectable and answers
  $clean = StartTarget "$D\vi_actarget.exe" 'clean64'
  $r = Invoke-V @('inject', '--pid', "$($clean.p.Id)")
  Check ($r.code -eq 0 -and $r.j.ok) 'control: clean x64 target is injected' $r.raw
  $r = Invoke-V @('--pid', "$($clean.p.Id)", 'status')
  Check ($r.j.ok -and $r.j.anticheat_disabled -eq $false) 'control: clean target status ok, anticheat_disabled=false' $r.raw

  # B. x64 target with a module named like EasyAntiCheat
  $t = StartTarget "$D\vi_actarget.exe" 'module64' @('--load', "`"$dummy64`"")
  Check (Select-String -LiteralPath $t.log -Pattern 'LOAD ok' -Quiet) 'fixture: dummy EasyAntiCheat_x64.dll is loaded in the x64 target'
  $r = Invoke-V @('inject', '--pid', "$($t.p.Id)")
  Check (Refused $r 'EasyAntiCheat' 'module') 'x64 target with an EasyAntiCheat-named module: inject refused (exit 4)' $r.raw
  Check (-not (HasVinput $t.p)) 'x64 refused target holds no vinput module'
  $r = Invoke-V @('--pid', "$($t.p.Id)", 'ping')
  Check (-not $r.j.ok) 'x64 refused target has no control pipe'

  # C. x86 target with a module named like BattlEye (vinput.exe hands it to vinput32.exe)
  $t = StartTarget "$D\vi_actarget32.exe" 'module32' @('--load', "`"$dummy32`"")
  Check (Select-String -LiteralPath $t.log -Pattern 'LOAD ok' -Quiet) 'fixture: dummy BEClient.dll is loaded in the x86 target'
  $r = Invoke-V @('inject', '--pid', "$($t.p.Id)")
  Check (Refused $r 'BattlEye' 'module') 'x86 target with a BattlEye-named module: inject refused (exit 4)' $r.raw
  $r = Invoke-V @('--pid', "$($t.p.Id)", 'ping')
  Check (-not $r.j.ok) 'x86 refused target has no control pipe'
  $c32 = StartTarget "$D\vi_actarget32.exe" 'clean32'
  $r = Invoke-V @('inject', '--pid', "$($c32.p.Id)")
  Check ($r.code -eq 0 -and $r.j.ok) 'control: clean x86 target is injected' $r.raw

  # D. game folder that ships an EasyAntiCheat component: refused before start (launch) and on inject
  $game = "$Out\fakegame"; Remove-Item -Recurse -Force -LiteralPath $game -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force "$game\Binaries\Win64", "$game\EasyAntiCheat" | Out-Null
  Copy-Item -LiteralPath "$D\vi_actarget.exe" "$game\Binaries\Win64\FakeGame.exe"
  $exe = "$game\Binaries\Win64\FakeGame.exe"
  foreach ($mode in @(@(), @('--early'))) {
    $r = Invoke-V (@('launch', '--exe', $exe, '--args', "`"$Out\fakegame_launch.log`"", '--timeout', '5', '--settle', '0', '--fg-guard', '0') + $mode)
    $started = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
    Check ((Refused $r 'EasyAntiCheat' 'game_folder') -and $started.Count -eq 0) "launch $((@('--exe') + $mode) -join ' ') of an EasyAntiCheat install: refused and nothing started" $r.raw
    foreach ($s in $started) { $script:owned += $s }
  }
  $g = StartTarget $exe 'fakegame_running'
  $r = Invoke-V @('inject', '--pid', "$($g.p.Id)")
  Check (Refused $r 'EasyAntiCheat' 'game_folder') 'inject into a running game from an EasyAntiCheat install: refused' $r.raw
  Remove-Item -Recurse -Force -LiteralPath "$game\EasyAntiCheat"
  $r = Invoke-V @('inject', '--pid', "$($g.p.Id)")
  Check ($r.code -eq 0 -and $r.j.ok) 'control: same game injectable once the EasyAntiCheat folder is gone' $r.raw

  # E. an anti-cheat process running anywhere on the machine (a copy of the plain target named BEService.exe)
  $acDir = "$Out\fakeac"; New-Item -ItemType Directory -Force $acDir | Out-Null
  Copy-Item -LiteralPath "$D\vi_actarget.exe" "$acDir\BEService.exe" -Force
  $other = StartTarget "$D\vi_actarget.exe" 'other64'
  $ac = StartTarget "$acDir\BEService.exe" 'fake_beservice'
  $r = Invoke-V @('inject', '--pid', "$($other.p.Id)")
  Check ((Refused $r 'BattlEye' 'process') -and -not (HasVinput $other.p)) 'inject while a BEService.exe process runs: refused' $r.raw
  $r = Invoke-V @('launch', '--exe', "$D\vi_actarget.exe", '--args', "`"$Out\launch_while_ac.log`"", '--timeout', '5', '--settle', '0', '--fg-guard', '0')
  if ($r.j.pid) { $lp = Get-Process -Id $r.j.pid -ErrorAction SilentlyContinue; if ($lp) { $script:owned += $lp } }
  Check (Refused $r 'BattlEye' 'process') 'launch while a BEService.exe process runs: refused' $r.raw
  Stop-Process -Id $ac.p.Id -Force; $ac.p.WaitForExit(5000) | Out-Null
  $r = Invoke-V @('inject', '--pid', "$($other.p.Id)")
  Check ($r.code -eq 0 -and $r.j.ok) 'control: same target injectable once BEService.exe has exited' $r.raw

  # F. an anti-cheat module loaded AFTER injection disables every input / capture command
  $late = StartTarget "$D\vi_actarget.exe" 'late64' @('--late', "`"$dummy64`"")
  $r = Invoke-V @('inject', '--pid', "$($late.p.Id)"); Check ($r.code -eq 0 -and $r.j.ok) 'late: clean target injected' $r.raw
  $r = Invoke-V @('--pid', "$($late.p.Id)", 'key', 'a'); Check ($r.j.ok) 'late: key a accepted before the anti-cheat module loads' $r.raw
  $r = Invoke-V @('--pid', "$($late.p.Id)", 'key', 'l'); Check ($r.j.ok) 'late: key l accepted (the target loads the dummy module on L)' $r.raw
  for ($i = 0; $i -lt 30 -and -not (Select-String -LiteralPath $late.log -Pattern 'LATE_LOAD ok' -Quiet); $i++) { Start-Sleep -Milliseconds 100 }
  Check (Select-String -LiteralPath $late.log -Pattern 'LATE_LOAD ok' -Quiet) 'fixture: the target loaded EasyAntiCheat_x64.dll after injection'
  $r = Invoke-V @('--pid', "$($late.p.Id)", 'key', 'b')
  Check ($r.code -ne 0 -and $r.j.anticheat_refused -eq $true -and $r.j.anticheat.kind -eq 'module') 'late: key b refused after the anti-cheat module loaded' $r.raw
  $r = Invoke-V @('--pid', "$($late.p.Id)", 'capture', "$Out\late.png")
  Check ($r.j.anticheat_refused -eq $true -and -not (Test-Path -LiteralPath "$Out\late.png")) 'late: capture refused too' $r.raw
  $r = Invoke-V @('--pid', "$($late.p.Id)", 'status')
  Check ($r.j.ok -and $r.j.anticheat_disabled -eq $true) 'late: status still answers and reports anticheat_disabled' $r.raw
  Start-Sleep -Milliseconds 300
  $log = Get-Content -LiteralPath $late.log -Raw
  Check ($log.Contains('WM_KEYDOWN vk=0x41') -and -not $log.Contains('WM_KEYDOWN vk=0x42')) 'late: the target saw A but never B'

  # G. the DLL itself refuses to start in a process that already has an anti-cheat module (e.g. another injector)
  $self = StartTarget "$D\vi_actarget.exe" 'selfload64' @('--load', "`"$dummy64`"", '--load', "`"$((Resolve-Path $CliDir).Path)\vinput64.dll`"")
  Start-Sleep -Milliseconds 1500
  $r = Invoke-V @('--pid', "$($self.p.Id)", 'ping')
  $dllLog = "$((Resolve-Path $CliDir).Path)\logs\vinput_$($self.p.Id).log"
  Check ((-not $r.j.ok) -and (Test-Path -LiteralPath $dllLog) -and (Select-String -LiteralPath $dllLog -Pattern 'REFUSED: anti-cheat' -Quiet)) 'DLL loaded next to an anti-cheat module: no pipe, logs REFUSED' $r.raw
} finally {
  foreach ($p in $owned) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction SilentlyContinue } }
  "owned test processes stopped by PID ($($owned.Count))"
}
"actest: $($script:pass) PASS, $($script:fail) FAIL"
if ($script:fail) { exit 1 }
$global:LASTEXITCODE = 0
