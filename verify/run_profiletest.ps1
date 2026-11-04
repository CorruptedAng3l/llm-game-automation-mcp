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
  $result=Invoke-ProfileCli (@('--pid',"$targetPid")+$command) 15000
  if ($result.timed_out-or$result.exit_code-ne0-or-not$result.reply.ok) { throw "Command failed: $command : $($result.reply | ConvertTo-Json -Compress) (timeout=$($result.timed_out))" }
  return $result.reply
}
$records=[Collections.Generic.List[object]]::new();$failures=[Collections.Generic.List[object]]::new();$ownedCli=[Collections.Generic.List[Diagnostics.Process]]::new()
$activeCase='';$finished=$false
function Read-ProfileText([string]$path) {
  $stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete)
  $reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}
}
function Invoke-ProfileCli([string[]]$arguments,[int]$budgetMs) {
  $info=[Diagnostics.ProcessStartInfo]::new($V);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
  $info.ArgumentList.Add('--json');foreach($argument in $arguments){$info.ArgumentList.Add($argument)}
  $clock=[Diagnostics.Stopwatch]::StartNew();$process=[Diagnostics.Process]::Start($info);$ownedCli.Add($process)
  $stdoutTask=$process.StandardOutput.ReadToEndAsync();$stderrTask=$process.StandardError.ReadToEndAsync()
  $done=$process.WaitForExit($budgetMs)
  if(-not$done){try{$process.Kill()}catch{};[void]$process.WaitForExit(2000)}
  [void]$stdoutTask.Wait(1000);[void]$stderrTask.Wait(1000);$clock.Stop()
  $stdout=if($stdoutTask.IsCompleted){$stdoutTask.GetAwaiter().GetResult()}else{''};$stderr=if($stderrTask.IsCompleted){$stderrTask.GetAwaiter().GetResult()}else{''}
  $reply=$null;$parseError=$null;$lines=@($stdout-split'\r?\n'|Where-Object {$_.Trim().Length})
  try{if($lines.Count-ne1){throw "expected one JSON line, got$($lines.Count)"};$reply=$lines[0]|ConvertFrom-Json}catch{$parseError=$_.Exception.Message}
  $result=[pscustomobject]@{case=$script:activeCase;arguments=$arguments;owned_cli_pid=$process.Id;budget_ms=$budgetMs;elapsed_ms=$clock.ElapsedMilliseconds;timed_out=(-not$done);exit_code=$(if($process.HasExited){$process.ExitCode}else{$null});stdout=$stdout;stderr=$stderr;parse_error=$parseError;reply=$reply}
  $records.Add($result);return $result
}
function Read-HostMetrics($case) {
  if(-not$case.host_metrics){return $null}
  for($attempt=0;$attempt-lt4;$attempt++){
    try{$value=(Read-ProfileText $case.host_metrics)|ConvertFrom-Json;if($value.pid-eq$case.pid){return $value}}catch{}
    Start-Sleep -Milliseconds 10
  }
  return $null
}
function Copy-HostReceipts($case,[string]$label) {
  $metrics=Read-HostMetrics $case
  if($metrics){$metrics|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$Out\${label}_$($case.n)_$($case.pid).host.json"}
  if($case.host_journal-and(Test-Path -LiteralPath $case.host_journal)){
    try{[IO.File]::WriteAllText("$Out\${label}_$($case.n)_$($case.pid).host.log",(Read-ProfileText $case.host_journal))}catch{}
  }
  return $metrics
}
function Save-ProfileFailure($case,[string]$reason) {
  $first=Invoke-ProfileCli @('--pid',"$($case.pid)",'status') 1200
  Start-Sleep -Milliseconds 100
  $second=Invoke-ProfileCli @('--pid',"$($case.pid)",'status') 1200
  $queue=Invoke-ProfileCli @('--pid',"$($case.pid)",'readiness','--wait-ms','100') 1200
  $metrics=Copy-HostReceipts $case 'failure'
  $threads=@();try{$target=Get-Process -Id $case.pid -ErrorAction Stop;foreach($thread in $target.Threads){$wait=$null;if($thread.ThreadState-eq[Diagnostics.ThreadState]::Wait){try{$wait=$thread.WaitReason.ToString()}catch{}};$threads+=[pscustomobject]@{id=$thread.Id;state=$thread.ThreadState.ToString();wait_reason=$wait;cpu_ms=$thread.TotalProcessorTime.TotalMilliseconds}}}catch{}
  $dllLog=if($second.reply.log_path){$second.reply.log_path}elseif($case.initial_status.log_path){$case.initial_status.log_path}else{''}
  if($dllLog-and(Test-Path -LiteralPath $dllLog)){try{[IO.File]::WriteAllText("$Out\failure_$($case.n)_$($case.pid).vinput.log",(Read-ProfileText $dllLog))}catch{}}
  $failure=[pscustomobject]@{case=$case.n;pid=$case.pid;started_utc=$case.started_utc;exe=$case.exe;module_path=$case.module_path;module_sha256=$case.module_sha256;reason=$reason;first_status=$first;second_status=$second;queue_probe=$queue;host=$metrics;threads=$threads}
  $failures.Add($failure);$failure|ConvertTo-Json -Depth 18|Set-Content -LiteralPath "$Out\failure_$($case.n)_$($case.pid).json"
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
    $activeCase=$c.n
    $targetLog = "$Out\$($c.n).log"
    $p = Start-Process -FilePath $c.exe -ArgumentList "`"$targetLog`" -28000 600 420 260" -WindowStyle Hidden -PassThru
    $started[$c.n] = $p
    $c.pid=$p.Id;$c.started_utc=$p.StartTime.ToUniversalTime().ToString('o')
    $injectArgs = @('inject','--pid',"$($p.Id)"); if ($c.dll) { $injectArgs += @('--dll',$c.dll) }
    $injection=Invoke-ProfileCli $injectArgs 40000;$r=$injection.reply
    Require $r.ok "$($c.n) injection"
    $c.injection=$r;$c.module_path=$r.dll
    if($c.module_path){$c.module_sha256=(Get-FileHash -LiteralPath $c.module_path -Algorithm SHA256).Hash}
    if($c.dll){$c.host_metrics="$($c.module_path).host.$($p.Id).json";$c.host_journal="$($c.module_path).host.$($p.Id).log"}
    $ready=$false
    for ($i=0; $i -lt 50; $i++) {
      $s = Command $p.Id @('status')
      if ($s.hwnd -ne '0x0' -and $s.counters.present_ours -gt 0) { $ready=$true; break }
      Start-Sleep -Milliseconds 100
    }
    Require $ready "$($c.n) target ready"
    $c.initial_status=$s
    if($c.dll) {
      $until=[Environment]::TickCount64+10000;$host=$null
      do{$host=Read-HostMetrics $c;if($host.failed){throw "$($c.n) host hook installation failed: $($host.phase)"};if($host.installed-and$host.returned-gt0){break};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$until)
      Require ($host.installed-and$host.returned-gt0-and$host.mh_create-eq0-and$host.mh_enable-eq0) "$($c.n) host hook installed after real vinput initialization"
      $chainBefore=Command $p.Id @('status');$hostBefore=$host;$chainReady=$false;$until=[Environment]::TickCount64+3500
      do{Start-Sleep -Milliseconds 100;$host=Read-HostMetrics $c;$s=Command $p.Id @('status');if($host.returned-gt$hostBefore.returned-and$s.counters.present_ours-gt$chainBefore.counters.present_ours){$chainReady=$true;break}}while([Environment]::TickCount64-lt$until)
      Require ($chainReady-and-not$s.really_foreground) "$($c.n) host return and vinput frame counters advance together before input"
      $c.install_witness=@{host_before=$hostBefore;host_after=$host;status_before=$chainBefore;status_after=$s}
      [void](Copy-HostReceipts $c 'installed')
    }
  }
  foreach ($c in $cases) {
    $activeCase=$c.n
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
      $pf="$($c.module_path).presents.txt";$host=Copy-HostReceipts $c 'complete'
      Require ((Test-Path -LiteralPath $pf)-and[int](Read-ProfileText $pf)-gt0-and$host.installed-and$host.entered-gt0-and$host.returned-gt0-and$s.counters.present_ours-gt0) "$($c.n) host and vinput Present hooks chain"
    }
  }
  # The four-profile isolation/chain checks are complete; release their render/poll
  # load before running independent suites, which each create their own targets.
  foreach ($p in $started.Values) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
  $started.Clear()
  Note 'PASS all four input/capture profiles stayed in the background'
  $finished=$true
} catch {
  $failedCase=$cases|Where-Object n -eq $activeCase|Select-Object -First 1
  if($failedCase.pid){try{Save-ProfileFailure $failedCase $_.Exception.Message}catch{Note "Failure receipt capture error: $($_.Exception.Message)"}}
  throw
} finally {
  foreach($c in $cases){if($c.pid-and$c.dll){[void](Copy-HostReceipts $c 'final')}}
  foreach ($ownedTarget in $started.Values) { if (-not $ownedTarget.HasExited) { Stop-Process -Id $ownedTarget.Id -Force -Confirm:$false } }
  foreach($process in $ownedCli){try{if(-not$process.HasExited){$process.Kill();[void]$process.WaitForExit(2000)}}catch{};$process.Dispose()}
  [pscustomobject]@{ok=$finished;cases=$cases;calls=$records;failures=$failures}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath "$Out\result.json"
  Note 'Owned profile targets stopped by PID'
}
$global:LASTEXITCODE=0
