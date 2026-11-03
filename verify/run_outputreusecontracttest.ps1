param([string]$Out="$PSScriptRoot\..\build\test\_out\output_reuse",[switch]$ExpectRed)
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_suiteplan.ps1"
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$stub="$Out\stubs";New-Item -ItemType Directory -Force $stub|Out-Null
@'
param([Parameter(Mandatory)][string]$Out)
$ErrorActionPreference='Stop';New-Item -ItemType Directory -Force $Out|Out-Null
$marker=Join-Path $Out 'fixture.resume'
if(Test-Path -LiteralPath $marker){'FAIL stale sentinel from earlier invocation';exit 1}
[IO.File]::WriteAllText($marker,'owned fixture marker');'PASS new invocation has no stale sentinel';exit 0
'@|Set-Content "$stub\fixture.ps1" -Encoding utf8NoBOM
$case=New-VinputSuiteCase 'repeat' 'fixture.ps1';$plan=[pscustomobject]@{suite='output_reuse';requires_idle=$false;physical_input=$false;cases=@($case);skipped_cases=@();excluded_count=0}
$runs=[Collections.Generic.List[object]]::new();$root="$Out\same_root"
foreach($attempt in 1..2){$good=$false;$errorText='';try{Invoke-VinputSuitePlan -Plan $plan -Out $root -ScriptRoot $stub;$good=$LASTEXITCODE-eq0}catch{$errorText=$_.Exception.Message}
 $receipt=Get-Content "$root\suite_result.json" -Raw|ConvertFrom-Json;Copy-Item "$root\suite_result.json" "$Out\run$attempt.json";$runs.Add(@{attempt=$attempt;pass=$good;error=$errorText;receipt=$receipt})
}
$first=$runs[0].pass;$second=$runs[1].pass;$distinct=$runs[0].receipt.cases[0].log_path-ne$runs[1].receipt.cases[0].log_path
$pass=if($ExpectRed){$first-and!$second}else{$first-and$second-and$distinct}
@{pass=$pass;expected_red=[bool]$ExpectRed;distinct_child_logs=$distinct;runs=$runs}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json"
"$(if($pass){'PASS'}else{'FAIL'}) actual same-output-root repeat control first=$first second=$second distinct=$distinct expected_red=$ExpectRed";if(!$pass){exit 1};exit 0
