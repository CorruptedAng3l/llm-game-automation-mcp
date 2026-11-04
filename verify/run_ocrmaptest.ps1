# Native OCR/text map contract. All19 lower writers remain terminal.
# Unchanged fixture for actual before/after; no expected-red inversion.
param([Parameter(Mandatory=$true)][string]$RepoRoot,[string]$CliDir='', [string]$Out="$PSScriptRoot\_out\ocr_map",[switch]$UpscaleOnly)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliDir).Path)
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must remain under private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r1_tail.cs"}
if(-not('ViR1Native'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViR1Native{
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
}
'@}
$checks=[Collections.Generic.List[object]]::new();$arches=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$processes=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new()
$complete=$false;$instrument=$true;$current=$null;$token=0;$serial=0;$problem=$null
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name"}
function Require([bool]$Good,[string]$Name){if(-not$Good){throw $Name}}
function Pin([string]$Path){$pins.Add(@{path=$Path;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath $Path).Length})}
function Rows([string]$Path){if(Test-Path -LiteralPath $Path){foreach($line in ((Get-Content -LiteralPath $Path -Raw)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}}
function Live($T){$r=ReadRetainedImage $T.process $T.path $T.created $T.retained_handle;$T.latest=$r;Require $r.matched 'fresh exact retained kernel fixture identity'}
function NewTracked($P,[string]$Image){
 # Record the original returned object BEFORE any metadata check can throw.
 $t=@{process=$P;pid=0;path=$Image;created=[long]0;retained_handle=[long]0;cache=$null;cleanup=$null;startup_error=$null};$processes.Add($t)
 try{$t.pid=$P.Id;$t.retained_handle=$P.Handle.ToInt64()}catch{$t.startup_error=$_.Exception.Message};return $t
}
function ValidateTracked($T){Require ($T.retained_handle-ne0-and-not$T.startup_error) 'original process handle retained';$T.created=$T.process.StartTime.ToUniversalTime().Ticks;$seen=ObserveStartupImage $T.process $T.path $T.created $T.retained_handle;$T.startup=$seen;Require $seen.matched 'exact owned startup image/PID/creation';$T.cache=New-ViStrictCliStartupCache (ReadRetainedImage $T.process $T.path $T.created $T.retained_handle) $T.retained_handle ([uint32]$T.pid) $T.created $T.path}
function Finish($T){if(-not$T-or$T.cleanup){return};try{$T.cleanup=AccountOrReapCli $T.process $T.path $T.created $T.retained_handle ([uint32]$T.pid) $T.cache}catch{$T.cleanup=@{completion_owned=$false;reaped=$false;termination_attempted=$false;error=$_.Exception.Message}};if(-not$T.cleanup.completion_owned-or$T.cleanup.error){$script:instrument=$false;Write-Warning ('Owned cleanup/accounting failed: '+$T.cleanup.error)}}
function RunOwned([string]$Image,[string[]]$Arguments,[int]$Budget=10000,[Threading.EventWaitHandle]$StartupGate=$null){
 $Image=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Image).Path)
 $script:serial++;$prefix=Join-Path $run ('call_'+$serial);$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Image;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$run;$si.Environment['TEMP']="$run\temp";$si.Environment['TMP']="$run\temp";foreach($arg in $Arguments){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;$t=$null;$outTail=$null;$errTail=$null;$record=@{image=$Image;arguments=$Arguments;exit=$null;stdout='';stderr='';cleanup=$null};$calls.Add($record)
 try{Require ($p.Start()) 'owned compiler/CLI started';$t=NewTracked $p $Image;$outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError);ValidateTracked $t;if($StartupGate){Require ($StartupGate.Set()) 'release private startup gate only after strict live kernel identity'};Require ($p.WaitForExit($Budget)) 'owned compiler/CLI bounded exit';$proof=Get-ViExitedCliAccounting $t.retained_handle ([uint32]$t.pid) $t.created $Image $t.cache;Require (Test-ViExitedCliAccounting $proof) 'readonly original-handle CLI exit accounting';$record.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);$record.exit_accounting=$proof
  $null=$outTail.Wait(500);$null=$errTail.Wait(500);$record.stdout=$outTail.Text();$record.stderr=$errTail.Text();$record.stdout_complete=$outTail.Complete;$record.stderr_complete=$errTail.Complete
  Require ($outTail.Complete-and$errTail.Complete-and-not$outTail.Error-and-not$errTail.Error-and$outTail.Characters-le65536-and$errTail.Characters-le65536) 'bounded complete separate stdout/stderr'
  return $record
 }finally{try{if($t){Finish $t;$record.cleanup=$t.cleanup}}finally{
   if($outTail){$null=$outTail.Wait(500);$record.stdout=$outTail.Text()};if($errTail){$null=$errTail.Wait(500);$record.stderr=$errTail.Text()};[IO.File]::WriteAllText($prefix+'.stdout.log',$record.stdout);[IO.File]::WriteAllText($prefix+'.stderr.log',$record.stderr);if($t){$t.process.Dispose();$t.Remove('process')};$p.Dispose()
  }}
}
function Invoke-ViOcrMapCli([string[]]$Arguments){Live $current;$r=RunOwned $current.cli (@('--json','--pid',[string]$current.pid)+$Arguments);$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});Require ($lines.Count-eq1) 'one CLI JSON record';$r.reply=$lines[0]|ConvertFrom-Json;return $r}
function Status{$r=Invoke-ViOcrMapCli @('status');Require ($r.exit-eq0-and$r.reply.ok) 'actual status reply';return $r.reply}
function Config([uint64]$Hwnd){$j=@{cmd='config';hwnd=('0x{0:X}'-f$Hwnd);fakefocus=$false;blockfocus=$false;autoactivate=$false;isolate=$true;cursor='auto'}|ConvertTo-Json -Compress;$r=Invoke-ViOcrMapCli @('json',$j);Require ($r.exit-eq0-and$r.reply.ok) 'exact owned target configured';$r=Invoke-ViOcrMapCli @('json','{"cmd":"readiness"}');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.hook_ready-and$r.reply.window_ready-and$r.reply.queue_ready-and$r.reply.cursor_hooks_ready) 'full actual hook/window/queue/cursor readiness'}
function Control([uint32]$Action){Live $current;[uint32]$pidValue=0;$h=[IntPtr]::new([long][uint64]$current.ready.a);Require ([ViR1Native]::GetWindowThreadProcessId($h,[ref]$pidValue)-eq$current.ready.thread-and$pidValue-eq$current.pid) 'exact live own control HWND/PID/thread';$script:token++;$mine=$token;Require ([ViR1Native]::PostMessageW($h,0x80A0,[UIntPtr]::new([uint64]$Action),[IntPtr]::new([long]$mine))) 'owned tokened control accepted';$until=[Environment]::TickCount64+4000;do{$row=@(Rows $current.log|Where-Object {$_.event-eq'snapshot'-and$_.token-eq$mine})|Select-Object -Last 1;if($row){$current.observations.Add($row);return $row};if($current.process.HasExited){throw 'fixture exited awaiting control'};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw 'owned control receipt unavailable'}
function StartFixture([string]$Exe,[string]$CliPath,[string]$DllPath,[string]$Name){
 $log=Join-Path $run ($Name+'.jsonl');$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;[void]$si.ArgumentList.Add($log);$p=[Diagnostics.Process]::new();$p.StartInfo=$si;Require ($p.Start()) 'owned offscreen fixture started';$t=NewTracked $p $Exe;$script:current=$t;$t.cli=$CliPath;$t.log=$log;$t.keybd_count=0;$t.observations=[Collections.Generic.List[object]]::new();ValidateTracked $t
 $until=[Environment]::TickCount64+4000;do{$ready=@(Rows $log|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
 $guard=@(Rows $log|Where-Object event -eq guards)|Select-Object -First 1;Require ($ready-and$ready.pid-eq$p.Id-and$ready.terminal_spies-and$ready.modeled_foreground-and$ready.private_clip_only-and$guard.verified-and$guard.terminal_writes-eq19-and-not$guard.native_writer_called-and-not$guard.keybd_event_probed) 'terminal lower spies/private clip/independent foreground fixture prerequisite';Require ($ready.owned_print_surrogate-eq$true) 'owned memory PrintWindow backend installed';$t.ready=$ready
 $inject=Invoke-ViOcrMapCli @('inject','--dll',$DllPath,'--hooks-only');Require ($inject.exit-eq0-and$inject.reply.ok) 'actual chosen DLL hooks-only injection';Config ([uint64]$ready.a);return $t
}
function StopCurrent{if($script:current){try{Finish $script:current}finally{$script:current.process.Dispose();$script:current.Remove('process');$script:current=$null}}}
function MapSnapshot{$s=Control 0;$row=@(Rows $current.log|Where-Object {$_.event-eq'map_mouse'-and$_.token-eq$s.token})|Select-Object -Last 1;Require ($row-and$row.native_printwindow_called-eq$false) 'actual owned memory-backend/mouse snapshot';return $row}
function SeedMap([string]$Tag){
 $path=Join-Path $run ($current.arch+'.'+$Tag+'.png');$before=MapSnapshot
 $r=Invoke-ViOcrMapCli @('capture',$path,'--method','printwindow','--region','80,60,320,240','--scale','0.5');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.w-eq160-and$r.reply.h-eq120-and$r.reply.capture_space_updated-eq$true) 'actual cropped/scaled screenshot established capture map'
 $b=[Drawing.Bitmap]::new($path);try{$pixel=$b.GetPixel(5,5);Require ($b.Width-eq160-and$b.Height-eq120-and$pixel.R-eq30-and$pixel.G-eq80-and$pixel.B-eq150) 'independent PNG known-source blue-interior witness'}finally{$b.Dispose()}
 $after=MapSnapshot;Require ($after.print_callbacks-gt$before.print_callbacks) 'actual surrogate callback progressed';return $r
}
function ProbeMap([string]$Name){$before=MapSnapshot;$r=Invoke-ViOcrMapCli @('mouse','40','30','--capture-space','--steps','1');$after=MapSnapshot;Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.x-eq160-and$r.reply.y-eq120-and$after.moves-gt$before.moves-and$after.x-eq160-and$after.y-eq120) $Name;return @{reply=$r.reply;before=$before;after=$after}}
# Same approved owned fixture/lifecycle; new capture outcomes are semantic Checks.
function Invoke-ViUpscaleCase([int]$Scale,[string]$ReferenceHash){
 $name="$($current.arch) native scale$Scale";$path=Join-Path $run ($current.arch+'.upscale'+$Scale+'.png');$before=MapSnapshot
 $r=Invoke-ViOcrMapCli @('capture',$path,'--method','printwindow','--region','80,60,320,240','--scale',[string]$Scale);$after=MapSnapshot
 $captureOk=$r.exit-eq0-and$r.reply.ok-eq$true;$geometry=$captureOk-and$r.reply.w-eq320*$Scale-and$r.reply.h-eq240*$Scale-and$r.reply.source_w-eq320-and$r.reply.source_h-eq240-and$r.reply.origin_x-eq80-and$r.reply.origin_y-eq60-and$r.reply.region_client_w-eq320-and$r.reply.region_client_h-eq240-and$r.reply.capture_space_updated-eq$true
 $hashOk=$captureOk-and$r.reply.hash_scope-eq'source_region_bgra'-and$r.reply.hash-match'^[0-9A-Fa-f]{16}$'-and(-not$ReferenceHash-or$r.reply.hash-eq$ReferenceHash)
 $pngOk=$false;$png=$null;$decodeError=$null
 if(Test-Path -LiteralPath $path){$bitmap=$null;try{$bitmap=[Drawing.Bitmap]::new($path);$p=$bitmap.GetPixel(10*$Scale,10*$Scale);$png=@{w=$bitmap.Width;h=$bitmap.Height;r=$p.R;g=$p.G;b=$p.B;a=$p.A};$pngOk=$bitmap.Width-eq320*$Scale-and$bitmap.Height-eq240*$Scale-and$p.R-eq30-and$p.G-eq80-and$p.B-eq150}catch{$decodeError=$_.Exception.Message}finally{if($bitmap){$bitmap.Dispose()}}}
 $callback=$captureOk-and$after.print_callbacks-gt$before.print_callbacks
 $mapped=$false;$probe=$null
 if($captureOk){$mapBefore=MapSnapshot;$move=Invoke-ViOcrMapCli @('mouse',[string](80*$Scale),[string](60*$Scale),'--capture-space','--steps','1');$mapAfter=MapSnapshot;$probe=@{reply=$move.reply;before=$mapBefore;after=$mapAfter};$mapped=$move.exit-eq0-and$move.reply.ok-eq$true-and$move.reply.x-eq160-and$move.reply.y-eq120-and$mapAfter.moves-gt$mapBefore.moves-and$mapAfter.x-eq160-and$mapAfter.y-eq120}
 Check $captureOk "$name capture succeeds (unsupported upscale is an ordinary contract failure)"
 Check $geometry "$name saved dimensions/client-region metadata are exact"
 Check ($captureOk-and$pngOk) "$name independent PNG dimensions and known-source interior bytes"
 Check $hashOk "$name source-region hash remains before output transforms"
 Check $callback "$name actual owned PrintWindow surrogate callback progressed"
 Check $mapped "$name actual saved output map reaches observed client160,120"
 return @{scale=$Scale;completed=$true;capture=$r.reply;exit=$r.exit;before=$before;after=$after;png=$png;decode_error=$decodeError;map_probe=$probe;healthy=$captureOk-and$geometry-and$pngOk-and$hashOk-and$callback-and$mapped}
}
try{
 Add-Type -AssemblyName System.Drawing
 foreach($f in @($PSCommandPath,"$PSScriptRoot\vi_r1_identity.ps1","$PSScriptRoot\vi_exited_cli_accounting.ps1","$PSScriptRoot\vi_r1_lifecycle.ps1","$PSScriptRoot\..\test\vi_r1_tail.cs","$PSScriptRoot\..\test\vi_ocrmap.cpp")){Pin $f}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry)
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder)
   $cli=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'}))).Path);$dll=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'}))).Path);Pin $cli;Pin $dll
   $mh=Join-Path $RepoRoot "build\$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'};$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$exe=Join-Path $folder 'vi_ocrmap.exe';$batch=Join-Path $folder 'build.cmd'
   @"
