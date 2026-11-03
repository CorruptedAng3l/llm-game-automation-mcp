# Managed mocks only; this test does not load user32 or generate OS input.
param([string]$Harness="$PSScriptRoot\run_userintenttest.ps1",[string]$Out="$PSScriptRoot\..\build\test\user_intent\bootstrap_at_cursor")
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $Out|Out-Null
$Out=(Resolve-Path $Out).Path
Add-Type @'
using System;
public static class UserIntentNative {
 public struct Pt {public int x,y;}
 public static int reads,pidReads,scenario,windowMoves,moveX,moveY;public static uint moveFlags;
 public static uint GetWindowThreadProcessId(IntPtr h,out uint p){p=(uint)(scenario==3&&++pidReads>1?100:99);return 1;}
 public static bool GetCursorPos(out Pt p){p=new Pt{x=500,y=500};if(scenario==1&&++reads>=3)p.x++;return true;}
 public static bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int z,uint flags){windowMoves++;moveX=x;moveY=y;moveFlags=flags;return true;}
 public static IntPtr GetAncestor(IntPtr h,uint flags){return h;}
 public static IntPtr WindowFromPoint(Pt p){return (IntPtr)(scenario==2?456:123);}
}
'@
function Start-Sleep {param($Milliseconds)}
$script:owned=@([pscustomobject]@{Id=99})
$events=[Collections.Generic.List[bool]]::new()
function InputButton([bool]$up){$events.Add($up)}
$parseErrors=$null;$ast=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $Harness),[ref]$null,[ref]$parseErrors)
if($parseErrors){throw 'parse error'}
$helper=$ast.Find({param($n)$n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'ClickAtExistingCursor'},$true)
. ([scriptblock]::Create($helper.Extent.Text))
$checks=@()
foreach($scenario in 0..3){
 [UserIntentNative]::scenario=$scenario;[UserIntentNative]::reads=0;[UserIntentNative]::pidReads=0;[UserIntentNative]::windowMoves=0;$events.Clear()
 $point=[UserIntentNative+Pt]::new();$point.x=500;$point.y=500;$threw=$false
 try{ClickAtExistingCursor ([IntPtr]123) $point}catch{$threw=$true}
 $pass=if($scenario -eq 0){-not $threw -and $events.Count -eq 2 -and -not $events[0] -and $events[1] -and [UserIntentNative]::moveX -eq 400 -and [UserIntentNative]::moveY -eq 400 -and [UserIntentNative]::moveFlags -eq 0x51}else{$threw -and $events.Count -eq 0}
 $checks += [pscustomobject]@{scenario=$scenario;meaning=@('stable: ownwindowaroundcursor,onepair','cursor drift: noclick','occluded: noclick','changedPID: noclick')[$scenario];pass=$pass;threw=$threw;button_events=$events.Count;window_moves=[UserIntentNative]::windowMoves}
}
$checks|ConvertTo-Json|Set-Content "$Out\mock_checks.json"
$checks|Format-Table
if(@($checks|Where-Object{-not $_.pass}).Count){throw 'mock checks failed'}
