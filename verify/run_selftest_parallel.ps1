# Parallel background suite: the same cases as run_selftest.ps1, split into shards that run side by side.
# Cases that share machine-wide state (physical cursor / quiet-machine controls, lock files, the anti-cheat refusal test
# whose fake BEService.exe makes every other injection refuse) run in one serial lane; anticheat_refusal runs alone last.
param([string]$Out="$PSScriptRoot\..\build\test\selftest_par",[int]$Shards=[Math]::Max(2,[Math]::Min(8,[Environment]::ProcessorCount/2)),[string[]]$Only=@(),[string[]]$Serial=@())
$ErrorActionPreference='Stop'
. "$PSScriptRoot\vi_suiteplan.ps1"
$plan=Get-VinputSuitePlan -Suite background
# -Only: rerun a subset (e.g. the cases a failed lane did not reach). -Serial adds ids to the serial lane.
if($Only.Count){$plan.cases=@($plan.cases|Where-Object{$_.id -in $Only})}
$serialIds=@('cursor_guard','legacy_focus','physical_readers','recenterguard','edge_guard','owner_input','lease_invariants','legacylock','lock_wait','launch','early','multi','relative_accounting')+$Serial
$lastIds=@('anticheat_refusal')
$parallel=@($plan.cases | Where-Object { $_.id -notin $serialIds -and $_.id -notin $lastIds })
$lanes=@(for($i=0;$i -lt $Shards;$i++){ ,@() })
for($i=0;$i -lt $parallel.Count;$i++){ $lanes[$i % $Shards]+=$parallel[$i] }
$lanes+=,@($plan.cases | Where-Object { $_.id -in $serialIds })
New-Item -ItemType Directory -Force $Out | Out-Null;$Out=(Resolve-Path $Out).Path
$shell=(Get-Command pwsh).Source
function Start-Lane([object[]]$cases,[string]$name){
 $dir=Join-Path $Out $name;New-Item -ItemType Directory -Force $dir | Out-Null
 $sub=[pscustomobject]@{schema=1;suite='background';requires_idle=$false;physical_input=$false;cases=$cases;skipped_cases=@();excluded_count=0}
 $sub | Export-Clixml (Join-Path $dir 'plan.xml')
 $runner=Join-Path $dir 'lane.ps1'
 Set-Content $runner (". '$PSScriptRoot\vi_suiteplan.ps1'; `$p=Import-Clixml '$dir\plan.xml'; Invoke-VinputSuitePlan -Plan `$p -Out '$dir' -ScriptRoot '$PSScriptRoot'")
 Start-Process $shell -ArgumentList @('-NoProfile','-File',$runner) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $dir 'stdout.log') -RedirectStandardError (Join-Path $dir 'stderr.log')
}
$watch=[Diagnostics.Stopwatch]::StartNew()
$procs=@();for($i=0;$i -lt $lanes.Count;$i++){ if(@($lanes[$i]).Count){ $procs+=Start-Lane $lanes[$i] ("lane$i") } }
"started $($procs.Count) lanes (PIDs $($procs.Id -join ',')); $($parallel.Count) parallel + $(@($lanes[-1]).Count) serial cases"
$procs | Wait-Process
$lastCases=@($plan.cases | Where-Object { $_.id -in $lastIds })
if($lastCases.Count){ $last=Start-Lane $lastCases 'lane_last';$last | Wait-Process }
$summary=foreach($d in Get-ChildItem $Out -Directory){ $r=Join-Path $d.FullName 'suite_result.json'; if(Test-Path $r){ $j=Get-Content $r -Raw|ConvertFrom-Json; foreach($c in $j.cases){ [pscustomobject]@{lane=$d.Name;id=$c.id;status=$c.status;secs=[int]($c.elapsed_ms/1000);log=$c.log_path} } } }
$summary | Sort-Object status,lane | Format-Table -AutoSize | Out-String -Width 220
$bad=@($summary | Where-Object { $_.status -notin 'passed','not_measured' })
"TOTAL cases=$($summary.Count) passed=$(@($summary|? status -eq 'passed').Count) not_measured=$(@($summary|? status -eq 'not_measured').Count) other=$($bad.Count) wall_secs=$([int]$watch.Elapsed.TotalSeconds)"
if($bad.Count -or $summary.Count -ne $plan.cases.Count){exit 1}
