# Real owned render loops: pre-phase 600ms remains visible; in-phase 600ms must still fail 250ms.
param([string]$Out="$PSScriptRoot\..\build\test\capturetiming")
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out|Out-Null
$env:TEMP=Join-Path $root 'build\temp';$env:TMP=$env:TEMP
. "$PSScriptRoot\vi_capturetiming.ps1"
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViTimingNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);}'
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=@()
function Check($ok,$name){$checks.Add(@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not$ok){throw $name}}
function Text($t){Get-Content -LiteralPath $t.log -Raw}
function Post($t,$message,$value){[uint32]$owner=0;[ViTimingNative]::GetWindowThreadProcessId($t.h,[ref]$owner)|Out-Null;if($owner-ne$t.p.Id){throw 'unowned timing HWND'};if(-not[ViTimingNative]::PostMessage($t.h,$message,[IntPtr]::new($value),[IntPtr]::Zero)){throw 'post failed'}}
function Ack($t,$label,$token){$until=[Environment]::TickCount64+3000;do{if((Text $t)-match"CAPTURE_TIMING_$label token=$token "){return};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "missing $label token=$token"}
try{foreach($arch in @('','32')){foreach($api in @('d3d12','gl','vk')){
 $name="$api$arch";$log=Join-Path $Out "$name.log";$extra=if($api-eq'vk'){' --wait-vinput 20000'}else{''}
 $p=Start-Process -FilePath "$root\dist\vi_$($api)test$arch.exe" -ArgumentList "`"$log`" -28000 600 820 480$extra" -WindowStyle Hidden -PassThru;$owned+=,$p
 $v="$root\dist\vinput.exe";$inject=&$v --json inject --pid $p.Id|ConvertFrom-Json
 Check ($LASTEXITCODE-eq0-and$inject.ok) "$name inject"
 $until=[Environment]::TickCount64+6000;do{$status=&$v --pid $p.Id status|ConvertFrom-Json;if($status.frames-gt3-and$status.frame_api-eq$api){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
 Check ($status.frames-gt3-and$status.frame_api-eq$api-and-not$status.really_foreground) "$name actual native background render loop"
 $t=@{p=$p;h=[IntPtr]::new([Convert]::ToInt64($status.hwnd,16));log=$log}
 Post $t (0x8000+119) 600;Post $t (0x8000+118) 10;Ack $t END 10
 $old=([regex]::Matches((Text $t),'maxms=([\d.]+)')|ForEach-Object{[double]$_.Groups[1].Value}|Measure-Object -Maximum).Maximum
 Check ($old-ge590) "$name RED old lifetime oracle includes actual pre-phase 600ms"
 Post $t (0x8000+117) 11;Ack $t START 11;Start-Sleep -Milliseconds 100;Post $t (0x8000+118) 11;Ack $t END 11
 $clean=Get-ViCaptureTiming (Text $t) 11
 Check ($clean.samples-ge1-and$clean.worst_ms-lt250) "$name GREEN acknowledged clean phase excludes prior stall"
 Post $t (0x8000+117) 12;Ack $t START 12;Post $t (0x8000+119) 600;Post $t (0x8000+118) 12;Ack $t END 12
 $bad=Get-ViCaptureTiming (Text $t) 12
 Check ($bad.worst_ms-ge590-and-not($bad.worst_ms-lt250)) "$name RED actual in-phase 600ms still violates unchanged 250ms"
 $missing=$false;try{$null=Get-ViCaptureTiming (Text $t) 13}catch{$missing=$true};Check $missing "$name missing acknowledgement cannot pass"
 $evidence.Add(@{case=$name;old_max=$old;clean=$clean;stalled=$bad;status=$status})
 $p.Kill();$p.WaitForExit()
}}}finally{
 @{checks=@($checks.ToArray());evidence=@($evidence.ToArray())}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json"
 foreach($p in $owned){if(-not$p.HasExited){$p.Kill();$p.WaitForExit()}}
}
"$($checks.Count) PASS / 0 FAIL";$global:LASTEXITCODE=0
