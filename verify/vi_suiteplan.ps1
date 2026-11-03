# Shared inventory; dot-sourcing declares functions without running a test.
function New-VinputSuiteCase {
 param([string]$Id,[string]$Script,[hashtable]$Arguments=@{},[string]$Reason='',[bool]$PhysicalInput=$false)
 [pscustomobject]@{id=$Id;script=$Script;arguments=$Arguments;reason=$Reason;physical_input=$PhysicalInput;
  origin=$(if($PhysicalInput){'explicit'}else{'standard'})}
}
function Get-VinputForegroundCases {
 param([switch]$IncludePhysicalInput)
 New-VinputSuiteCase 'fgguard' 'run_fgguardtest.ps1' @{} 'External activation and hand-back need an independently witnessed real foreground destination.'
 New-VinputSuiteCase 'focusrobust' 'run_focusrobusttest.ps1' @{} 'Startup exposure, external activation, leases and hosted roots require real foreground ownership.'
 New-VinputSuiteCase 'hosthistory' 'run_hosthistorytest.ps1' @{} 'Injected hosted-window history needs a held real foreground sentinel.'
 New-VinputSuiteCase 'hostlaunch' 'run_hosthistorytest.ps1' @{NoInjectLaunch=$true} 'Uninjected launch history needs a held real foreground sentinel.'
 New-VinputSuiteCase 'windowfocus_sync' 'run_windowfocustest.ps1' @{CreateWindowAttack=$true} 'Synchronous activation attack includes an unprotected real-foreground positive control.'
 New-VinputSuiteCase 'windowfocus_worker_w' 'run_windowfocustest.ps1' @{WorkerCreateAttack=$true} 'Wide worker-window attack includes a real-foreground positive control.'
 New-VinputSuiteCase 'windowfocus_worker_a' 'run_windowfocustest.ps1' @{WorkerCreateAttack=$true;AnsiCreateAttack=$true} 'ANSI worker-window attack includes a real-foreground positive control.'
 New-VinputSuiteCase 'windowfocus_chain' 'run_windowfocustest.ps1' @{ReplaceChain=$true} 'Subclass replacement cancellation includes a real-foreground positive control.'
 New-VinputSuiteCase 'launch_restore' 'run_foregroundtest.ps1' @{} 'Launch foreground restoration is asserted against the real foreground; run it only on an idle machine.'
 if ($IncludePhysicalInput) {
  New-VinputSuiteCase 'human_runtime' 'run_userintenttest.ps1' @{} 'Explicit real mouse/keyboard takeover and unrelated-input controls.' $true
  New-VinputSuiteCase 'human_launch' 'run_userintenttest.ps1' @{LaunchControl=$true} 'Explicit real click into an owned launch target.' $true
  New-VinputSuiteCase 'human_alt_tab' 'run_userintenttest.ps1' @{AltTabOnly=$true} 'Explicit real Alt+Tab selection of owned targets.' $true
 }
}
function Get-VinputBackgroundCases {
 New-VinputSuiteCase 'userintentstate' 'run_userintentstate.ps1'
 New-VinputSuiteCase 'profiles' 'run_profiletest.ps1'
 New-VinputSuiteCase 'hover_load' 'run_hoverloadtest.ps1'
 New-VinputSuiteCase 'hostselect' 'run_hostselecttest.ps1'
 New-VinputSuiteCase 'restorelatency' 'run_restorelatencytest.ps1'
 New-VinputSuiteCase 'sentinelcleanup' 'run_sentinelcleanuptest.ps1'
 New-VinputSuiteCase 'sentinelstyle' 'run_sentinelstyletest.ps1'
 New-VinputSuiteCase 'click' 'run_clicktest.ps1'
 New-VinputSuiteCase 'activate' 'run_activatetest.ps1'
 New-VinputSuiteCase 'keys' 'run_keytest.ps1'
 New-VinputSuiteCase 'trace' 'run_tracetest.ps1'
 New-VinputSuiteCase 'capture' 'run_capturetest.ps1'
 New-VinputSuiteCase 'launch' 'run_launchtest.ps1' @{} 'Nonactivating synthetic launch, CLI output and owned-lock refusal; asserts the real foreground never changes.'
 New-VinputSuiteCase 'move' 'run_movetest.ps1'
 New-VinputSuiteCase 'edge_guard' 'run_edgeguardtest.ps1'
 New-VinputSuiteCase 'nativecapture' 'run_nativecapture.ps1'
 New-VinputSuiteCase 'capture_timing' 'run_capturetimingtest.ps1'
 New-VinputSuiteCase 'xinput' 'run_xinputtest.ps1'
 New-VinputSuiteCase 'unit' 'run_unittests.ps1'
 New-VinputSuiteCase 'ocr' 'run_ocrtest.ps1'
 New-VinputSuiteCase 'text' 'run_texttest.ps1'
 New-VinputSuiteCase 'ocr_robust' 'run_ocrrobusttest.ps1'
 New-VinputSuiteCase 'keyconfirm' 'run_keyconfirmtest.ps1'
 New-VinputSuiteCase 'keytransport' 'run_keytransporttest.ps1'
 New-VinputSuiteCase 'image' 'run_imagetest.ps1'
 New-VinputSuiteCase 'small' 'run_smalltest.ps1'
 New-VinputSuiteCase 'inputrobust' 'run_inputrobusttest.ps1'
 New-VinputSuiteCase 'overlay' 'run_overlaytest.ps1'
 New-VinputSuiteCase 'recenterguard' 'run_recenterguardtest.ps1'
 New-VinputSuiteCase 'clickpoint' 'run_clickpointtest.ps1'
 New-VinputSuiteCase 'lease_invariants' 'run_locktest.ps1'
 New-VinputSuiteCase 'legacylock' 'run_legacylocktest.ps1' @{CliCheck=$true}
 New-VinputSuiteCase 'pipe_security' 'run_pipesecuritytest.ps1'
 New-VinputSuiteCase 'doctor' 'run_doctortest.ps1'
 New-VinputSuiteCase 'doctor_hints' 'run_doctorhinttest.ps1'
 New-VinputSuiteCase 'script' 'run_scripttest.ps1'
 New-VinputSuiteCase 'json' 'run_jsontest.ps1'
 New-VinputSuiteCase 'dialog' 'run_dialogtest.ps1'
 New-VinputSuiteCase 'early' 'run_earlytest.ps1'
 New-VinputSuiteCase 'lock_wait' 'run_lockwaittest.ps1'
 New-VinputSuiteCase 'key_aliases' 'run_keyaliastest.ps1'
 New-VinputSuiteCase 'inject_exit' 'run_injectexittest.ps1'
 New-VinputSuiteCase 'legacy_di' 'run_dinput1test.ps1'
 New-VinputSuiteCase 'legacy_focus' 'run_dinputlegacyfocustest.ps1'
 New-VinputSuiteCase 'autoactivate' 'run_autoactivatetest.ps1'
 New-VinputSuiteCase 'di_pollclick' 'run_dipollclicktest.ps1'
 New-VinputSuiteCase 'di_release' 'run_direleasepolltest.ps1'
 New-VinputSuiteCase 'placement' 'run_placementtest.ps1'
 New-VinputSuiteCase 'fullscreen' 'run_fullscreentest.ps1'
 New-VinputSuiteCase 'ddraw_windowed' 'run_ddrawwindowedtest.ps1'
 New-VinputSuiteCase 'd3d9_windowed' 'run_d3d9windowedtest.ps1'
 New-VinputSuiteCase 'overlay_capture' 'run_overlaycapturetest.ps1'
 New-VinputSuiteCase 'vk_create_boundary' 'run_vkcreateboundarytest.ps1'
 New-VinputSuiteCase 'resize_cycle_contract' 'run_resizecyclecontracttest.ps1'
 New-VinputSuiteCase 'capture_output' 'run_captureoutputtest.ps1'
 New-VinputSuiteCase 'image_encode' 'run_imageencodetest.ps1'
 New-VinputSuiteCase 'raw_registration' 'run_rawregistrationtest.ps1'
 New-VinputSuiteCase 'relative_accounting' 'run_relativeaccounttest.ps1'
 New-VinputSuiteCase 'log_window' 'run_logwindowtest.ps1'
 New-VinputSuiteCase 'output_reuse' 'run_outputreusecontracttest.ps1'
 New-VinputSuiteCase 'steering_semantics' 'run_steeringsemanticstest.ps1'
 New-VinputSuiteCase 'input_jobs' 'run_inputjobstest.ps1'
 New-VinputSuiteCase 'hwnd_ownership' 'run_hwndownershiptest.ps1'
 New-VinputSuiteCase 'mouse_up_cleanup' 'run_capturemapcleanuptest.ps1'
 New-VinputSuiteCase 'multi' 'run_multitest.ps1'
 New-VinputSuiteCase 'anticheat_refusal' 'run_actest.ps1'
}
function Get-VinputSuitePlan {
 param([ValidateSet('background','foreground')][string]$Suite,[switch]$IncludePhysicalInput)
 $cases=@();$skipped=@()
 if ($Suite -eq 'background') {
  $cases=@(Get-VinputBackgroundCases)
  $skipped=@(Get-VinputForegroundCases | ForEach-Object {
   [pscustomobject]@{id=$_.id;script=$_.script;arguments=$_.arguments;reason=$_.reason;
    status='skipped';moved_to='run_foreground_suite.ps1';physical_input=$false}
  })
 } else {
  $cases=@(Get-VinputForegroundCases -IncludePhysicalInput:$IncludePhysicalInput)
  # This explicit regression was never part of the default gate, so it is not
  # counted among the moved/skipped default groups.
  $cases+=New-VinputSuiteCase 'hosted_noactivate' 'run_hostednoactivatetest.ps1' @{} 'Permanent NOACTIVATE restoration destination needs a real held foreground positive control.'
 }
 [pscustomobject]@{schema=1;suite=$Suite;requires_idle=($Suite -eq 'foreground');
  physical_input=[bool]$IncludePhysicalInput;cases=$cases;skipped_cases=$skipped;excluded_count=$skipped.Count}
}
function Invoke-VinputSuitePlan {
 param([Parameter(Mandatory)]$Plan,[Parameter(Mandatory)][string]$Out,[Parameter(Mandatory)][string]$ScriptRoot)
 New-Item -ItemType Directory -Force -Path $Out | Out-Null
 $Out=(Resolve-Path -LiteralPath $Out).Path
 $console=Join-Path $Out 'selftest_log.txt';Set-Content -LiteralPath $console -Value ''
 $invocation=[guid]::NewGuid().ToString('N')
 $caseResults=@($Plan.cases | ForEach-Object {
  [pscustomobject]@{id=$_.id;script=$_.script;status='not_run';exit_code=$null;
   started_utc=$null;finished_utc=$null;elapsed_ms=$null;log_path=$null;out_dir=$null;error=$null}
 })
 $result=[ordered]@{schema=1;suite=$Plan.suite;invocation_id=$invocation;status='running';started_utc=[DateTime]::UtcNow.ToString('o');
  finished_utc=$null;requires_idle=$Plan.requires_idle;physical_input=$Plan.physical_input;
  passed_suite_count=0;failed_suite_count=0;not_measured_suite_count=0;not_run_count=$caseResults.Count;
  skipped_cases=@($Plan.skipped_cases);excluded_count=$Plan.excluded_count;cases=$caseResults;
  environment=@{NoDefaultCurrentDirectoryInExePath='1'}}
 $resultPath=Join-Path $Out 'suite_result.json';$shell=(Get-Command pwsh -ErrorAction Stop).Source
 $previousDirectorySearch=[Environment]::GetEnvironmentVariable('NoDefaultCurrentDirectoryInExePath','Process')
 $previousPath=[Environment]::GetEnvironmentVariable('PATH','Process')
 # VS's own VsDevCmd pushes into Installer then invokes vswhere by bare name.
 # Keep current-directory search disabled and give only that known tool an explicit PATH entry.
 $installer=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
 if(Test-Path -LiteralPath (Join-Path $installer 'vswhere.exe') -PathType Leaf){
  [Environment]::SetEnvironmentVariable('PATH',$installer+';'+$previousPath,'Process')
  $result.environment.vswhere_directory=$installer
 }
 [Environment]::SetEnvironmentVariable('NoDefaultCurrentDirectoryInExePath','1','Process')
 try {
  foreach ($excluded in $Plan.skipped_cases) {
   "SKIPPED: $($excluded.id) — moved to $($excluded.moved_to): $($excluded.reason)" | Tee-Object -FilePath $console -Append
  }
  for ($index=0;$index -lt $Plan.cases.Count;$index++) {
   $case=$Plan.cases[$index];$record=$caseResults[$index];$childPath=Join-Path $ScriptRoot $case.script
   $caseRoot=Join-Path $Out $case.id
   $caseOut=Join-Path $caseRoot ('run_'+$invocation);New-Item -ItemType Directory -Force -Path $caseOut | Out-Null
   $record.out_dir=$caseOut
   $record.log_path=Join-Path $caseOut 'suite_console.log'
   $childArguments=@('-NoProfile','-File',$childPath,'-Out',$caseOut)
   foreach ($key in @($case.arguments.Keys | Sort-Object)) {
    $value=$case.arguments[$key]
    if ($value -is [bool]) { if ($value) { $childArguments+='-'+$key } }
    else { $childArguments+='-'+$key;$childArguments+=[string]$value }
   }
   $record.status='running';$record.started_utc=[DateTime]::UtcNow.ToString('o')
   $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $resultPath
   $watch=[Diagnostics.Stopwatch]::StartNew()
   try {
    "RUN $($Plan.suite): $($case.id)" | Tee-Object -FilePath $console -Append
    if (-not (Test-Path -LiteralPath $childPath -PathType Leaf)) { throw "Missing suite script: $childPath" }
    # A fresh -File process isolates expected negative native exits, test-only
    # native type replacements and each child suite's cleanup scope.
    $global:LASTEXITCODE=0
    & $shell @childArguments 2>&1 | Tee-Object -FilePath $record.log_path | Tee-Object -FilePath $console -Append
    $childExit=$LASTEXITCODE;$record.exit_code=$childExit
    if($childExit-eq3-and$case.id-eq'legacy_focus'){
     $receipt=Get-Content -LiteralPath (Join-Path $caseOut 'result.json') -Raw|ConvertFrom-Json
     if($receipt.schema-eq1-and$receipt.status-eq'not_measured'-and$receipt.completed-and$receipt.failed_check_count-eq0-and$receipt.not_measured_check_count-gt0-and$receipt.positive_case_count-gt0){
      $record.status='not_measured';$record.error='Native pre-injection control contaminated by physical activity or unavailable witness; positive controls completed.';$result.not_measured_suite_count++
      "NOT MEASURED $($case.id): native control inconclusive; remaining suites continue" | Tee-Object -FilePath $console -Append
      continue
     }
    }
    if ($childExit -ne 0) { throw "Suite $($case.id) failed (exit $childExit); see $($record.log_path)" }
    $record.status='passed';$result.passed_suite_count++
   } catch {
    $record.status='failed';$record.error=$_.Exception.Message;$result.failed_suite_count++;throw
   } finally {
    $watch.Stop();$record.elapsed_ms=$watch.ElapsedMilliseconds;$record.finished_utc=[DateTime]::UtcNow.ToString('o')
   }
  }
  if($result.not_measured_suite_count){$result.status='not_measured';"NOT MEASURED $($Plan.suite) suite: $($result.passed_suite_count) passed, $($result.not_measured_suite_count) inconclusive, no failed suites" | Tee-Object -FilePath $console -Append}
  else{$result.status='passed';"PASS $($Plan.suite) suite: $($result.passed_suite_count) child suites passed; $($result.excluded_count) foreground suites SKIPPED (not counted as passes)" | Tee-Object -FilePath $console -Append}
 } catch { $result.status='failed';throw }
 finally {
  [Environment]::SetEnvironmentVariable('NoDefaultCurrentDirectoryInExePath',$previousDirectorySearch,'Process')
  [Environment]::SetEnvironmentVariable('PATH',$previousPath,'Process')
  $result.not_run_count=@($caseResults | Where-Object status -eq 'not_run').Count
  $result.finished_utc=[DateTime]::UtcNow.ToString('o')
  $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $resultPath
 }
 $global:LASTEXITCODE=if($result.not_measured_suite_count){3}else{0}
}
