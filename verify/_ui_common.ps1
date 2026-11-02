# Shared helpers for the UI-navigation tests (dot-source). Synthetic targets only: no real foreground, no shared lock.
$ErrorActionPreference = 'Stop'
$script:D = (Resolve-Path "$PSScriptRoot\..\dist").Path
$script:V = "$D\vinput.exe"

function Require($condition, [string]$description) {
  if (-not $condition) { throw "FAIL $description" }
  Write-Output "PASS $description"
}
# One CLI call -> parsed JSON (the CLI prints exactly one JSON line for single commands).
function Vi([int]$TargetPid, [string[]]$Arguments) {
  $text = & $script:V --pid $TargetPid @Arguments
  $line = ($text | Where-Object { $_ -like '{*' } | Select-Object -Last 1)
  if (-not $line) { throw "no JSON from vinput $Arguments : $text" }
  return $line | ConvertFrom-Json
}
function Start-UiTarget([string]$Out, [string]$Name, [string[]]$TargetArgs = @(), [string]$Exe = 'vi_uitarget.exe') {
  $log = "$Out\$Name.log"
  $argLine = "`"$log`" -28000 600 " + ($TargetArgs -join ' ')
  $p = Start-Process -FilePath "$script:D\$Exe" -ArgumentList $argLine -WindowStyle Hidden -PassThru
  $r = & $script:V inject --pid $p.Id | ConvertFrom-Json
  if (-not $r.ok) { Stop-Process -Id $p.Id -Force -Confirm:$false; throw "inject failed: $($r | ConvertTo-Json -Compress)" }
  $ready = $false
  for ($i = 0; $i -lt 80; $i++) {
    $s = & $script:V --pid $p.Id status | ConvertFrom-Json
    if ($s.hwnd -ne '0x0' -and $s.counters.present_ours -gt 2) { $ready = $true; break }
    Start-Sleep -Milliseconds 100
  }
  if (-not $ready) { Stop-Process -Id $p.Id -Force -Confirm:$false; throw "target $Name not ready" }
  if ($s.really_foreground) { Stop-Process -Id $p.Id -Force -Confirm:$false; throw "target $Name took the real foreground" }
  return [pscustomobject]@{ Proc = $p; Pid = $p.Id; Log = $log; Name = $Name }
}
function Stop-UiTarget($t) { if ($t -and $t.Proc -and -not $t.Proc.HasExited) { Stop-Process -Id $t.Proc.Id -Force -Confirm:$false } }
# "<tick> CLICK x y name" lines
function Get-Clicks($t) {
  Start-Sleep -Milliseconds 150
  $rows = @()
  foreach ($l in (Get-Content -LiteralPath $t.Log)) {
    if ($l -match '^\d+ CLICK (-?\d+) (-?\d+) (\S+)') { $rows += [pscustomobject]@{ X = [int]$Matches[1]; Y = [int]$Matches[2]; Name = $Matches[3] } }
  }
  return , $rows
}
function Get-LogText($t) { Start-Sleep -Milliseconds 150; return (Get-Content -LiteralPath $t.Log -Raw) }
function In-Rect($x, $y, $l, $t, $r, $b) { return ($x -ge $l -and $x -lt $r -and $y -ge $t -and $y -lt $b) }
function Wait-ScreenInLog($t, [string]$Screen, [int]$Seconds = 8) {
  for ($i = 0; $i -lt $Seconds * 10; $i++) { if ((Get-LogText $t) -match "SCREEN $Screen") { return $true }; Start-Sleep -Milliseconds 100 }
  return $false
}
