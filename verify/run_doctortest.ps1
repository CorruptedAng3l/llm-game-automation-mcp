# Read-only diagnostics against nonactivating synthetic targets, both architectures. Baseline must fail.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\doctor")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
$results=[Collections.Generic.List[object]]::new()
if(-not ('ViDoctorNative' -as [type])) { Add-Type 'using System; using System.Runtime.InteropServices; public static class ViDoctorNative { [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid); [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint access,bool inherit,uint tid); [DllImport("kernel32.dll")] public static extern uint SuspendThread(IntPtr thread); [DllImport("kernel32.dll")] public static extern uint ResumeThread(IntPtr thread); [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle); }' }
function Require($ok,[string]$what) { if(-not $ok){throw "FAIL $what"}; "PASS $what" }
function One([string[]]$a) {
  $lines=@(& $V --json @a 2> "$Out\stderr.txt")
  if($lines.Count -ne 1){throw "expected one JSON line from $a; got $($lines.Count)"}
  return $lines[0] | ConvertFrom-Json
}
try {
  foreach($case in @(@{exe='vi_channelprobe.exe';bits=64;arch='x64'},@{exe='vi_channelprobe32.exe';bits=32;arch='x86'})) {
    $p=$null
    try {
      $p=Start-Process -FilePath "$D\$($case.exe)" -ArgumentList "`"$Out\$($case.exe).log`"" -WindowStyle Hidden -PassThru
      Start-Sleep -Milliseconds 200
      $notInjected=One @('doctor','--pid',"$($p.Id)")
      $results.Add(@{target=$case.exe;stage='not_injected';reply=$notInjected})
      Require ($notInjected.command -eq 'doctor' -and -not $notInjected.ok -and -not $notInjected.status_available) "$($case.arch) missing pipe gives structured doctor failure"
      Require ($notInjected.process_alive -and $notInjected.bits -eq $case.bits -and $notInjected.arch -eq $case.arch -and $notInjected.app_container -eq $false) "$($case.arch) process architecture and token readable without injection"
      Require ($null -eq $notInjected.log_path -and $null -eq $notInjected.capture_path -and $null -eq $notInjected.input_channels.GetAsyncKeyState.calls) "$($case.arch) unavailable state is null, not guessed"
      $i=One @('inject','--pid',"$($p.Id)"); Require $i.ok "$($case.arch) injection positive control"
      $failed=One @('--pid',"$($p.Id)",'capture',"$Out\must_not_exist_$($case.exe).png",'--method','native','--timeout-ms','50')
      Require (-not $failed.ok) "$($case.arch) native capture fails on GDI target before any successful capture"
      $failedDoctor=One @('doctor','--pid',"$($p.Id)")
      Require ($failedDoctor.ok -and $failedDoctor.capture_path_reported -eq 'none' -and ($null -eq $failedDoctor.capture_path -or $failedDoctor.capture_path -eq 'none')) "$($case.arch) failed capture does not imply a successful capture route"
      Require ($failedDoctor.capture_path_note -ne 'older target status does not identify its successful capture path') "$($case.arch) missing route is not attributed confidently to an older successful capture"
      $results.Add(@{target=$case.exe;stage='failed_capture_before_success';capture=$failed;reply=$failedDoctor})
      $claim=One @('--owner','doctor-owner','--pid',"$($p.Id)",'claim','--ttl','30'); Require $claim.ok "$($case.arch) owner claim"
      $down=One @('--owner','doctor-owner','--pid',"$($p.Id)",'down','insert'); Require $down.ok "$($case.arch) held INSERT positive control"
      Start-Sleep -Milliseconds 100
      $before=One @('--pid',"$($p.Id)",'status')
      $first=One @('doctor','--pid',"$($p.Id)",'--owner','another-agent','--key','insert')
      $after=One @('--pid',"$($p.Id)",'status')
      $results.Add(@{target=$case.exe;stage='injected';before=$before;reply=$first;after=$after})
      Require ($first.ok -and $first.status_available -and $first.bits -eq $case.bits -and $first.arch -eq $case.arch) "$($case.arch) one doctor JSON for live target"
      Require ($first.claim.active -and $first.owner -eq 'doctor-owner' -and $first.claim.owner -eq 'doctor-owner') "$($case.arch) foreign owner can inspect claim"
      Require ($after.claim.expires_in_ms -le $before.claim.expires_in_ms -and $after.claim.expires_in_ms -gt 0) "$($case.arch) doctor does not renew claim TTL"
      Require (($before.virtual_down -join ',') -eq ($after.virtual_down -join ',') -and $before.counters.posted -eq $after.counters.posted -and $before.counters.captures -eq $after.counters.captures) "$($case.arch) doctor changes no held keys, messages or captures"
      Require ($first.trace_key -eq 45 -and $first.key_trace.ok -and $first.key_trace.injected_down -eq 1 -and $first.key_trace.channels.GetAsyncKeyState.down_reads -gt 0) "$($case.arch) selected per-key trace reports actual INSERT reads"
      Require ($first.input_channels.GetAsyncKeyState.calls -gt 0 -and $first.input_channels.GetAsyncKeyState.observed -and 'GetAsyncKeyState' -in $first.observed_channels) "$($case.arch) observed input channels come from counters"
      Require ($first.input_channels.DirectInputKeyboardState.calls -gt 0 -and $first.input_channels.WindowKeyboardMessages.calls -gt 0) "$($case.arch) DirectInput and window message channels reported"
      Require (-not $first.foreground.really_foreground -and $first.foreground.blockfocus) "$($case.arch) real foreground and protection reported"
      Require ($first.log_path -and (Test-Path -LiteralPath $first.log_path) -and $first.log_path -match '\\run\\logs\\vinput_\d+\.log$') "$($case.arch) exact injected log path exists"
      Require ($first.suggestions.Count -gt 0 -and ($first.suggestions -join ' ') -match 'not.*successful game actions') "$($case.arch) suggestions state observation limits"
      $cli64=$V
      try {
        $V=(Resolve-Path "$CliDir\vinput32.exe").Path
        $cli32=One @('doctor','--pid',"$($p.Id)")
        Require ($cli32.ok -and $cli32.bits -eq $case.bits -and $cli32.arch -eq $case.arch -and $cli32.log_path -eq $first.log_path) "$($case.arch) diagnosed correctly by the x86 CLI too"
        $results.Add(@{target=$case.exe;stage='x86_cli';reply=$cli32})
      } finally {$V=$cli64}
      One @('--owner','doctor-owner','--pid',"$($p.Id)",'up','insert') | Out-Null
      $cap=One @('--owner','doctor-owner','--pid',"$($p.Id)",'capture',"$Out\$($case.exe).png",'--method','printwindow'); Require $cap.ok "$($case.arch) capture positive control"
      $captured=One @('--pid',"$($p.Id)",'doctor')
      $exactPath=$captured.capture_path -eq 'printwindow'
      $unknownLegacyPath=$null -eq $captured.capture_path -and $captured.capture_path_reported -eq 'none' -and $captured.capture_path_note
      Require (($exactPath -or $unknownLegacyPath) -and $captured.frame_api -eq 'none') "$($case.arch) capture path reported or explicitly unknown for legacy status"
      $results.Add(@{target=$case.exe;stage='captured';reply=$captured})
      One @('--owner','doctor-owner','--pid',"$($p.Id)",'unclaim') | Out-Null
      # Suspend only our synthetic UI thread: status's title read may block, but doctor must not.
      [uint32]$threadOwner=0
      $window=[IntPtr][long]::Parse($after.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
      $threadId=[ViDoctorNative]::GetWindowThreadProcessId($window,[ref]$threadOwner)
      Require ($threadOwner -eq $p.Id) "$($case.arch) bounded-response control owns UI thread"
      $thread=[ViDoctorNative]::OpenThread(2,$false,$threadId)
      Require ($thread -ne [IntPtr]::Zero) "$($case.arch) bounded-response control opens owned UI thread"
      $suspended=$false
      try {
        $suspended=[ViDoctorNative]::SuspendThread($thread) -ne [uint32]::MaxValue
        Require $suspended "$($case.arch) owned UI thread suspended"
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $blocked=One @('doctor','--pid',"$($p.Id)")
        $watch.Stop()
        Require ($watch.ElapsedMilliseconds -lt 4000 -and ($blocked.ok -or (-not $blocked.status_available -and $blocked.error -match 'timed out'))) "$($case.arch) suspended UI cannot make diagnostics wait indefinitely"
        $results.Add(@{target=$case.exe;stage='unresponsive';elapsed_ms=$watch.ElapsedMilliseconds;reply=$blocked})
      } finally {if($suspended){[ViDoctorNative]::ResumeThread($thread) | Out-Null}; [ViDoctorNative]::CloseHandle($thread) | Out-Null}
    } finally { if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false} }
  }
  $ac=$null
  try {
    $acOut="$Out\appcontainer"; New-Item -ItemType Directory -Force $acOut | Out-Null
    icacls $acOut /grant '*S-1-15-2-1:(OI)(CI)M' | Out-Null
    icacls "$D\vi_channelprobe.exe" /grant '*S-1-15-2-1:(RX)' | Out-Null
    $acStarted=& "$D\vi_aclaunch.exe" "$D\vi_channelprobe.exe" "$acOut\target.log" | ConvertFrom-Json
    Require $acStarted.ok 'AppContainer synthetic process started'
    $ac=Get-Process -Id $acStarted.pid
    $acBefore=One @('doctor','--pid',"$($ac.Id)")
    Require ($acBefore.app_container -and -not $acBefore.status_available) 'AppContainer token diagnosed before injection'
    $acInject=One @('inject','--pid',"$($ac.Id)"); Require $acInject.ok 'AppContainer injection positive control'
    $acDoctor=One @('doctor','--pid',"$($ac.Id)")
    Require ($acDoctor.ok -and $acDoctor.app_container -and $acDoctor.bits -eq 64 -and $acDoctor.log_path -and (Test-Path -LiteralPath $acDoctor.log_path)) 'AppContainer namespace pipe and exact log path diagnosed'
    Require (($acDoctor.suggestions -join ' ') -match 'CoreWindow|AppContainer keyboard') 'AppContainer suggestions describe keyboard limits'
    $results.Add(@{stage='appcontainer';before=$acBefore;reply=$acDoctor})
  } finally {if($ac -and -not $ac.HasExited){Stop-Process -Id $ac.Id -Force -Confirm:$false}}
  $dead=One @('doctor','--pid','4294967295')
  Require (-not $dead.ok -and $dead.command -eq 'doctor' -and -not $dead.process_alive -and -not $dead.status_available -and $null -eq $dead.bits -and $null -eq $dead.log_path) 'gone PID reports unknown fields without injection'
  $results.Add(@{stage='gone';reply=$dead})
  foreach($a in @(@('doctor'),@('doctor','--pid','0'),@('doctor','--pid','1','--key','w+space'),@('doctor','--pid','1','--key','vk256'),@('doctor','--pid','1','extra'),@('doctor','--pid','1','--bogus'))) {
    $bad=One $a; Require (-not $bad.ok -and $bad.error) "invalid doctor args rejected: $($a -join ' ')"
  }
  # The user-facing docs: README.md, docs\CLI.md (CLI reference) and mcp\README.md (agent quick start).
  $readme=(@("$PSScriptRoot\..\README.md","$PSScriptRoot\..\docs\CLI.md","$PSScriptRoot\..\mcp\README.md")|ForEach-Object{Get-Content -LiteralPath $_ -Raw}) -join "`n"
  Require ($readme.Contains('dist\run\logs\vinput_<pid>.log') -and -not $readme.Contains('Log: `dist\logs\vinput_<pid>.log`')) 'docs document injected log path'
  Require (-not $readme.Contains('Send `vinput --pid N activate` first.')) 'docs remove obsolete activation-before-click workaround'
  Require ($readme.Contains('## Agent quick start') -and $readme.Contains('doctor --pid N') -and $readme.Contains('target-side `SendInput`')) 'docs include agent workflow, diagnostics and target-side SendInput restriction'
  $global:LASTEXITCODE=0
  'PASS doctor: both architectures, missing/live/gone process, channels/trace/log/capture, read-only owner isolation'
} finally { $results | ConvertTo-Json -Depth 18 | Set-Content -LiteralPath "$Out\result.json" }
