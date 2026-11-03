# Background-safe gate. Real foreground controls are explicit in run_foreground_suite.ps1.
param([string]$Out="$PSScriptRoot\..\build\test\selftest",[switch]$PlanOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\vi_suiteplan.ps1"
$plan=Get-VinputSuitePlan -Suite background
if ($PlanOnly) { $plan | ConvertTo-Json -Depth 8;return }
Invoke-VinputSuitePlan -Plan $plan -Out $Out -ScriptRoot $PSScriptRoot
exit $LASTEXITCODE
