param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\edge_guard\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_ownedprocess.ps1";$root=(Resolve-Path "$PSScriptRoot\..").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path;$tools="$Out\tools";New-Item -ItemType Directory -Force $tools,"$tools\temp"|Out-Null
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViEdgeNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=@()
function Check($Ok,$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Call([string[]]$Arguments){$r=& $V --json --pid $script:targetId @Arguments|ConvertFrom-Json;$records.Add(@{args=$Arguments;reply=$r});return $r}
function ResetPoint($X,$Y){$n=@(Get-Content $script:log|Where-Object{$_-like'RESET *'}).Count;[ViEdgeNative]::PostMessage($script:h,0x8001,[IntPtr]$X,[IntPtr]$Y)|Out-Null;$until=[Environment]::TickCount64+2000;do{if(@(Get-Content $script:log|Where-Object{$_-like'RESET *'}).Count-gt$n){return};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$until);throw 'reset not dispatched'}
function LastReads { $text=Get-Content $script:log -Raw;return @($text.Substring($text.LastIndexOf('RESET '))-split"`n"|Where-Object{$_-match'^READ '}) }
try{
 foreach($arch in @('x64','x86')){$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'};$batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX "$root\test\vi_edgeguard.cpp" /Fo:"$tools\edge$suffix.obj" /Fe:"$tools\edge$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
"@;[IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch fixture zero warnings"
  $script:log="$Out\edge$suffix.log";$p=Start-Process "$tools\edge$suffix.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned+=$p;$script:targetId=$p.Id
  $r=& $V --json inject --pid $p.Id|ConvertFrom-Json;Check $r.ok "$arch injected"
  $s=Call @('config','cursor=virtual');$script:h=[IntPtr]::new([Convert]::ToInt64($s.hwnd.Substring(2),16));Check (-not$s.really_foreground) "$arch background only"
  ResetPoint 0 60;$r=Call @('mouse','100','60','--steps','40','--step-ms','20');Start-Sleep -Milliseconds 50;$reads=LastReads
  Check ($reads.Count-gt10-and-not@($reads|Where-Object{$_-notmatch'scroll=0 '}).Count) "$arch edge-origin path and rest never edge-scroll"
  Check ($r.ok-and$r.x-eq100-and$r.y-eq60-and$r.edge_guard_applied-and-not$r.edge_contact) "$arch guarded path telemetry and exact endpoint"
  ResetPoint 100 60;$r=Call @('mouse','2','60','--steps','20','--step-ms','20');Start-Sleep -Milliseconds 50;$reads=LastReads
  Check ($r.ok-and$r.x-eq2-and$r.edge_contact-and@($reads|Where-Object{$_-match'scroll=[1-9]'}).Count-gt0) "$arch intentional edge destination stays exact and warns"
  $s=Call @('status');Check ($s.edge_contact-and$s.edge_margin-eq8-and$s.input_precision.edge_guard) "$arch status capability and live edge contact"
  $null=Call @('config','edge_margin=0');ResetPoint 0 60;$r=Call @('mouse','100','60','--steps','40','--step-ms','20');Start-Sleep -Milliseconds 50;$reads=LastReads
  Check (@($reads|Where-Object{$_-match'scroll=[1-9]'}).Count-gt0) "$arch disabled guard positive edge-scroll control"
  $s=Call @('config','edge_margin=12');ResetPoint 0 0;$r=Call @('mouse','100','100','--steps','40','--step-ms','20');Start-Sleep -Milliseconds 50;$reads=LastReads
  Check ($r.ok-and$r.edge_margin-eq12-and-not@($reads|Where-Object{$_-notmatch'scroll=0 '}).Count) "$arch configurable corner escape"
  $r=Call @('move','--raw','-200','-200','--steps','5');Check ($r.ok-and$r.applied_dx-eq-200-and$r.applied_dy-eq-200-and$r.edge_guard_bypassed) "$arch raw camera deltas bypass pointer guard exactly"
  ResetPoint 0 60;$s=Call @('status');Check ($s.edge_contact-and$s.vcur_x-eq0) "$arch game cursor writer remains visible in live status"
  $bad=Call @('config','edge_margin=-1');$s=Call @('status');Check (-not$bad.ok-and$s.edge_margin-eq12) "$arch invalid margin rejected without mutation"
  [ViEdgeNative]::PostMessage($h,0x8002,[IntPtr]5,[IntPtr]5)|Out-Null
  $until=[Environment]::TickCount64+2000;do{$s=Call @('status');if($s.client_w-eq5){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
  ResetPoint 0 0;$r=Call @('mouse','2','2','--steps','10');Check ($s.client_w-eq5-and$r.ok-and$r.x-eq2-and$r.y-eq2-and$r.edge_margin_x-eq2-and-not$r.edge_contact) "$arch tiny client has a bounded safe center"
  Stop-ViOwnedProcess $p
 }
}finally{foreach($p in $owned){if(-not$p.HasExited){Stop-ViOwnedProcess $p}};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
