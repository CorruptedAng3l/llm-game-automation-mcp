# Early (suspended) injection control: `launch --exe X --early` creates the process suspended, injects vinput,
# waits for its pipe and only then resumes the main thread, so vinput sees vkCreateDevice / vkGetDeviceQueue /
# vkCreateSwapchainKHR. The Vulkan target here does NOT wait for vinput (no --wait-vinput): it LoadLibrary's
# vulkan-1.dll and creates its device immediately, like a real game.
#   negative control (same build, no --early): native capture must refuse ("obtained before vinput was injected")
#   positive (--early): native capture must succeed with capture_path vk and the pattern must be correct.
# On e44e849 --early is ignored, so the positive case is RED there. No real foreground is used.
param([string]$CliDir = "$PSScriptRoot\..\dist", [string]$Out = "$PSScriptRoot\..\_scratch\earlytest")
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\dist").Path; $V = (Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path
. "$PSScriptRoot\vi_patterncheck.ps1"
. "$PSScriptRoot\vi_early_cleanup.ps1"
$results = @(); $owned = @();$cleanupResults=[Collections.Generic.List[object]]::new()
function Register-EarlyTarget($Receipt,[string]$Exe){
  [uint32]$targetPid=0;[long]$filetime=0
  if(-not[uint32]::TryParse([string]$Receipt.pid,[ref]$targetPid)-or-not$targetPid-or
     -not[long]::TryParse([string]$Receipt.process_created,[ref]$filetime)-or$filetime-le0-or$Receipt.spawned_pid-ne$targetPid){throw 'launch lacks exact spawned PID/creation; no cleanup identity inferred'}
  # Initial discovery is bound to the typed launch receipt, then retained once.
  # No PID/name lookup is used to authorize any later cleanup operation.
  $original=[Diagnostics.Process]::GetProcessById([int]$targetPid)
  $record=New-ViEarlyOwnedRecord $original (Join-Path $D $Exe) ([DateTime]::FromFileTimeUtc($filetime).Ticks)
  $script:owned+=,$record
  if(-not$record.startup_verified){throw ('retained launched process identity could not be proved: '+$record.startup_error)}
}
function Stop-OwnedEarly{
  $failures=@()
  foreach($record in $script:owned){try{$cleanup=Stop-ViEarlyOwnedRecord $record;$cleanupResults.Add(@{pid=$record.pid;path=$record.path;created=$record.created;retained_handle=$record.retained_handle;cleanup=$cleanup});if(-not$cleanup.completed){$failures+=$cleanup.error}}finally{$record.process.Dispose()}}
  $script:owned=@()
  if($failures.Count){throw ('bounded original-handle cleanup failed: '+($failures-join'; '))}
}
function Launch([string]$exe, [string]$log, [bool]$early) {
  $a = @('--json', 'launch', '--exe', "$D\$exe", '--args', "`"$log`" -28000 600 820 480 --fmt bgra", '--settle', '1', '--timeout', '15', '--fg-guard', '0')
  if ($early) { $a += '--early' }
  $raw = & $V @a 2> "$log.stderr.txt"
  $r = $raw | Where-Object { $_ -like '{*' } | Select-Object -Last 1 | ConvertFrom-Json
  if ($r.pid) { Register-EarlyTarget $r $exe }
  return $r
}
try {
  foreach ($case in @(
      @{n='vk_x64_late';  exe='vi_vktest.exe';   early=$false; expect='refuse'},
      @{n='vk_x64_early'; exe='vi_vktest.exe';   early=$true;  expect='vk'},
      @{n='vk_x86_late';  exe='vi_vktest32.exe'; early=$false; expect='refuse'},
      @{n='vk_x86_early'; exe='vi_vktest32.exe'; early=$true;  expect='vk'})) {
    $log = "$Out\$($case.n).log"; Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
    $l = Launch $case.exe $log $case.early
    $cap = $null; $st = $null
    if ($l.ok) {
      Start-Sleep -Milliseconds 500
      $cap = & $V --pid $l.pid capture "$Out\$($case.n).png" --method native | ConvertFrom-Json
      $st = & $V --pid $l.pid status | ConvertFrom-Json
    }
    $pat = $null
    if ($cap.ok) { $pat = Test-VinputPattern "$Out\$($case.n).png" }
    $pass = if ($case.expect -eq 'vk') { $l.ok -and $l.early_injected -and $cap.ok -and $cap.capture_path -eq 'vk' -and $st.vk.swapchains -ge 1 -and $pat.ok }
            else { $l.ok -and -not $l.early_injected -and -not $cap.ok -and $cap.error -match 'before vinput was injected' }
    $results += [pscustomobject]@{case=$case.n; pass=[bool]$pass; launch_ok=$l.ok; early_injected=$l.early_injected; pid=$l.pid
      capture_ok=$cap.ok; capture_path=$cap.capture_path; capture_error=$cap.error; vk_devices=$st.vk.devices; vk_swapchains=$st.vk.swapchains; frames_vk=$st.frames_by_api.vk
      pattern=$(if ($pat) { "ok=$($pat.ok) frame=$($pat.frame) bars=$($pat.bars_ok)" }); target_log_tail=(Get-Content -LiteralPath $log -Tail 3 -ErrorAction SilentlyContinue) -join ' | '}
    Stop-OwnedEarly
  }
  # D3D11 input target with --early: the ordinary input/capture paths still work when vinput is in before main()
  foreach ($exe in @('vi_testinput.exe', 'vi_testinput32.exe')) {
    $log = "$Out\early_$exe.log"; Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
    $raw = & $V --json launch --exe "$D\$exe" --args "`"$log`" -28000 600 420 260" --settle 1 --timeout 15 --fg-guard 0 --early 2> "$log.stderr.txt"
    $l = $raw | Where-Object { $_ -like '{*' } | Select-Object -Last 1 | ConvertFrom-Json
    if ($l.pid) { Register-EarlyTarget $l $exe }
    $k = if ($l.ok) { & $V --pid $l.pid key q 120 | ConvertFrom-Json }
    $cap = if ($l.ok) { & $V --pid $l.pid capture "$Out\early_$exe.png" --method native | ConvertFrom-Json }
    Start-Sleep -Milliseconds 300
    $t = Get-Content -Raw -LiteralPath $log -ErrorAction SilentlyContinue
    $pass = $l.ok -and $l.early_injected -and $k.ok -and $t -match 'WM_KEYDOWN vk=0x51' -and $t -match 'GAKS vk=0x51 down' -and $cap.ok -and $cap.capture_path -eq 'd3d11'
    $results += [pscustomobject]@{case="d3d11_input_early_$exe"; pass=[bool]$pass; launch_ok=$l.ok; early_injected=$l.early_injected; pid=$l.pid; capture_ok=$cap.ok; capture_path=$cap.capture_path; capture_error=$cap.error}
    Stop-OwnedEarly
  }
} finally {try{Stop-OwnedEarly}finally{$cleanupResults|ConvertTo-Json -Depth 12|Set-Content -LiteralPath "$Out\cleanup.json"}}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$Out\result.json"
foreach ($r in $results) { "$(if ($r.pass) {'PASS'} else {'FAIL'}) $($r.case): early=$($r.early_injected) capture_ok=$($r.capture_ok) path=$($r.capture_path) swapchains=$($r.vk_swapchains) err=$($r.capture_error)" }
if (@($results | Where-Object { -not $_.pass }).Count) { throw "FAIL early injection; see $Out\result.json" }
'PASS early (suspended) injection flips Vulkan native capture from refused to working (x64 + x86)'
