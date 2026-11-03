# Both receiver windows are owned synthetic targets. Never sends OS input or activates a window.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\hwndownership",[switch]$ExpectRed)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$CliDir=(Resolve-Path $CliDir).Path;$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out|Out-Null;$env:TEMP="$root\build\temp";$env:TMP=$env:TEMP
. "$PSScriptRoot\vi_textlog.ps1"
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViHwndOwner{[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);}'
$checks=@();$records=@();$owned=@()
function Check($ok,$name){$script:checks+=@{pass=[bool]$ok;name=$name};"$(if($ok){'PASS'}else{'FAIL'}) $name"}
try{foreach($suffix in @('','32')){
 $v=Join-Path $CliDir "vinput$suffix.exe";$exe="$root\dist\vi_uitarget$suffix.exe";$alog="$Out\a$suffix.log";$blog="$Out\b$suffix.log"
 $a=Start-Process -FilePath $exe -ArgumentList "`"$alog`" -28000 600" -WindowStyle Hidden -PassThru;$owned+=,$a
 $b=Start-Process -FilePath $exe -ArgumentList "`"$blog`" -28000 1100" -WindowStyle Hidden -PassThru;$owned+=,$b
 $until=[Environment]::TickCount64+4000;do{$b.Refresh();$h=$b.MainWindowHandle;if($h-ne[IntPtr]::Zero){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
 [uint32]$owner=0;[ViHwndOwner]::GetWindowThreadProcessId($h,[ref]$owner)|Out-Null;if($owner-ne$b.Id){throw 'receiver ownership prerequisite unavailable'}
 $inject=&$v --json inject --pid $a.Id|ConvertFrom-Json;if(-not$inject.ok){throw 'owned injection failed'}
 $before=&$v --json --pid $a.Id status|ConvertFrom-Json
 $foreign=&$v --json --pid $a.Id config "hwnd=0x$($h.ToInt64().ToString('X'))"|ConvertFrom-Json;$foreignExit=$LASTEXITCODE
 $after=&$v --json --pid $a.Id status|ConvertFrom-Json
 Check (-not$foreign.ok-and$foreignExit-ne0-and$foreign.error-match'belong') "hwnd$suffix rejects other process before configuration"
 Check ($after.hwnd-eq$before.hwnd) "hwnd$suffix rejection preserves adopted window"
 $old=[regex]::Matches((Read-ViSharedText $blog),'MOVE 80 70 btn=0').Count
 $move=&$v --json --pid $a.Id mouse 80 70|ConvertFrom-Json;Start-Sleep -Milliseconds 150
 Check ([regex]::Matches((Read-ViSharedText $blog),'MOVE 80 70 btn=0').Count-eq$old) "hwnd$suffix virtual move never reaches other owned process"
 $restore=&$v --json --pid $a.Id config "hwnd=$($before.hwnd)"|ConvertFrom-Json
 $ownMove=&$v --json --pid $a.Id mouse 90 71|ConvertFrom-Json;Start-Sleep -Milliseconds 100
 Check ($restore.ok-and$ownMove.ok-and(Read-ViSharedText $alog)-match'MOVE 90 71 btn=0') "hwnd$suffix valid owned selection and real message delivery remain"
 $records+=@{arch=$suffix;injected_pid=$a.Id;receiver_pid=$b.Id;receiver_hwnd=('0x{0:X}'-f$h.ToInt64());before=$before;foreign=$foreign;foreign_exit=$foreignExit;after=$after;move=$move;restore=$restore}
 $a.Kill();$a.WaitForExit();$b.Kill();$b.WaitForExit()
}}finally{@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json";foreach($p in $owned){if(-not$p.HasExited){$p.Kill();$p.WaitForExit()}}}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL"
if($ExpectRed){if($failed-lt4){throw 'missing foreign-window red controls'};$global:LASTEXITCODE=0}else{if($failed){throw 'HWND isolation controls failed'};$global:LASTEXITCODE=0}