@echo off
call "$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_ocrmap.cpp" /Fo:"$folder\fixture.obj" /Fe:"$exe" /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'both-arch W4/WX compile zero warnings';Pin $exe
   $current=StartFixture $exe $cli $dll ($arch+'.target');$current.arch=$arch;$r=Invoke-ViOcrMapCli @('config','msgs=on','raw=off');Require ($r.exit-eq0-and$r.reply.ok) 'message observer channel selected'
   if($UpscaleOnly){$entry.upscale=[Collections.Generic.List[object]]::new();$one=Invoke-ViUpscaleCase 1 '';$entry.upscale.Add($one);Require $one.healthy 'scale1 full capture/PNG/map prerequisite before attributing upscale failures';foreach($scale in @(2,4)){$case=Invoke-ViUpscaleCase $scale ([string]$one.capture.hash);$entry.upscale.Add($case)}}else{
   $entry.control=SeedMap initial;$entry.initial=ProbeMap "$arch cropped screenshot mapping control reaches client160,120"
   $entry.seed_ocr=SeedMap before_ocr;$r=Invoke-ViOcrMapCli @('ocr','--method','printwindow','--lines-only');Require ($r.exit-eq0-and$r.reply.ok) 'actual CLI OCR completed (zero text allowed)';$entry.ocr=$r.reply;Check ($r.reply.capture_space_updated-eq$false) "$arch OCR explicitly confirms no saved-map update";$entry.after_ocr=ProbeMap "$arch OCR leaves original screenshot map and observed coordinates unchanged"
   $entry.seed_text=SeedMap before_text;$r=Invoke-ViOcrMapCli @('click-text','__VI_OCRMAP_ABSENT_638D1C528BFF__','--exact','--optional','--timeout','0','--method','printwindow');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.skipped-eq$true-and$r.reply.clicked-eq$false) 'actual missing-text scan completed without clicking';$entry.text=$r.reply;Check ($r.reply.capture_space_updated-eq$false) "$arch text search explicitly confirms no saved-map update";$entry.after_text=ProbeMap "$arch optional text miss leaves original screenshot map and observed coordinates unchanged"
   }
   $entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}finally{StopCurrent}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2
 if($UpscaleOnly){$complete=$complete-and@($arches|Where-Object {$_.upscale.Count-eq3-and@($_.upscale|Where-Object completed -eq $true).Count-eq3}).Count-eq2}
}catch{$problem=$_.Exception.Message;Check $false "setup: $problem"}
@{schema=1;completed=$complete;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;checks=$checks;arches=$arches;calls=$calls;processes=$processes;pins=$pins;error=$problem;failure_is_expected_inversion=$false;upscale_mode=[bool]$UpscaleOnly;native_os_input_tested=$false;backend='owned lower PrintWindow memory-DC checker surrogate';native_printwindow_tested=$false;tool_issues=@()}|ConvertTo-Json -Depth 35|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $(@($checks|Where-Object pass -eq $true).Count) PASS $(@($checks|Where-Object pass -eq $false).Count) FAIL $run";if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
