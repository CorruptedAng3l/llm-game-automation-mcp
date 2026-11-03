# Four background input/capture profiles; no real foreground prerequisite.
param([string]$Out = "$PSScriptRoot\..\build\test\_out\profiles")
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = "$D\vinput.exe"
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$log = "$Out\selftest_log.txt"
Set-Content -LiteralPath $log -Value ''
function Note($m) { ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m) | Tee-Object -Append $log }
function Require($condition, $description) { if (-not $condition) { throw "FAIL $description" }; Note "PASS $description" }
function Command([int]$targetPid, [string[]]$command) {
  $r = & $V --pid $targetPid @command | ConvertFrom-Json
  if (-not $r.ok) { throw "Command failed: $command : $($r | ConvertTo-Json -Compress)" }
  return $r
}
$cases = @(
  @{ n='x64_dll'; exe="$D\vi_testinput.exe"; dll=''; bits=64; key='q'; vk='51'; dik='10' },
  @{ n='x86_dll'; exe="$D\vi_testinput32.exe"; dll=''; bits=32; key='r'; vk='52'; dik='13' },
  @{ n='x64_compiled'; exe="$D\vi_testinput.exe"; dll="$D\compilein_test64.dll"; bits=64; key='s'; vk='53'; dik='1F' },
  @{ n='x86_compiled'; exe="$D\vi_testinput32.exe"; dll="$D\compilein_test32.dll"; bits=32; key='t'; vk='54'; dik='14' }
)
$started = @{}
try {
  foreach ($c in $cases) {
    $targetLog = "$Out\$($c.n).log"
    $p = Start-Process -FilePath $c.exe -ArgumentList "`"$targetLog`" -28000 600 420 260" -WindowStyle Hidden -PassThru
    $started[$c.n] = $p
    $injectArgs = @('inject','--pid',"$($p.Id)"); if ($c.dll) { $injectArgs += @('--dll',$c.dll) }
    $r = & $V @injectArgs | ConvertFrom-Json
    Require $r.ok "$($c.n) injection"
    $ready=$false
    for ($i=0; $i -lt 50; $i++) {
      $s = Command $p.Id @('status')
      if ($s.hwnd -ne '0x0' -and $s.counters.present_ours -gt 0) { $ready=$true; break }
      Start-Sleep -Milliseconds 100
    }
    Require $ready "$($c.n) target ready"
  }
  foreach ($c in $cases) {
    $targetPid=$started[$c.n].Id
    Command $targetPid @('key',$c.key,'160') | Out-Null
    Command $targetPid @('click','left','50','40') | Out-Null
    Command $targetPid @('move','7','3') | Out-Null
    Command $targetPid @('wheel','120') | Out-Null
    Command $targetPid @('type','hi','--delay','10') | Out-Null
    $cap=Command $targetPid @('capture',"$Out\$($c.n).png")
    $s=Command $targetPid @('status')
    Require ($s.bits -eq $c.bits -and -not $s.really_foreground) "$($c.n) architecture and background isolation"
    Require ($cap.method -eq 'swapchain' -and $cap.w -gt 0 -and $cap.h -gt 0) "$($c.n) native D3D11 capture"
    $t=Get-Content -LiteralPath "$Out\$($c.n).log" -Raw
    foreach ($pattern in @("WM_KEYDOWN vk=0x$($c.vk)","WM_KEYUP vk=0x$($c.vk)","GAKS vk=0x$($c.vk) down","RAW_KBD vk=0x$($c.vk) down","DI_KBD dik=0x$($c.dik) down",'WM_LBUTTONDOWN 50 40','RAW_MOUSE dx=7 dy=3','DI_MOUSE dx=7 dy=3','WM_MOUSEWHEEL 120',"WM_CHAR 104 'h'","WM_CHAR 105 'i'")) {
      Require ($t.Contains($pattern)) "$($c.n) $pattern"
    }
    foreach ($other in $cases) { if ($other.n -ne $c.n) { Require (-not $t.Contains("WM_KEYDOWN vk=0x$($other.vk)")) "$($c.n) no $($other.key) cross-talk" } }
    if ($c.dll) {
      $pf=Get-ChildItem "$D\run" -Filter "$([IO.Path]::GetFileNameWithoutExtension($c.dll))_*.presents.txt" | Sort-Object LastWriteTime | Select-Object -Last 1
      Require ($pf -and [int](Get-Content -LiteralPath $pf.FullName) -gt 0 -and $s.counters.present_ours -gt 0) "$($c.n) host DLL and vinput Present hooks chain"
    }
  }
  # The four-profile isolation/chain checks are complete; release their render/poll
  # load before running independent suites, which each create their own targets.
  foreach ($p in $started.Values) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
  $started.Clear()
  Note 'PASS all four input/capture profiles stayed in the background'
} finally {
  foreach ($ownedTarget in $started.Values) { if (-not $ownedTarget.HasExited) { Stop-Process -Id $ownedTarget.Id -Force -Confirm:$false } }
  Note 'Owned profile targets stopped by PID'
}
$global:LASTEXITCODE=0