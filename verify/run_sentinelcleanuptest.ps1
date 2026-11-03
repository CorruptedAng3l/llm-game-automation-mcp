# Actual cleanup function with a fake native surface/witness/process stopper. No native UI calls.
param([string]$Helper="$PSScriptRoot\_focus_sentinel.ps1",[string]$Out="$PSScriptRoot\..\build\test\_out\sentinelcleanup",[switch]$KeepGoing)
$ErrorActionPreference='Stop';New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
Add-Type 'using System;public static class ViSentinelNative{public static IntPtr Current;public static uint CurrentOwner,InitialOwner;public static bool InitialValid,ChangeOnInitial;public static IntPtr GetForegroundWindow(){return Current;}public static uint GetWindowThreadProcessId(IntPtr h,out uint p){p=h==new IntPtr(10)?InitialOwner:(h==Current?CurrentOwner:0);if(ChangeOnInitial && h==new IntPtr(10))Current=new IntPtr(30);return 1;}public static bool IsWindow(IntPtr h){return h==new IntPtr(10)?InitialValid:h!=IntPtr.Zero;}}'
. $Helper
$script:requests=0;$script:stopped=@();$checks=@()
function Invoke-SentinelWitness($State,[IntPtr]$WindowHandle,[string]$Label){++$script:requests;[ViSentinelNative]::Current=$WindowHandle;$worker=[pscustomobject]@{};$worker|Add-Member ScriptMethod WaitForExit {param($Milliseconds)$true};[pscustomobject]@{process=$worker}}
function Stop-Process {[CmdletBinding(SupportsShouldProcess)]param([int]$Id,[switch]$Force);$script:stopped+=$Id}
function Check($ok,$name){$script:checks+=[pscustomobject]@{pass=[bool]$ok;name=$name};"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not $ok -and -not $KeepGoing){throw $name}}
$cases=@(
 @{name='own sentinel foreground';current=20;owner=20;initial_owner=10;valid=$true;restore=1},
 @{name='new third-party human choice';current=30;owner=30;initial_owner=10;valid=$true;restore=0},
 @{name='original already foreground';current=10;owner=10;initial_owner=10;valid=$true;restore=0},
 @{name='foreground unavailable';current=0;owner=0;initial_owner=10;valid=$true;restore=0},
 @{name='sentinel HWND reused by other owner';current=20;owner=99;initial_owner=10;valid=$true;restore=0},
 @{name='original HWND reused by other owner';current=20;owner=20;initial_owner=99;valid=$true;restore=0},
 @{name='original window destroyed';current=20;owner=20;initial_owner=0;valid=$false;restore=0},
 @{name='new choice during destination lookup';current=20;owner=20;initial_owner=10;valid=$true;restore=0;change=$true}
)
foreach($case in $cases){
 $script:requests=0;$script:stopped=@();[ViSentinelNative]::Current=[IntPtr]$case.current;[ViSentinelNative]::CurrentOwner=$case.owner;[ViSentinelNative]::InitialOwner=$case.initial_owner;[ViSentinelNative]::InitialValid=$case.valid;[ViSentinelNative]::ChangeOnInitial=[bool]$case.change
 $state=[pscustomobject]@{process=[pscustomobject]@{Id=20;HasExited=$false};hwnd=[IntPtr]20;initial_hwnd=[IntPtr]10;initial_pid=10;helpers=@();out=$Out}
 Close-FocusSentinel $state
 Check ($script:requests -eq $case.restore) "$($case.name): handback count=$script:requests expected=$($case.restore)"
 Check ($script:stopped.Count -eq 1 -and $script:stopped[0] -eq 20) "$($case.name): only own sentinel cleanup remains"
}
$checks|ConvertTo-Json -Depth 5|Set-Content "$Out\checks.json";$failed=@($checks|Where-Object{-not $_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
