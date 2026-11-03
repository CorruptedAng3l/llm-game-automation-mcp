# Splash -> main virtual activation recovery; real activation only in the locked API positive control.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\windowfocus",[switch]$KeepGoing,[int]$LockWaitSec=300,[switch]$CreateWindowAttack,[switch]$WorkerCreateAttack,[switch]$AnsiCreateAttack,[switch]$ReplaceChain)
$ErrorActionPreference='Stop';$V=(Resolve-Path "$CliDir\vinput.exe").Path;$D=(Resolve-Path $TargetDir).Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
. "$PSScriptRoot\_focus_sentinel.ps1"
if(-not('ViWindowFocusNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViWindowFocusNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'}
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[Diagnostics.Process]]::new();$sentinel=$null
function Check($ok,$Name){$checks.Add(@{pass=[bool]$ok;name=$Name});"$(if($ok){'PASS'}else{'FAIL'}) $Name";if(-not$ok-and-not$KeepGoing){throw $Name}}
function Vi($GamePid,[string[]]$Arguments){& $V --json --pid $GamePid @Arguments|ConvertFrom-Json}
function Text($Log){Get-Content -LiteralPath $Log -Raw -ErrorAction SilentlyContinue}
function Count($Log,$Pattern){[regex]::Matches((Text $Log),$Pattern).Count}
function Post($Window,$Message,[IntPtr]$Value=[IntPtr]::Zero){if(-not[ViWindowFocusNative]::PostMessage($Window,$Message,$Value,[IntPtr]::Zero)){throw 'owned control post failed'}}
function Wait-Log($Log,$Pattern,$Timeout=3000){$until=[Environment]::TickCount64+$Timeout;do{$text=Text $Log;if($text-match$Pattern){return $Matches};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until);throw "fixture missing $Pattern"}
function Start-Witness($Exe,$GamePid,$Label,$Duration){$log="$Out\$Label.witness.log";$worker=Start-Process -FilePath $Exe -ArgumentList "--witness $GamePid `"$log`" $Duration" -WindowStyle Hidden -PassThru;$owned.Add($worker);Wait-Log $log 'WITNESS_READY hook=1'|Out-Null;return @{process=$worker;log=$log}}
function Finish-Witness($Witness){if(-not$Witness.process.WaitForExit(6000)){throw 'independent foreground witness timed out'};$text=Text $Witness.log;if($text-notmatch'WITNESS_RESULT events=(\d+) samples=(\d+) held_ms=(\d+) ended_target=(\d+)'){throw 'independent witness result missing'};return @{events=[int]$Matches[1];samples=[int]$Matches[2];held_ms=[int]$Matches[3];ended_target=$Matches[4]-eq'1';raw=$text;exit_code=$Witness.process.ExitCode}}
$lock=$(if($env:VINPUT_LOCK){$env:VINPUT_LOCK}else{New-Item -ItemType Directory -Force "$PSScriptRoot\..\build"|Out-Null;"$PSScriptRoot\..\build\foreground.lock"});$token=[guid]::NewGuid().ToString('N');$stream=$null;$begin=[Environment]::TickCount64
while(-not$stream){try{$stream=[IO.File]::Open($lock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)}catch [IO.IOException]{if([Environment]::TickCount64-$begin-gt$LockWaitSec*1000){throw 'canonical lock busy; never removed'};Start-Sleep -Seconds 1}}
$bytes=[Text.Encoding]::UTF8.GetBytes((@{owner='vinput two-window focus';pid=$PID;token=$token;time=(Get-Date -Format o)}|ConvertTo-Json -Compress));$stream.Write($bytes,0,$bytes.Length);$stream.Dispose()
try{
 $sentinel=New-FocusSentinel "$D\vi_focusprobe.exe" "$Out\sentinel"
 foreach($arch in @('','32')){
  $game=$null
  try{
   Set-FocusSentinel $sentinel|Out-Null
   $exe="$D\vi_windowfocus$arch.exe";$log="$Out\window$arch.log"
   $fixtureArgs="`"$log`"";if($CreateWindowAttack){$fixtureArgs+=" --create-window-attack"};if($WorkerCreateAttack){$fixtureArgs+=" --worker-create-attack"};if($AnsiCreateAttack){$fixtureArgs+=" --ansi-create-attack"};if($ReplaceChain){$fixtureArgs+=" --replace-chain"}
   $game=Start-Process -FilePath $exe -ArgumentList $fixtureArgs -WindowStyle Hidden -PassThru;$owned.Add($game)
   $inject=& $V --json inject --pid $game.Id|ConvertFrom-Json;if(-not$inject.ok){throw 'fixture injection failed'}
   for($i=0;$i-lt60;$i++){$splash=Vi $game.Id @('status');if($splash.class-eq'vi_windowfocus_splash'){break};Start-Sleep -Milliseconds 50}
   Check ($splash.class-eq'vi_windowfocus_splash'-and-not$splash.really_foreground) "window$arch splash is adopted in background first"
   $splashHwnd=[IntPtr][long]::Parse($splash.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
   # Injection/adoption can take seconds. The handler borrows only this owned
   # foreground queue, so re-establish its witnessed precondition at the trigger.
   Set-FocusSentinel $sentinel|Out-Null
   $negative=Start-Witness $exe $game.Id "protected$arch" 3500
   Post $splashHwnd 0x8014 $sentinel.hwnd
   $match=Wait-Log $log 'MAIN_VISIBLE hwnd=0x([0-9A-F]+)';$mainHwnd=[IntPtr][long]::Parse($match[1],[Globalization.NumberStyles]::HexNumber)
   for($i=0;$i-lt80;$i++){$adopted=Vi $game.Id @('status');if($adopted.hwnd-eq('0x{0:X}'-f$mainHwnd.ToInt64())){break};Start-Sleep -Milliseconds 40}
   Start-Sleep -Milliseconds 150
   $beforeKey=Vi $game.Id @('trace','key','insert');$actions=Count $log 'DI_ACTION'
   Vi $game.Id @('key','insert','150')|Out-Null;Start-Sleep -Milliseconds 100
   $afterKey=Vi $game.Id @('trace','key','insert')
   Check ($adopted.class-eq'vi_windowfocus_main'-and(Text $log)-match'MAIN_INACTIVE_BEFORE_VISIBLE') "window$arch real main is adopted after its unhooked hidden deactivation"
   Check ((Count $log 'DI_ACTION')-gt$actions-and$afterKey.channels.DirectInputBuffer.down_reads-gt$beforeKey.channels.DirectInputBuffer.down_reads) "window$arch main activation refresh resumes actual DirectInput consumer"
   $protected=Finish-Witness $negative;$recovered=Vi $game.Id @('status')
   Check ($protected.events-eq0-and$protected.samples-eq0-and-not$recovered.really_foreground) "window$arch independent witness sees zero real foreground gain during adoption"
   if(-not$WorkerCreateAttack){Check ($recovered.counters.act_refresh_delivered-ge2-and$recovered.counters.setfg_blocked-ge$(if($ReplaceChain){4}else{8})-and(Text $log)-match'ATTACK_CALLED') "window$arch recovered activation handler tried all blocked foreground APIs"}
   if($CreateWindowAttack-and-not$WorkerCreateAttack){Check (($recovered.counters.act_cbt_blocked-gt0-or$recovered.counters.create_visible_deferred-gt0)-and$recovered.counters.act_cbt_installed-ge2-and$recovered.counters.act_cbt_failed-eq0-and$recovered.counters.act_cbt_unhook_failed-eq0-and(Text $log)-match'ATTACK_CREATE_WINDOW') "window$arch scoped guard or deferred visible creation prevents popup activation"}
   if($WorkerCreateAttack){Check ($recovered.counters.create_visible_deferred-gt0-and(Text $log)-match'WORKER_CREATED.*attached=1.*noactivate_style=0 visible=1') "window$arch worker visible creation is deferred without persistent NOACTIVATE style"}
   if($ReplaceChain){Check ((Text $log)-match'CHAIN_REPLACED'-and(Count $log 'MAIN_ACTIVATE 1')-eq0-and$recovered.counters.act_refresh_cancelled-ge1-and($recovered.counters.act_refresh_delivered-$splash.counters.act_refresh_delivered)-eq1) "window$arch replacing the chain during first callback cancels the second replay"}
   Check ($recovered.counters.act_leaked-eq0) "window$arch unseen pre-adoption deactivation is not invented in leak counter"
   # Prove the leak counter describes only forwarding actually observed at our subclass.
   Vi $game.Id @('config','fakefocus=false')|Out-Null
   Post $mainHwnd 0x001C;Post $mainHwnd 0x0006
   Start-Sleep -Milliseconds 100;$leaked=Vi $game.Id @('status')
   Check ($leaked.counters.act_leaked-ge$recovered.counters.act_leaked+2-and(Text $log)-match'MAIN_ACTIVATEAPP 0') "window$arch observed forwarded deactivation increments leak counter"
   $actions=Count $log 'DI_ACTION';$activation=Vi $game.Id @('activate')
   Vi $game.Id @('key','insert','100')|Out-Null;Start-Sleep -Milliseconds 100
   Check ($activation.ok-and$activation.posts_activation-eq$false-and(Count $log 'DI_ACTION')-eq$actions) "window$arch explicit activate remains virtual-only without replaying messages"
   # Explicit fixture recovery returns the consumer to active, then protected deactivation is swallowed.
   Set-FocusSentinel $sentinel|Out-Null
   Post $mainHwnd 0x001C ([IntPtr]1);Post $mainHwnd 0x0006 ([IntPtr]1);Start-Sleep -Milliseconds 100
   $safeBefore=Vi $game.Id @('status');Post $mainHwnd 0x001C;Post $mainHwnd 0x0006;Start-Sleep -Milliseconds 100
   $safeAfter=Vi $game.Id @('status');$actions=Count $log 'DI_ACTION';Vi $game.Id @('key','insert','100')|Out-Null;Start-Sleep -Milliseconds 100
   Check ($safeAfter.counters.act_swallowed-ge$safeBefore.counters.act_swallowed+2-and$safeAfter.counters.act_leaked-eq$safeBefore.counters.act_leaked-and(Count $log 'DI_ACTION')-gt$actions) "window$arch protected deactivation stays swallowed and consumer works"
   # Same activation handler really can gain foreground when the policy is explicitly disabled.
   Set-FocusSentinel $sentinel|Out-Null;Vi $game.Id @('config','blockfocus=false')|Out-Null
   if($WorkerCreateAttack){Post $mainHwnd 0x8016;Start-Sleep -Milliseconds 40}
   $positiveWatcher=Start-Witness $exe $game.Id "unblocked$arch" 1000
   Post $mainHwnd 0x001C ([IntPtr]1);Post $mainHwnd 0x0006 ([IntPtr]1)
   $positive=Finish-Witness $positiveWatcher
   Check (($positive.events-gt0-or$positive.samples-gt0)-and$positive.held_ms-ge100-and$positive.ended_target) "window$arch identical handler is a genuine held foreground positive with policy disabled"
   $restore=Vi $game.Id @('json',('{"cmd":"restore-foreground","hwnd":"'+('0x{0:X}'-f$sentinel.hwnd.ToInt64())+'"}'))
   Vi $game.Id @('config','blockfocus=true')|Out-Null;Vi $game.Id @('release')|Out-Null
   $evidence.Add(@{arch=$arch;create_window_attack=[bool]$CreateWindowAttack;worker_create_attack=[bool]$WorkerCreateAttack;ansi_create_attack=[bool]$AnsiCreateAttack;replace_chain=[bool]$ReplaceChain;splash=$splash;adopted=$adopted;before_key=$beforeKey;after_key=$afterKey;protected_witness=$protected;recovered=$recovered;leaked=$leaked;activate=$activation;safe_before=$safeBefore;safe_after=$safeAfter;positive_witness=$positive;restore=$restore})
   if($recovered.log_path){Copy-Item -LiteralPath $recovered.log_path -Destination "$Out\window$arch.dll.log"}
  }finally{
   if($game-and-not$game.HasExited){try{Vi $game.Id @('release')|Out-Null;Vi $game.Id @('config','blockfocus=true')|Out-Null;Vi $game.Id @('json',('{"cmd":"restore-foreground","hwnd":"'+('0x{0:X}'-f$sentinel.hwnd.ToInt64())+'"}'))|Out-Null}catch{};Stop-Process -Id $game.Id -Force -Confirm:$false}
  }
 }
}finally{foreach($worker in $owned){if(-not$worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false}};Close-FocusSentinel $sentinel;$lockText=Get-Content -LiteralPath $lock -Raw;if($lockText.Contains($token)){Remove-Item -LiteralPath $lock};@{checks=$checks;evidence=$evidence}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json"}
$failed=@($checks|Where-Object {-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
