# Recenter must never turn a requested overlay coordinate into a world/centre click.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\clickpoint",[switch]$KeepGoing)
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
if(-not('ViClickPointNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViClickPointNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'}
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=@();$caseEvidence=$null
function Check($ok,$name){$checks.Add(@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not$ok-and-not$KeepGoing){throw $name}}
function Vi($GamePid,[string[]]$Arguments){
 $raw=@(& $V --json --pid $GamePid @Arguments);$code=$LASTEXITCODE
 $record=@{arguments=$Arguments;exit=$code;received_tick=[Environment]::TickCount64;raw_lines=$raw;reply=$null}
 $script:caseEvidence.replies.Add($record)
 # Preserve even a partial/native-error reply before parsing or any assertion.
 $script:evidence|ConvertTo-Json -Depth 16|Set-Content "$Out\partial_replies.json" -Encoding utf8NoBOM
 $reply=$raw|ConvertFrom-Json;$record.reply=$reply;return $reply
}
function Text($Target){Get-Content -LiteralPath $Target.log -Raw}
function Count($Target,$Pattern){[regex]::Matches((Text $Target),$Pattern).Count}
function Control($Target,$Message,$Value,$Pattern){
 $old=Count $Target $Pattern;[ViClickPointNative]::PostMessage($Target.h,0x8000+$Message,[IntPtr]$Value,[IntPtr]::Zero)|Out-Null
 for($i=0;$i-lt60;$i++){
  if((Count $Target $Pattern)-gt$old){
   if($Message-eq2-and$Value-eq0){
    $off=[regex]::Matches((Text $Target),'RECENTER off generation=(\d+)');if(-not$off.Count){throw 'fixture lacks worker OFF acknowledgement support'}
    $generation=$off[$off.Count-1].Groups[1].Value;$until=[Environment]::TickCount64+2000
    do{if((Text $Target)-match("RECENTER_IDLE generation="+$generation+' native_calls=(\d+)')){$script:caseEvidence.off_acks.Add(@{generation=$generation;native_calls=$Matches[1];observed_tick=[Environment]::TickCount64});return};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$until)
    throw "worker never acknowledged recenter OFF generation $generation"
   }
   return
  };Start-Sleep -Milliseconds 20
 };throw "control not received: $Pattern"
}
try{foreach($arch in @('','32')){
 $caseEvidence=@{arch=$arch;replies=[Collections.Generic.List[object]]::new();off_acks=[Collections.Generic.List[object]]::new()};$evidence.Add($caseEvidence)
 $log="$Out\point$arch.log";$worker=Start-Process -FilePath "$D\vi_channelprobe$arch.exe" -ArgumentList "`"$log`"" -WindowStyle Hidden -PassThru;$owned+=$worker
 $inject=& $V --json inject --pid $worker.Id|ConvertFrom-Json;if(-not$inject.ok){throw 'injection failed'}
 for($i=0;$i-lt50;$i++){$status=Vi $worker.Id @('status');if($status.hwnd-ne'0x0'){break};Start-Sleep -Milliseconds 50}
 if($status.really_foreground){throw 'synthetic unexpectedly foreground'}
 $target=@{pid=$worker.Id;h=[IntPtr][long]::Parse($status.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber);log=$log}
 if((Text $target)-notmatch'RECENTER_GUARD_VERSION 1 '){throw 'refusing recenter controls: fixture lacks fail-closed guard'}
 $forced=Vi $worker.Id @('config','cursor=virtual')
 Check ($forced.ok-and$forced.cursor_mode-eq1-and$forced.effective_cursor-eq'virtual') "point$arch fixture explicitly requires forced virtual cursor"
 Control $target 5 1 'OVERLAY_MODE 1'
 $normal=Vi $worker.Id @('click','left','80','70','--settle-ms','100')
 Check ($normal.ok-and$normal.x-eq80-and$normal.y-eq70-and$normal.last_press_x-eq80-and$normal.button_down_count-eq1) "point$arch normal click records actual press snapshot"
 Control $target 2 1 'RECENTER on';$before=Count $target 'OVERLAY_CLICK_ACCEPT'
 $drift=Vi $worker.Id @('click','left','80','70','--settle-ms','200');$driftExit=$LASTEXITCODE
 Check (-not$drift.ok-and$driftExit-ne0-and(Count $target 'OVERLAY_CLICK_ACCEPT')-eq$before) "point$arch game recenter during preparation refuses click before any press"
 Check ($drift.requested_x-eq80-and$drift.resolved_x-eq80-and$drift.clamped_x-eq80-and$drift.applied_x-eq[int]($status.client_w/2)-and$drift.button_down_count-eq0) "point$arch failure reports requested resolved clamped and actual cursor separately"
 $down=Vi $worker.Id @('mousedown','left','80','70','--settle-ms','200');$state=Vi $worker.Id @('status')
 Check (-not$down.ok-and1-notin$state.virtual_down-and(Count $target 'OVERLAY_CLICK_ACCEPT')-eq$before) "point$arch recentered mousedown leaves no held button"
 Control $target 2 0 'RECENTER off'
 $clamp=Vi $worker.Id @('click','left','9999','9999')
 Check ($clamp.ok-and$clamp.requested_x-eq9999-and$clamp.resolved_x-eq9999-and$clamp.clamped_x-eq$status.client_w-1-and$clamp.applied_x-eq$clamp.clamped_x) "point$arch legal client clamp remains successful and explicit"
 $norm=Vi $worker.Id @('json','{"cmd":"click","x":0.25,"y":0.25,"norm":true}')
 Check ($norm.ok-and$norm.requested_x-eq0.25-and$norm.resolved_x-eq[math]::Round($status.client_w/4)-and$norm.applied_x-eq$norm.clamped_x) "point$arch normalized request compares resolved client coordinate"
 Control $target 14 1 'RECENTER_AFTER_CLICK_MODE 1';$before=Count $target 'OVERLAY_CLICK_ACCEPT'
 $gap=Vi $worker.Id @('json','{"cmd":"click","x":80,"y":70,"count":2,"ms":60,"gap":200}')
 $released=Vi $worker.Id @('trace','key','lbutton')
 Check (-not$gap.ok-and$gap.button_down_count-eq1-and$gap.last_press_x-eq80-and(Count $target 'OVERLAY_CLICK_ACCEPT')-eq$before+1-and-not$released.virtual_down) "point$arch repeat gap recenter stops before second press and first press is released"
 Control $target 2 0 'RECENTER off'
 $driftBefore=Vi $worker.Id @('status');$pressesBefore=Count $target 'OVERLAY_CLICK_ACCEPT'
 $after=Vi $worker.Id @('click','left','80','70','--settle-ms','100')
 $caseEvidence.after=$after
 $until=[Environment]::TickCount64+2000
 do{$presses=[regex]::Matches((Text $target),'OVERLAY_CLICK_ACCEPT lp=(-?\d+),(-?\d+) cursor=(-?\d+),(-?\d+)');if($presses.Count-gt$pressesBefore){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 $actual=if($presses.Count-gt$pressesBefore){$presses[$pressesBefore]}else{$null}
 $actualPoint=$actual-and$actual.Groups[1].Value-eq'80'-and$actual.Groups[2].Value-eq'70'-and$actual.Groups[3].Value-eq'80'-and$actual.Groups[4].Value-eq'70'
 Check ($after.ok-and$after.x-eq80-and$after.y-eq70-and$after.applied_x-eq80-and$after.applied_y-eq70-and$after.last_press_x-eq80-and$after.last_press_y-eq70-and$after.button_down_count-eq1-and$presses.Count-eq$pressesBefore+1-and$actualPoint) "point$arch later game cursor drift does not overwrite the reported or actual press point"
 $centreX=[math]::Floor($status.client_w/2);$centreY=[math]::Floor($status.client_h/2);$until=[Environment]::TickCount64+2000
 do{$recentered=Vi $worker.Id @('status');if($recentered.vcur_x-eq$centreX-and$recentered.vcur_y-eq$centreY-and$recentered.counters.scur-gt$driftBefore.counters.scur){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 $caseEvidence.recenter_observation=@{reply_snapshot_already_centred=($after.current_x-eq$centreX-and$after.current_y-eq$centreY);observed_tick=[Environment]::TickCount64;before=$driftBefore;after=$recentered;meaning='reply fields are the serialization snapshot; later status independently observes guarded recenter, without assuming it completed by the reply'}
 Check ($recentered.vcur_x-eq$centreX-and$recentered.vcur_y-eq$centreY-and$recentered.counters.scur-gt$driftBefore.counters.scur) "point$arch actual guarded recenter is observed independently of reply timing"
 Control $target 14 0 'RECENTER_AFTER_CLICK_MODE 0';Control $target 2 0 'RECENTER off'
 $paused=Vi $worker.Id @('click','left','80','70')
 Check ($paused.ok-and$paused.x-eq80-and$paused.current_x-eq80) "point$arch paused recenter normal positive succeeds"
 foreach($entry in @{status=$status;normal=$normal;drift=$drift;drift_exit=$driftExit;down=$down;clamp=$clamp;norm=$norm;gap=$gap;released=$released;paused=$paused}.GetEnumerator()){$caseEvidence[$entry.Key]=$entry.Value}
 Stop-Process -Id $worker.Id -Force -Confirm:$false
}}
finally{foreach($worker in $owned){if(-not$worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false}};@{checks=$checks;evidence=$evidence}|ConvertTo-Json -Depth 14|Set-Content "$Out\result.json"}
$failed=@($checks|Where-Object {-not$_.pass}).Count
"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
