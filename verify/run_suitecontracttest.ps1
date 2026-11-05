# Runner contract controls using copied runners and inert PowerShell case stubs only.
# Never starts a target executable, invokes a foreground API, or sends OS input.
param([string]$SourceDir=$PSScriptRoot,[string]$Out="$PSScriptRoot\_out\suitecontract",[string]$BaselineRef='1b1f2a2',[switch]$WatchdogOnly)
$ErrorActionPreference='Stop'
$SourceDir=(Resolve-Path $SourceDir).Path
$repo=Split-Path $SourceDir -Parent
New-Item -ItemType Directory -Force $Out|Out-Null
$Out=(Resolve-Path $Out).Path
$work=Join-Path $Out ('shadow with space '+[guid]::NewGuid().ToString('N'))
$shadow=Join-Path $work 'verify'
New-Item -ItemType Directory -Force $shadow,(Join-Path $work 'dist')|Out-Null
[IO.File]::WriteAllText((Join-Path $work 'dist\vinput.exe'),'INERT PLACEHOLDER: must never execute')
$trace=Join-Path $work 'case_trace.txt'
$checks=[Collections.Generic.List[object]]::new()
$evidence=[Collections.Generic.List[object]]::new()
$declaredScripts=@{}
# Only this inert contract fixture uses this containment. A gate prevents the
# runner from starting descendants until its original process handle is in a
# fresh non-breakaway, kill-on-close job. No PID/name-based process killing.
if(-not('ViSuiteContractJobV1'-as[type])){Add-Type @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public sealed class ViSuiteContractJobV1 : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct BasicLimits { public long PerProcess,PerJob;public uint Flags;public UIntPtr Min,Max;public uint Active;public UIntPtr Affinity;public uint Priority,Scheduling; }
 [StructLayout(LayoutKind.Sequential)] struct Io {public ulong A,B,C,D,E,F;}
 [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {public BasicLimits Basic;public Io Io;public UIntPtr ProcessMemory,JobMemory,PeakProcess,PeakJob;}
 [StructLayout(LayoutKind.Sequential)] struct Accounting {public long User,Kernel,PeriodUser,PeriodKernel;public uint Faults,Total,Active,Terminated;}
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateJobObjectW(IntPtr attributes,IntPtr name);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int kind,ref ExtendedLimits limits,uint bytes);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int kind,out Accounting info,uint bytes,IntPtr returned);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool TerminateJobObject(IntPtr job,uint code);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint GetProcessId(IntPtr process);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr process,out long created,out long exited,out long kernel,out long user);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageNameW(IntPtr process,uint flags,StringBuilder path,ref uint count);
 IntPtr job;
 public uint RootPid {get;private set;} public long CreatedFiletime {get;private set;} public string Image {get;private set;}
 public ViSuiteContractJobV1(){job=CreateJobObjectW(IntPtr.Zero,IntPtr.Zero);if(job==IntPtr.Zero)throw new Win32Exception();var l=new ExtendedLimits();l.Basic.Flags=0x2000;if(!SetInformationJobObject(job,9,ref l,(uint)Marshal.SizeOf<ExtendedLimits>())){int e=Marshal.GetLastWin32Error();Dispose();throw new Win32Exception(e);}}
 public void Assign(Process p,string expected){IntPtr h=p.Handle;uint pid=GetProcessId(h);long c,e,k,u;var b=new StringBuilder(32768);uint n=(uint)b.Capacity;if(pid!=(uint)p.Id||!GetProcessTimes(h,out c,out e,out k,out u)||c<=0||!QueryFullProcessImageNameW(h,0,b,ref n)||!String.Equals(b.ToString(),expected,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("original process identity mismatch");RootPid=pid;CreatedFiletime=c;Image=b.ToString();if(!AssignProcessToJobObject(job,h))throw new Win32Exception();}
 public uint[] Counts(){Accounting a;if(!QueryInformationJobObject(job,1,out a,(uint)Marshal.SizeOf<Accounting>(),IntPtr.Zero))throw new Win32Exception();return new uint[]{a.Total,a.Active,a.Terminated};}
 public void Terminate(){if(!TerminateJobObject(job,124))throw new Win32Exception();}
 public void Dispose(){if(job!=IntPtr.Zero){CloseHandle(job);job=IntPtr.Zero;}}
}
'@}
$gateScript=Join-Path $work 'owned_runner_gate.ps1'
[IO.File]::WriteAllText($gateScript,@'
param([string]$GateName,[string]$Runner,[string]$ArgumentFile)
$ErrorActionPreference='Stop'
$gate=[Threading.EventWaitHandle]::OpenExisting($GateName)
try{if(-not$gate.WaitOne(10000)){throw 'owned runner gate was not released'}}finally{$gate.Dispose()}
$runnerArguments=@(Get-Content -LiteralPath $ArgumentFile -Raw|ConvertFrom-Json)
# Preserve the original native -File argument binding and process-exit semantics;
# PowerShell array-splatting strings into a script would bind '-Out' positionally.
& (Get-Command pwsh).Source -NoProfile -File $Runner @runnerArguments
exit $LASTEXITCODE
'@)
function Check($value,[string]$name){$checks.Add([pscustomobject]@{pass=[bool]$value;name=$name});Write-Host "$(if($value){'PASS'}else{'FAIL'}) $name";if(-not$value){throw $name}}
function Read-Trace {if(Test-Path $trace){return ,@(Get-Content $trace)};return ,@()}
function Reset-Trace {[IO.File]::WriteAllText($trace,'');[IO.File]::WriteAllText($trace+'.args','')}
function Get-RunnerBudget([int]$count){[int][Math]::Min(300000,[Math]::Max(60000,15000+1500*$count))}
function Get-RunnerStop([long]$elapsed,[long]$idle,[long]$total,[long]$noProgress){
 if($elapsed-ge$total){return 'total_timeout'};if($idle-ge$noProgress){return 'no_progress_timeout'};return $null
}
function Get-TraceProgress([string[]]$expected){
 $rows=@();$argsRows=@()
 foreach($file in @($trace,$trace+'.args')){
  if(-not[IO.File]::Exists($file)){continue}
  if((Get-Item -LiteralPath $file).Length-gt4194304){throw 'inert trace exceeds fixture byte cap'}
 }
 try{
  if([IO.File]::Exists($trace)){$raw=[IO.File]::ReadAllText($trace);$rows=@([regex]::Matches($raw,'[^\r\n]*\r?\n')|ForEach-Object{$_.Value.TrimEnd("`r","`n")})}
  if([IO.File]::Exists($trace+'.args')){$raw=[IO.File]::ReadAllText($trace+'.args');$argsRows=@([regex]::Matches($raw,'[^\r\n]*\r?\n')|ForEach-Object{$_.Value.TrimEnd("`r","`n")})}
 }catch [IO.IOException]{return -1} # append sharing/partial writes never count as progress
 $count=[Math]::Min($rows.Count,$argsRows.Count)
 if($count-gt$expected.Count){throw 'inert trace exceeds declared plan prefix'}
 for($i=0;$i-lt$count;$i++){
  $call=$argsRows[$i]|ConvertFrom-Json -ErrorAction Stop
  if($rows[$i]-cne$expected[$i]-or$call.script-cne$expected[$i]){throw 'inert trace is not the declared ordered prefix'}
 }
 return $count
}
function Invoke-Runner([string]$script,[string[]]$arguments,[string]$label,[hashtable]$control=@{}){
 $expected=@();$key=[IO.Path]::GetFileName($script)+'|physical='+[bool]($arguments-contains'-IncludePhysicalInput')
 if($declaredScripts.ContainsKey($key)-and$arguments-notcontains'-PlanOnly'){$expected=@($declaredScripts[$key])}
 if($control.ContainsKey('expected')){$expected=@($control.expected)}
 $total=Get-RunnerBudget $expected.Count;$noProgress=15000
 if($control.ContainsKey('total')){$total=[Math]::Min($total,[int]$control.total)}
 if($control.ContainsKey('idle')){$noProgress=[Math]::Min($noProgress,[int]$control.idle)}
 $gateName='Local\vinput_suitecontract_'+[guid]::NewGuid().ToString('N')
 $gate=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$gateName)
 $argumentFile=Join-Path $work ($label+'.arguments.json');ConvertTo-Json -InputObject @($arguments)|Set-Content -LiteralPath $argumentFile
 $job=[ViSuiteContractJobV1]::new();$process=$null;$stdout=$null;$stderr=$null;$result=$null;$failure=$null
 $watch=[Diagnostics.Stopwatch]::StartNew();$lastProgress=0L;$progress=0;$maxJobProcesses=0;$cleanupError=$null;$observerError=$null
 try{
 $si=[Diagnostics.ProcessStartInfo]::new((Get-Command pwsh).Source)
 $si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
 $si.Environment['VINPUT_SUITE_CONTRACT_TRACE']=$trace
 foreach($argument in @('-NoProfile','-File',$gateScript,'-GateName',$gateName,'-Runner',$script,'-ArgumentFile',$argumentFile)){[void]$si.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$si
 [void]$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
 $job.Assign($process,$si.FileName);[void]$gate.Set();$watch.Restart()
 while(-not$process.WaitForExit(100)){
  $counts=$job.Counts();$maxJobProcesses=[Math]::Max($maxJobProcesses,$counts[0])
  $next=Get-TraceProgress $expected
  if($next-gt$progress){$progress=$next;$lastProgress=$watch.ElapsedMilliseconds}
  $failure=Get-RunnerStop $watch.ElapsedMilliseconds ($watch.ElapsedMilliseconds-$lastProgress) $total $noProgress
  if($failure){break}
 }
 }catch{$failure='observer_error';$observerError=$_.Exception.Message}
 finally{
  # Always contain leftover children, even when a normally exited root left one.
  try{
   $counts=$job.Counts();$maxJobProcesses=[Math]::Max($maxJobProcesses,$counts[0])
   if($counts[1]-gt0){$job.Terminate()}
   $cleanup=[Diagnostics.Stopwatch]::StartNew()
   do{$counts=$job.Counts();if($counts[1]-eq0){break};Start-Sleep -Milliseconds 25}while($cleanup.ElapsedMilliseconds-lt5000)
   if($counts[1]-ne0){throw 'owned Job Object still has active processes'}
   if($process-and-not$process.WaitForExit(2000)){throw 'original runner handle did not signal'}
  }catch{$cleanupError=(@($cleanupError,$_.Exception.Message)|Where-Object{$_})-join'; '}
  # Assignment can fail before containment. The gate is never released then;
  # kill only the retained original Process object, not a reopened PID.
  if($process-and-not$process.HasExited){try{$process.Kill();if(-not$process.WaitForExit(2000)){throw 'unassigned gated root did not exit'}}catch{$cleanupError=(@($cleanupError,$_.Exception.Message)|Where-Object{$_})-join'; '}}
  $drainOk=$true
  foreach($task in @($stdout,$stderr)){if($task-and-not$task.Wait(3000)){$drainOk=$false}}
  $result=[pscustomobject]@{label=$label;exit=$(if($process-and$process.HasExited){$process.ExitCode}else{$null});stdout=$(if($stdout-and$stdout.IsCompletedSuccessfully){$stdout.Result}else{''});stderr=$(if($stderr-and$stderr.IsCompletedSuccessfully){$stderr.Result}else{''});stop=$failure;elapsed_ms=$watch.ElapsedMilliseconds;total_budget_ms=$total;no_progress_budget_ms=$noProgress;valid_prefix_count=$progress;declared_case_count=$expected.Count;identity=@{pid=$job.RootPid;created_filetime=$job.CreatedFiletime;image=$job.Image};owned_job_total=$maxJobProcesses;owned_job_active_final=$(if($counts){$counts[1]}else{$null});drains_completed=$drainOk;observer_error=$observerError;cleanup_error=$cleanupError}
 [IO.File]::WriteAllText((Join-Path $Out "$label.stdout.txt"),$result.stdout)
 [IO.File]::WriteAllText((Join-Path $Out "$label.stderr.txt"),$result.stderr)
 $evidence.Add($result);$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out "$label.runner.json")
 $job.Dispose();$gate.Dispose();if($process){$process.Dispose()}
 }
 if($cleanupError-or-not$result.drains_completed){throw "$label inert runner cleanup/drain failed: $cleanupError"}
 if($failure-and-not$control.allowTimeout){throw "$label inert runner $failure after $($result.elapsed_ms)ms (valid prefix $progress/$($expected.Count)): $observerError; evidence saved"}
 return $result
}
function New-Stub([string]$name,[string]$mode='success'){
 $body=@'
param([string]$Out)
$ErrorActionPreference='Stop'
Add-Content -LiteralPath $env:VINPUT_SUITE_CONTRACT_TRACE -Value ([IO.Path]::GetFileName($PSCommandPath))
[pscustomobject]@{script=[IO.Path]::GetFileName($PSCommandPath);out=$Out;arguments=@($args)}|ConvertTo-Json -Compress|Add-Content -LiteralPath ($env:VINPUT_SUITE_CONTRACT_TRACE+'.args')
'@
 $tail=switch($mode){
  'throw' {"throw 'CONTRACT intentional child exception'"}
  'nonzero' {"Write-Output 'PASS misleading text is not an exit-code result'; exit 7"}
  'exit3' {"Write-Output 'NOT MEASURED without a valid receipt';exit 3"}
  'not_measured' {'@{schema=1;status="not_measured";completed=$true;failed_check_count=0;not_measured_check_count=1;positive_case_count=1}|ConvertTo-Json|Set-Content (Join-Path $Out "result.json");Write-Output "NOT MEASURED native control; positive control passed";exit 3'}
  'capture_not_measured' {'$receipt=@{schema=1;status="not_measured";completed=$true;failed_check_count=0;not_measured_check_count=1;positive_case_count=1;complete_candidate_positive_case_count=0;not_measured_reason="capture_enumeration_incomplete"};$inner=Join-Path $Out ("run_"+[guid]::NewGuid().ToString("N"));New-Item -ItemType Directory -Path $inner -ErrorAction Stop|Out-Null;$receipt|ConvertTo-Json|Set-Content (Join-Path $inner "result.json");exit 3'}
  'capture_no_positive' {'$receipt=@{schema=1;status="not_measured";completed=$true;failed_check_count=0;not_measured_check_count=1;positive_case_count=0;complete_candidate_positive_case_count=0;not_measured_reason="capture_enumeration_incomplete"};$inner=Join-Path $Out ("run_"+[guid]::NewGuid().ToString("N"));New-Item -ItemType Directory -Path $inner -ErrorAction Stop|Out-Null;$receipt|ConvertTo-Json|Set-Content (Join-Path $inner "result.json");exit 3'}
  'capture_string_completed' {'$receipt=@{schema=1;status="not_measured";completed="false";failed_check_count=0;not_measured_check_count=1;positive_case_count=1;complete_candidate_positive_case_count=0;not_measured_reason="capture_enumeration_incomplete"};$inner=Join-Path $Out ("run_"+[guid]::NewGuid().ToString("N"));New-Item -ItemType Directory -Path $inner -ErrorAction Stop|Out-Null;$receipt|ConvertTo-Json|Set-Content (Join-Path $inner "result.json");exit 3'}
  'capture_flat_only' {'$receipt=@{schema=1;status="not_measured";completed=$true;failed_check_count=0;not_measured_check_count=1;positive_case_count=1;complete_candidate_positive_case_count=0;not_measured_reason="capture_enumeration_incomplete"};$receipt|ConvertTo-Json|Set-Content (Join-Path $Out "result.json");exit 3'}
  'capture_multiple_inner' {'$receipt=@{schema=1;status="not_measured";completed=$true;failed_check_count=0;not_measured_check_count=1;positive_case_count=1;complete_candidate_positive_case_count=0;not_measured_reason="capture_enumeration_incomplete"};foreach($n in 1..2){$inner=Join-Path $Out ("run_"+[guid]::NewGuid().ToString("N"));New-Item -ItemType Directory -Path $inner -ErrorAction Stop|Out-Null;$receipt|ConvertTo-Json|Set-Content (Join-Path $inner "result.json");}exit 3'}
  'capture_wrong_reason' {'$receipt=@{schema=1;status="not_measured";completed=$true;failed_check_count=0;not_measured_check_count=1;positive_case_count=1;complete_candidate_positive_case_count=0;not_measured_reason="unrelated"};$inner=Join-Path $Out ("run_"+[guid]::NewGuid().ToString("N"));New-Item -ItemType Directory -Path $inner -ErrorAction Stop|Out-Null;$receipt|ConvertTo-Json|Set-Content (Join-Path $inner "result.json");exit 3'}
  'nm_string_completed' {'@{schema=1;status="not_measured";completed="false";failed_check_count=0;not_measured_check_count=1;positive_case_count=1}|ConvertTo-Json|Set-Content (Join-Path $Out "result.json");exit 3'}
  'reported_failure' {"Write-Output 'FAIL intentional assertion result'; exit 1"}
  'stale' {"`$global:LASTEXITCODE=29;Write-Output 'PASS expected-negative child handled its own result';exit 0"}
  default {"Write-Output 'PASS inert child';exit 0"}
 }
 [IO.File]::WriteAllText((Join-Path $shadow $name),$body+"`r`n"+$tail+"`r`n")
}
function Plan([string]$runner,[string[]]$extra,[string]$label){
 Reset-Trace
 $run=Invoke-Runner (Join-Path $shadow $runner) (@('-PlanOnly','-Out',(Join-Path $work $label))+$extra) $label
 Check ($run.exit-eq0) "$label exits zero"
 $plan=$run.stdout|ConvertFrom-Json
 Check ($null-ne$plan-and$plan-isnot[array]) "$label emits exactly one JSON plan object"
 Check ($plan.schema-eq1-and$null-ne$plan.cases-and$null-ne$plan.skipped_cases) "$label exposes the plan schema"
 Check ((Read-Trace).Count-eq0) "$label invokes no case, including foreground traps"
 $declaredScripts[$runner+'|physical='+[bool]($extra-contains'-IncludePhysicalInput')]=@($plan.cases|ForEach-Object{[IO.Path]::GetFileName($_.script)})
 return $plan
}
function Case-Arguments($case){
 $parts=@()
 foreach($property in @($case.arguments.PSObject.Properties|Sort-Object Name)){
  if($property.Value-is[bool]){if($property.Value){$parts+='-'+$property.Name}}
  else{$parts+='-'+$property.Name;$parts+=[string]$property.Value}
 }
 return ,$parts
}
function Case-Key($case){([IO.Path]::GetFileName($case.script))+'|'+((Case-Arguments $case)-join' ')}
function Validate-Result($run,[string]$folder,[string]$status,[int]$passed,[int]$failed,[int]$notRun){
 $file=Join-Path $folder 'suite_result.json'
 Check (Test-Path $file) "$($run.label) writes suite_result.json"
 $result=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json
 Check ($result.status-eq$status-and$result.passed_suite_count-eq$passed-and$result.failed_suite_count-eq$failed-and$result.not_run_count-eq$notRun) "$($run.label) reports actual passed/failed/not-run counts"
 $caseStatuses=@($result.cases|ForEach-Object status)
 Check (@($caseStatuses|Where-Object{$_-notin@('passed','failed','not_run')}).Count-eq0) "$($run.label) does not label unexecuted cases as passed"
 Check (@($caseStatuses|Where-Object{$_-eq'passed'}).Count-eq$passed-and@($caseStatuses|Where-Object{$_-eq'failed'}).Count-eq$failed-and@($caseStatuses|Where-Object{$_-eq'not_run'}).Count-eq$notRun) "$($run.label) case records agree with its summary counts"
 return $result
}
function Test-RunnerWatchdog {
 Check ((Get-RunnerBudget 0)-eq60000-and(Get-RunnerBudget 160)-eq255000-and(Get-RunnerBudget 1000)-eq300000) 'watchdog total budget scales by declared cases with independent hard cap'
 Check ((Get-RunnerStop 59999 14999 60000 15000)-eq$null-and(Get-RunnerStop 60000 0 60000 15000)-eq'total_timeout'-and(Get-RunnerStop 15000 15000 60000 15000)-eq'no_progress_timeout') 'watchdog total and no-progress boundaries are independent'
 Reset-Trace
 [IO.File]::WriteAllText($trace,"one.ps1`npartial")
 [IO.File]::WriteAllText($trace+'.args',"{`"script`":`"one.ps1`"}`n{`"script`":")
 Check ((Get-TraceProgress @('one.ps1','two.ps1'))-eq1) 'only complete matching entry and argument rows advance the ordered prefix'
 [IO.File]::WriteAllText($trace+'.args',"{`"script`":`"wrong.ps1`"}`n")
 $rejected=$false;try{[void](Get-TraceProgress @('one.ps1','two.ps1'))}catch{$rejected=$true}
 Check $rejected 'wrong-order argument records cannot feed the watchdog'
 $hang=Join-Path $work 'watchdog_hang.ps1'
 [IO.File]::WriteAllText($hang,@'
$si=[Diagnostics.ProcessStartInfo]::new((Get-Command pwsh).Source)
$si.UseShellExecute=$false;$si.CreateNoWindow=$true
foreach($v in @('-NoProfile','-Command','Start-Sleep -Seconds 60')){[void]$si.ArgumentList.Add($v)}
$child=[Diagnostics.Process]::Start($si)
Write-Output 'arbitrary stdout is not plan progress'
Start-Sleep -Seconds 60
'@)
 Reset-Trace
 $run=Invoke-Runner $hang @() 'watchdog_no_progress' @{idle=3000;total=10000;allowTimeout=$true}
 Check ($run.stop-eq'no_progress_timeout'-and$run.valid_prefix_count-eq0-and$run.elapsed_ms-ge3000-and$run.elapsed_ms-lt11000) 'silent owned runner times out despite arbitrary stdout'
 Check ($run.identity.pid-gt0-and$run.identity.created_filetime-gt0-and$run.owned_job_total-ge2-and$run.owned_job_active_final-eq0-and$run.drains_completed) 'timeout accounts original runner identity and kills its owned descendant job with bounded drains'
 $paced=Join-Path $work 'watchdog_paced.ps1'
 [IO.File]::WriteAllText($paced,@'
for($n=0;$n-lt20;$n++){
 $name="paced$n.ps1"
 Add-Content -LiteralPath $env:VINPUT_SUITE_CONTRACT_TRACE -Value $name
 @{script=$name}|ConvertTo-Json -Compress|Add-Content -LiteralPath ($env:VINPUT_SUITE_CONTRACT_TRACE+'.args')
 Start-Sleep -Milliseconds 300
}
'@)
 Reset-Trace
 $names=@(0..19|ForEach-Object{"paced$_.ps1"})
 $run=Invoke-Runner $paced @() 'watchdog_total' @{expected=$names;idle=1500;total=2500;allowTimeout=$true}
 Check ($run.stop-eq'total_timeout'-and$run.valid_prefix_count-ge2-and$run.valid_prefix_count-lt20-and$run.owned_job_active_final-eq0) 'continuing valid plan progress cannot extend the hard total deadline'
 $leave=Join-Path $work 'watchdog_leave_child.ps1'
 [IO.File]::WriteAllText($leave,([IO.File]::ReadAllText($hang)-replace 'Start-Sleep -Seconds 60\r?\n?$','exit 0'))
 Reset-Trace
 $run=Invoke-Runner $leave @() 'watchdog_natural_exit'
 Check ($run.exit-eq0-and-not$run.stop-and$run.owned_job_total-ge2-and$run.owned_job_active_final-eq0-and$run.drains_completed) 'natural runner exit preserves its code and still reaps leftover owned descendants'
 Reset-Trace
}
try {
 Test-RunnerWatchdog
 if($WatchdogOnly){return}
 # Legacy default is run only until an inert foreground trap. The old runner's
 # early foreground dependency is thereby observed without running any fixture.
 $baseline=& git -C $repo show "${BaselineRef}:verify/run_selftest.ps1"
 if($LASTEXITCODE){throw 'cannot load baseline runner'}
 [IO.File]::WriteAllText((Join-Path $shadow 'legacy_selftest.ps1'),($baseline-join"`r`n"))
 New-Stub 'run_userintentstate.ps1'
 New-Stub 'run_fgguardtest.ps1' 'throw'
 $declaredScripts['legacy_selftest.ps1|physical=False']=@('run_userintentstate.ps1','run_fgguardtest.ps1')
 Reset-Trace
 $legacy=Invoke-Runner (Join-Path $shadow 'legacy_selftest.ps1') @('-Out',(Join-Path $work 'legacy')) 'legacy_default'
 Check ($legacy.exit-ne0-and(Read-Trace)-contains'run_fgguardtest.ps1') 'RED legacy default actually enters a foreground-dependent case'

 foreach($file in @('run_selftest.ps1','run_foreground_suite.ps1','vi_suiteplan.ps1')){
  $source=Join-Path $SourceDir $file
  if(-not(Test-Path $source)){throw "proposed runner file not ready: $file"}
  Copy-Item -LiteralPath $source -Destination (Join-Path $shadow $file)
 }
 # Trap every known script before reading plans. Planning may create artifacts,
 # but must not execute even a mocked native/foreground case.
 foreach($file in Get-ChildItem -LiteralPath $SourceDir -Filter '*.ps1'){
  if($file.Name-notin@('run_selftest.ps1','run_foreground_suite.ps1','vi_suiteplan.ps1')){New-Stub $file.Name 'throw'}
 }
 $background=Plan 'run_selftest.ps1' @() 'background_plan'
 $dialogTokens=@($background.cases|Where-Object id -eq 'dialog_tokens')
 Check ($dialogTokens.Count-eq1-and$null-ne$dialogTokens[0].arguments.RepoRoot) 'dialog-token case explicitly supplies its mandatory repository argument'
 Check (([IO.Path]::GetFullPath($dialogTokens[0].arguments.RepoRoot))-eq$work) 'dialog-token repository argument resolves to the selected copied private source root'
 $retargetGuard=@($background.cases|Where-Object id -eq 'retarget_quiescence')
 Check ($retargetGuard.Count-eq1-and$null-ne$retargetGuard[0].arguments.RepoRoot-and$null-ne$retargetGuard[0].arguments.CliDir) 'retarget-quiescence case explicitly supplies repository and CLI arguments'
 Check (([IO.Path]::GetFullPath($retargetGuard[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($retargetGuard[0].arguments.CliDir))-eq(Join-Path $work 'dist')) 'retarget-quiescence arguments resolve inside the selected copied private source root'
 $captureDiagnostics=@($background.cases|Where-Object id -eq 'capture_diagnostics')
 Check ($captureDiagnostics.Count-eq1-and$null-ne$captureDiagnostics[0].arguments.RepoRoot-and$null-ne$captureDiagnostics[0].arguments.CliDir) 'capture-diagnostics case explicitly supplies repository and CLI arguments'
 Check (([IO.Path]::GetFullPath($captureDiagnostics[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($captureDiagnostics[0].arguments.CliDir))-eq(Join-Path $work 'dist')) 'capture-diagnostics arguments resolve inside the selected copied private source root'
 $launchContract=@($background.cases|Where-Object id -eq 'launch_contract')
 Check ($launchContract.Count-eq1-and$null-ne$launchContract[0].arguments.RepoRoot-and$null-ne$launchContract[0].arguments.SourceRoot-and$null-ne$launchContract[0].arguments.DllDir) 'launch-contract case explicitly supplies repository source and DLL arguments'
 Check (([IO.Path]::GetFullPath($launchContract[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($launchContract[0].arguments.SourceRoot))-eq$work-and([IO.Path]::GetFullPath($launchContract[0].arguments.DllDir))-eq(Join-Path $work 'dist')) 'launch-contract arguments resolve inside the selected copied private source root'
 $launchStatus=@($background.cases|Where-Object id -eq 'launch_status_json')
 Check ($launchStatus.Count-eq1-and$null-ne$launchStatus[0].arguments.RepoRoot-and$null-ne$launchStatus[0].arguments.SamplesDir) 'launch-status-json case explicitly supplies repository and captured sample arguments'
 Check (([IO.Path]::GetFullPath($launchStatus[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($launchStatus[0].arguments.SamplesDir))-eq(Join-Path $work 'verify\session6_launch_contract\config_observer')) 'launch-status-json arguments resolve inside the selected copied private source root'
 $nativeClip=@($background.cases|Where-Object id -eq 'native_clip')
 Check ($nativeClip.Count-eq1-and$null-ne$nativeClip[0].arguments.RepoRoot-and$null-ne$nativeClip[0].arguments.CliDir) 'native clipping declares its private source and binary roots'
 Check (([IO.Path]::GetFullPath($nativeClip[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($nativeClip[0].arguments.CliDir))-eq(Join-Path $work 'dist')) 'native clipping arguments resolve inside the selected copied private root'
 $wndQuarantine=@($background.cases|Where-Object id -eq 'wnd_quarantine')
 Check ($wndQuarantine.Count-eq1-and$null-ne$wndQuarantine[0].arguments.RepoRoot-and$null-ne$wndQuarantine[0].arguments.DllDir) 'WndProc quarantine declares its mandatory repository and DLL roots'
 Check (([IO.Path]::GetFullPath($wndQuarantine[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($wndQuarantine[0].arguments.DllDir))-eq(Join-Path $work 'dist')) 'WndProc quarantine arguments resolve inside the selected copied private root'
 $ocrMap=@($background.cases|Where-Object id -eq 'ocr_map')
 Check ($ocrMap.Count-eq1-and$null-ne$ocrMap[0].arguments.RepoRoot-and$null-ne$ocrMap[0].arguments.CliDir) 'OCR-map regression declares mandatory repository and CLI roots'
 Check (([IO.Path]::GetFullPath($ocrMap[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($ocrMap[0].arguments.CliDir))-eq(Join-Path $work 'dist')) 'OCR-map arguments resolve inside the selected copied private root'
 $foreground=Plan 'run_foreground_suite.ps1' @() 'foreground_plan'
 $padReadiness=@($background.cases|Where-Object id -eq 'pad_readiness')
 Check ($padReadiness.Count-eq1-and$null-ne$padReadiness[0].arguments.RepoRoot-and$null-ne$padReadiness[0].arguments.CliDir) 'pad-readiness regression declares repository and native binary roots'
 Check (([IO.Path]::GetFullPath($padReadiness[0].arguments.RepoRoot))-eq$work-and([IO.Path]::GetFullPath($padReadiness[0].arguments.CliDir))-eq(Join-Path $work 'dist')) 'pad-readiness arguments resolve inside the selected copied private root'
 $physical=Plan 'run_foreground_suite.ps1' @('-IncludePhysicalInput') 'foreground_physical_plan'
 Check ($background.suite-eq'background'-and$foreground.suite-eq'foreground'-and$physical.suite-eq'foreground') 'plans identify the correct suite'
 Check ($background.excluded_count-eq8-and@($background.skipped_cases).Count-eq8-and@($foreground.cases).Count-eq9-and@($physical.cases).Count-eq12) 'eight moved controls, one additional foreground case and three explicit physical controls are accounted'
 $expected=@(
  'run_fgguardtest.ps1|','run_focusrobusttest.ps1|',
  'run_hosthistorytest.ps1|','run_hosthistorytest.ps1|-NoInjectLaunch',
  'run_windowfocustest.ps1|-CreateWindowAttack','run_windowfocustest.ps1|-WorkerCreateAttack',
  'run_windowfocustest.ps1|-AnsiCreateAttack -WorkerCreateAttack','run_windowfocustest.ps1|-ReplaceChain'
 )
 $actual=@($foreground.cases|ForEach-Object{Case-Key $_})
 Check (@(Compare-Object ($expected+@('run_hostednoactivatetest.ps1|')) $actual).Count-eq0) 'foreground plan retains every original case/argument variant and hosted-noactivate extra'
 $excluded=@($background.skipped_cases|ForEach-Object{Case-Key $_})
 Check (@(Compare-Object $excluded $expected).Count-eq0-and@($excluded|Where-Object{$_-notin$actual}).Count-eq0) 'default exclusion list contains exactly the eight moved controls as foreground-plan subset'
 $backgroundKeys=@($background.cases|ForEach-Object{Case-Key $_})
 Check (@($backgroundKeys|Where-Object{$_-in$actual}).Count-eq0) 'default execution plan contains no moved foreground case'
 Check (@($background.skipped_cases|Where-Object{-not$_.reason-or$_.moved_to-notmatch'run_foreground_suite\.ps1$'}).Count-eq0) 'each excluded control explains its reason and destination'
 Check (@($foreground.cases|Where-Object{$_.physical_input-ne$false}).Count-eq0) 'ordinary foreground plan never opts into real input'
 $physicalCases=@($physical.cases|Where-Object physical_input)
 Check ($physicalCases.Count-eq3-and@($physicalCases|Where-Object{[IO.Path]::GetFileName($_.script)-ne'run_userintenttest.ps1'-or$_.origin-ne'explicit'}).Count-eq0) 'real-input cases require the explicit opt-in origin'

 foreach($case in @($background.cases)+@($physical.cases)){New-Stub ([IO.Path]::GetFileName($case.script))}
 foreach($suite in @(@{runner='run_selftest.ps1';plan=$background;name='background'},@{runner='run_foreground_suite.ps1';plan=$foreground;name='foreground'})){
  foreach($case in @($background.cases)+@($physical.cases)){New-Stub ([IO.Path]::GetFileName($case.script))}
  if($suite.name-eq'background'){foreach($case in $physical.cases){New-Stub ([IO.Path]::GetFileName($case.script)) 'throw'}}
  $plan=$suite.plan;$folder=Join-Path $work ($suite.name+'_success')
  Reset-Trace
  # A child handles an expected native failure but exits zero. Its private native
  # status must not contaminate its caller or the next independent case.
  New-Stub ([IO.Path]::GetFileName($plan.cases[0].script)) 'stale'
  $run=Invoke-Runner (Join-Path $shadow $suite.runner) @('-Out',$folder) ($suite.name+'_success')
  Check ($run.exit-eq0) "$($suite.name) successful child exits are not poisoned by stale native status"
  $result=Validate-Result $run $folder 'passed' @($plan.cases).Count 0 0
  Check ((Read-Trace).Count-eq@($plan.cases).Count) "$($suite.name) success executes exactly its declared cases"
  $calls=@(Get-Content ($trace+'.args')|ForEach-Object{$_|ConvertFrom-Json})
  $argumentsCorrect=$calls.Count-eq@($plan.cases).Count
  for($i=0;$i-lt$calls.Count-and$argumentsCorrect;$i++){
   $case=$plan.cases[$i];$call=$calls[$i]
   $argumentsCorrect=$call.script-eq[IO.Path]::GetFileName($case.script)-and$call.out-eq(Join-Path (Join-Path $folder $case.id) ('run_'+$result.invocation_id))-and(@($call.arguments)-join' ')-eq((Case-Arguments $case)-join' ')
  }
  Check $argumentsCorrect "$($suite.name) forwards declared arguments and spaced artifact paths exactly"
  if($suite.name-eq'background'){
   Check (@($result.skipped_cases).Count-eq8) 'background result retains all eight moved controls'
   foreach($case in $background.skipped_cases){Check ($run.stdout-match('SKIPPED.*'+[regex]::Escape($case.id))) "background console identifies skipped control $($case.id)"}
  }
  New-Stub ([IO.Path]::GetFileName($plan.cases[0].script))
  foreach($failure in @('throw','nonzero','reported_failure','missing','exit3')){
   $first=[IO.Path]::GetFileName($plan.cases[0].script)
   if($failure-eq'missing'){Remove-Item -LiteralPath (Join-Path $shadow $first)}else{New-Stub $first $failure}
   $folder=Join-Path $work ($suite.name+'_'+$failure);Reset-Trace
   $run=Invoke-Runner (Join-Path $shadow $suite.runner) @('-Out',$folder) ($suite.name+'_'+$failure)
   Check ($run.exit-ne0) "$($suite.name) $failure propagates as a nonzero suite exit"
   $result=Validate-Result $run $folder 'failed' 0 1 (@($plan.cases).Count-1)
   $expectedCode=switch($failure){'nonzero'{7};'exit3'{3};'missing'{$null};default{1}}
   Check ($result.cases[0].status-eq'failed'-and$result.cases[0].exit_code-eq$expectedCode-and@($result.cases|Select-Object -Skip 1|Where-Object status -ne 'not_run').Count-eq0) "$($suite.name) $failure retains the actual exit code and later-case order"
   $traceRows=Read-Trace
   $expectedRows=if($failure-eq'missing'){0}else{1}
   Check ($traceRows.Count-eq$expectedRows-and($failure-eq'missing'-or$traceRows[0]-eq$first)) "$($suite.name) $failure cannot be overwritten by later passing cases"
   New-Stub $first
  }
 }
 foreach($case in $background.cases){New-Stub ([IO.Path]::GetFileName($case.script))}
 $nativeCase=$background.cases|Where-Object id -eq 'legacy_focus';New-Stub ([IO.Path]::GetFileName($nativeCase.script)) 'not_measured'
 $folder=Join-Path $work 'background_not_measured';Reset-Trace;$run=Invoke-Runner (Join-Path $shadow 'run_selftest.ps1') @('-Out',$folder) 'background_not_measured'
 $result=Get-Content (Join-Path $folder 'suite_result.json') -Raw|ConvertFrom-Json
 Check ($run.exit-eq3-and$result.status-eq'not_measured'-and$result.not_measured_suite_count-eq1-and$result.failed_suite_count-eq0-and$result.passed_suite_count-eq@($background.cases).Count-1) 'valid inconclusive native receipt stays exit3 and never becomes PASS or FAIL'
 Check ($result.not_run_count-eq0-and(Read-Trace).Count-eq@($background.cases).Count-and@($result.cases|Where-Object status -eq 'not_measured').Count-eq1) 'inconclusive native control preserves later-suite execution and exact counts'
 foreach($case in $background.cases){New-Stub ([IO.Path]::GetFileName($case.script))}
 $cursorCase=$background.cases|Where-Object id -eq 'cursor_guard';Check ($null-ne$cursorCase) 'cursor guard has a declared background case'
 foreach($mode in @('not_measured','nm_string_completed','exit3')){
  New-Stub ([IO.Path]::GetFileName($cursorCase.script)) $mode
  $folder=Join-Path $work ('cursor_'+$mode);Reset-Trace;$run=Invoke-Runner (Join-Path $shadow 'run_selftest.ps1') @('-Out',$folder) ('cursor_'+$mode)
  $result=Get-Content (Join-Path $folder 'suite_result.json') -Raw|ConvertFrom-Json
  if($mode-eq'not_measured'){Check ($run.exit-eq3-and$result.status-eq'not_measured'-and$result.failed_suite_count-eq0-and$result.not_measured_suite_count-eq1-and$result.not_run_count-eq0-and$result.passed_suite_count-eq@($background.cases).Count-1) 'valid cursor NM receipt remains inconclusive and executes later stubs'}
  else{Check ($run.exit-ne0-and$result.status-eq'failed'-and$result.failed_suite_count-eq1-and$result.not_measured_suite_count-eq0) "cursor $mode cannot impersonate an inconclusive measurement"}
 }
 foreach($case in $background.cases){New-Stub ([IO.Path]::GetFileName($case.script))}
 $captureCase=$background.cases|Where-Object id -eq 'capture_diagnostics';Check ($null-ne$captureCase) 'capture diagnostics has a declared background case'
 # exit3 supplies no inner receipt; the other negatives isolate path/schema errors.
 foreach($mode in @('capture_not_measured','exit3','capture_multiple_inner','capture_flat_only','capture_wrong_reason','capture_string_completed','capture_no_positive')){
  New-Stub ([IO.Path]::GetFileName($captureCase.script)) $mode
  $folder=Join-Path $work ('capture_'+$mode);Reset-Trace;$run=Invoke-Runner (Join-Path $shadow 'run_selftest.ps1') @('-Out',$folder) ('capture_'+$mode)
  $result=Get-Content (Join-Path $folder 'suite_result.json') -Raw|ConvertFrom-Json
  if($mode-eq'capture_not_measured'){Check ($run.exit-eq3-and$result.status-eq'not_measured'-and$result.failed_suite_count-eq0-and$result.not_measured_suite_count-eq1-and$result.not_run_count-eq0-and$result.passed_suite_count-eq@($background.cases).Count-1) 'exactly one fresh nested capture NM receipt preserves exact counts and later stubs'}
  else{Check ($run.exit-ne0-and$result.status-eq'failed'-and$result.failed_suite_count-eq1-and$result.not_measured_suite_count-eq0) "capture $mode cannot impersonate an explicit complete NM receipt"}
 }
} finally {
 $checks|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Out 'checks.json')
 $evidence|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Out 'evidence.json')
}
$failed=@($checks|Where-Object{-not$_.pass}).Count
"$($checks.Count-$failed) PASS / $failed FAIL (inert runner contracts only)"
if($failed){exit 1};$global:LASTEXITCODE=0
