param([string]$Out="$PSScriptRoot\..\build\test\_out\physical_window")
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_physicalwindow.ps1"
New-Item -ItemType Directory -Force $Out|Out-Null
$checks=[Collections.Generic.List[object]]::new()
foreach($case in @(
 @{name='quiet zero';input=$false;age=800;changes=0;held=0;errors=0;expected='passed'},
 @{name='quiet positive input is still detected';input=$true;age=800;changes=0;held=0;errors=0;expected='failed'},
 @{name='input plus physical tick change is inconclusive';input=$true;age=800;changes=1;held=0;errors=0;expected='not_measured'},
 @{name='held physical key contaminates even unchanged last-input tick';input=$true;age=800;changes=0;held=1;errors=0;expected='not_measured'},
 @{name='recent physical event contaminates pre-window state';input=$true;age=100;changes=0;held=0;errors=0;expected='not_measured'},
 @{name='unavailable witness never supplies a clean negative';input=$false;age=800;changes=0;held=0;errors=1;expected='not_measured'}
)){
 $w=@{initial_age_ms=$case.age;tick_changes=$case.changes;held_samples=$case.held;read_failures=$case.errors};$actual=Get-ViNativeWindowVerdict $w $case.input
 $checks.Add(@{pass=($actual-eq$case.expected);name=$case.name;actual=$actual;expected=$case.expected;witness=$w})
 "$(if($actual-eq$case.expected){'PASS'}else{'FAIL'}) $($case.name)"
}
$live=[ViPhysicalWindow]::Sample(100);@{checks=$checks;read_only_live_witness=$live}|ConvertTo-Json -Depth 6|Set-Content "$Out\result.json"
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
