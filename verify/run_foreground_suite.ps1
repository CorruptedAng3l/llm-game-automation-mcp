# Explicit foreground controls; run while idle. Each child owns its canonical LIVE lock.
param([string]$Out="$PSScriptRoot\..\build\test\_out\foreground_suite",[switch]$PlanOnly,
      [switch]$IncludePhysicalInput,[scriptblock]$AfterForeground)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\vi_suiteplan.ps1"
$plan=Get-VinputSuitePlan -Suite foreground -IncludePhysicalInput:$IncludePhysicalInput
if ($PlanOnly) { $plan | ConvertTo-Json -Depth 8;return }
try { Invoke-VinputSuitePlan -Plan $plan -Out $Out -ScriptRoot $PSScriptRoot }
finally { if ($AfterForeground) { & $AfterForeground } }
exit $LASTEXITCODE
