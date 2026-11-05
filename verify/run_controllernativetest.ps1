# Actual XInput + actual MCP response image. No game/physical API is driven.
param([Parameter(Mandatory=$true)][string]$RepoRoot,[string]$CliDir='',[string]$McpRoot='', [string]$Out="$PSScriptRoot\_out\controller_native",[string[]]$OnlyCases=@())
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';$RepoRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepoRoot).Path)
if(-not$CliDir){$CliDir=Join-Path $RepoRoot 'dist'};$CliDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $CliDir).Path)
if(-not$McpRoot){$McpRoot=Join-Path $RepoRoot 'mcp'};$McpRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $McpRoot).Path)
$nodeCommand=Get-Command node -CommandType Application -ErrorAction Stop|Select-Object -First 1
$node=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $nodeCommand.Source).Path)
$outRoot=[IO.Path]::GetFullPath($Out);if(-not$outRoot.StartsWith($RepoRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Out must stay within private RepoRoot'}
$run=Join-Path $outRoot ('run_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $run,"$run\temp")
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r1_tail.cs"}
if(-not('ViControllerNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViControllerNative{
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
}
'@}
$checks=[Collections.Generic.List[object]]::new();$arches=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$processes=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new()
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

function Invoke-ViControllerCli([string[]]$Arguments){Live $current;$r=RunOwned $current.cli (@('--json','--pid',[string]$current.pid)+$Arguments);$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});Require ($lines.Count-eq1) 'one CLI JSON record';$r.reply=$lines[0]|ConvertFrom-Json;return $r}
function Control([uint32]$Action){Live $current;[uint32]$pidValue=0;$h=[IntPtr]::new([long][uint64]$current.ready.hwnd);Require ([ViControllerNative]::GetWindowThreadProcessId($h,[ref]$pidValue)-eq$current.ready.thread-and$pidValue-eq$current.pid) 'fresh own control HWND/PID/thread';$script:token++;$mine=$token;Require ([ViControllerNative]::PostMessageW($h,0x80B1,[UIntPtr]::new([uint64]$Action),[IntPtr]::new([long]$mine))) 'own control accepted';$end=[Environment]::TickCount64+4000;do{$row=@(Rows $current.log|Where-Object {$_.event-eq'control'-and$_.token-eq$mine})|Select-Object -Last 1;if($row){Require $row.ok 'actual control ACK';return $row};if($current.process.HasExited){throw 'fixture exited during control'};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);throw 'control ACK unavailable'}
function NeutralSetup{$r=Invoke-ViControllerCli @('pad','none','40','--lx','0','--ly','0','--rx','0','--ry','0','--lt','0','--rt','0');Require ($r.exit-eq0-and$r.reply.ok) 'explicit virtual-neutral setup accepted'}
function StartFixture([string]$Exe,[string]$CliPath,[string]$Dll,[string]$Name){
 $log=Join-Path $run ($Name+'.jsonl');$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;[void]$si.ArgumentList.Add($log);$p=[Diagnostics.Process]::new();$p.StartInfo=$si;Require ($p.Start()) 'owned controller fixture started';$t=NewTracked $p $Exe;$script:current=$t;$t.cli=$CliPath;$t.log=$log;$t.observations=[Collections.Generic.List[object]]::new();ValidateTracked $t
 $end=[Environment]::TickCount64+5000;do{$ready=@(Rows $log|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
 $guard=@(Rows $log|Where-Object event -eq guards)|Select-Object -First 1;Require ($ready-and$ready.pid-eq$p.Id-and$ready.actual_offscreen_checked-and$guard.safe-and$guard.terminal_desktop_writers-eq19-and$guard.terminal_rumble_enable-and-not$guard.rumble_enable_probed-and-not$guard.hardware_reader_called-and-not$guard.native_printwindow_called) 'terminal/no-hardware/offscreen prerequisites';$t.ready=$ready
 [uint32]$fg=0;[void][ViControllerNative]::GetWindowThreadProcessId([ViControllerNative]::GetForegroundWindow(),[ref]$fg);Require ($fg-ne$t.pid) 'actual foreground stays outside fixture'
 $r=Invoke-ViControllerCli @('inject','--dll',$Dll,'--hooks-only');Require ($r.exit-eq0-and$r.reply.ok) 'chosen DLL actually injected';$j=@{cmd='config';hwnd=('0x{0:X}'-f[uint64]$ready.hwnd);fakefocus=$false;blockfocus=$false;autoactivate=$false;isolate=$true}|ConvertTo-Json -Compress;$r=Invoke-ViControllerCli @('json',$j);Require ($r.exit-eq0-and$r.reply.ok) 'exact healthy target configured';$r=Invoke-ViControllerCli @('readiness');Require ($r.exit-eq0-and$r.reply.ok-and$r.reply.hook_ready-and$r.reply.window_ready-and$r.reply.queue_ready-and$r.reply.cursor_hooks_ready) 'actual full readiness';NeutralSetup;return $t
}
function StopCurrent{if($script:current){try{if(-not$current.process.HasExited){try{$null=Control 8;[void]$current.process.WaitForExit(500)}catch{}};Finish $script:current}finally{$script:current.process.Dispose();$script:current.Remove('process');$script:current=$null}}}
function Vector([string]$Direction,[string]$Speed,[double]$Rotation=0){$sign=@{'up'=@(0,1);'up-right'=@(1,1);'right'=@(1,0);'down-right'=@(1,-1);'down'=@(0,-1);'down-left'=@(-1,-1);'left'=@(-1,0);'up-left'=@(-1,1)}[$Direction];$m=if($Speed-eq'run'){32767}else{20000};$d=if($sign[0]-and$sign[1]){[Math]::Sqrt(2)}else{1};$a=$Rotation*[Math]::PI/180;return @{x=[int][Math]::Floor((($sign[0]*[Math]::Cos($a)+$sign[1]*[Math]::Sin($a))*$m/$d)+0.5);y=[int][Math]::Floor(((-$sign[0]*[Math]::Sin($a)+$sign[1]*[Math]::Cos($a))*$m/$d)+0.5);m=$m}}
function ImageFields([string]$Path){$bitmap=[Drawing.Bitmap]::new($Path);try{Require ($bitmap.Width-eq640-and$bitmap.Height-eq480) 'lossless full native model image';$bytes=[byte[]]::new(288);for($i=0;$i-lt$bytes.Length;$i++){$pixel=$bitmap.GetPixel($i,0);Require ($pixel.G-eq255-$pixel.R-and$pixel.B-eq90) 'encoded observation pixel integrity';$bytes[$i]=$pixel.R};Require ([Text.Encoding]::ASCII.GetString($bytes,0,8)-eq'MCPPAD01') 'actual observation image signature';$f=@();for($i=0;$i-lt35;$i++){$f+=[BitConverter]::ToUInt64($bytes,8+$i*8)};return @{version=$f[0];capture_id=$f[1];tick=$f[2];case_token=$f[3];std_samples=$f[4];ex_samples=$f[5];first_active=$f[6];last_active=$f[7];neutral_after=$f[8];std_rc=$f[9];ex_rc=$f[10];std_buttons=$f[11];std_lt=$f[12];std_rt=$f[13];std_lx=$f[14];std_ly=$f[15];std_rx=$f[16];std_ry=$f[17];std_active=$f[18];ex_active=$f[19];model=$f[22];display_x=[BitConverter]::ToInt64($bytes,8+23*8);display_y=[BitConverter]::ToInt64($bytes,8+24*8);ex_buttons=$f[26];ex_lt=$f[27];ex_rt=$f[28];ex_lx=$f[29];ex_ly=$f[30];ex_rx=$f[31];ex_ry=$f[32]}}finally{$bitmap.Dispose()}}
# Pure oracle over retained actual std/Ex rows. No input/process/window API calls.
function RotationEvidence($States,[string]$Source,$C,$Sdk){
 $e=@{fields_complete=$false;point_count=0;normalized=$false;winding_valid=$false;anchors_valid=$false;endpoint_valid=$false;dwell_valid=$false;sweep_deg=0;dwell_span_ms=$null;points=@();reason='no bounded pre-cleanup neutral witness'}
 $proof=@($Sdk.observed_neutral_before_cleanup|Where-Object source -eq $Source)
 if($proof.Count-ne1){return $e}
 $rows=@($States|Where-Object source -eq $Source|Sort-Object tick,sample)
 foreach($row in @($rows)+@($proof[0])){foreach($name in @('tick','sample','rc','buttons','lt','rt','lx','ly','rx','ry')){$v=$row.$name;if($null-eq$v-or-not($v-is[int]-or$v-is[long]-or$v-is[uint64])){$e.reason='missing/non-integral actual state field';return $e}}}
 $zero=$proof[0];if($zero.rc-ne0-or$zero.buttons-ne0-or$zero.lt-ne0-or$zero.rt-ne0-or$zero.lx-ne0-or$zero.ly-ne0-or$zero.rx-ne0-or$zero.ry-ne0){$e.reason='pre-cleanup neutral witness was not neutral';return $e}
 $x=if($C.args.stick-eq'left'){'lx'}else{'rx'};$y=if($C.args.stick-eq'left'){'ly'}else{'ry'};$ox=if($x-eq'lx'){'rx'}else{'lx'};$oy=if($y-eq'ly'){'ry'}else{'ly'};$e.axes=($x+'/'+$y)
 $e.fields_complete=$true;$bounded=@($rows|Where-Object {$_.tick-le$zero.tick});$active=@($bounded|Where-Object {$_.$x-ne0-or$_.$y-ne0})
 $points=[Collections.Generic.List[object]]::new();$normalized=$active.Count-gt0
 foreach($row in $active){$radius=[Math]::Sqrt([double]$row.$x*$row.$x+[double]$row.$y*$row.$y);if([Math]::Abs($radius-$C.rotation.radius)-gt2-or$row.$ox-ne0-or$row.$oy-ne0-or$row.buttons-ne0-or$row.lt-ne0-or$row.rt-ne0){$normalized=$false};if(-not$points.Count-or$points[$points.Count-1].$x-ne$row.$x-or$points[$points.Count-1].$y-ne$row.$y){$points.Add($row)}}
 $e.active_row_count=$active.Count;$e.point_count=$points.Count;$e.normalized=$normalized;$e.points=@($points.ToArray())
 $winding=$points.Count-ge2;$sweep=0.0;$maxStep=0.0
 for($i=1;$i-lt$points.Count;$i++){$a=$points[$i-1];$b=$points[$i];$cross=[double]$a.$x*$b.$y-[double]$a.$y*$b.$x;if([Math]::Sign($cross)-ne$C.rotation.winding){$winding=$false};$delta=([Math]::Atan2($b.$x,$b.$y)-[Math]::Atan2($a.$x,$a.$y))*180/[Math]::PI;while($delta-gt180){$delta-=360};while($delta-lt-180){$delta+=360};$sweep+=$delta;$maxStep=[Math]::Max($maxStep,[Math]::Abs($delta))}
 $e.sweep_deg=$sweep;$e.max_step_deg=$maxStep;$e.winding_valid=$winding-and$maxStep-le16-and[Math]::Abs($sweep-$C.rotation.sweep)-le0.25
 $anchors=$points.Count-eq$C.rotation.points
 foreach($anchor in $C.rotation.anchors){$i=[int]$anchor[0];if($i-ge$points.Count-or$points[$i].$x-ne$anchor[1]-or$points[$i].$y-ne$anchor[2]){$anchors=$false}}
 $e.anchors_valid=$anchors
 if($points.Count){$last=$points[$points.Count-1];$e.endpoint_tick=$last.tick;$e.endpoint_valid=$last.$x-eq$C.rotation.end_x-and$last.$y-eq$C.rotation.end_y;$neutral=@($bounded|Where-Object {$_.tick-gt$last.tick-and$_.buttons-eq0-and$_.lt-eq0-and$_.rt-eq0-and$_.lx-eq0-and$_.ly-eq0-and$_.rx-eq0-and$_.ry-eq0})|Select-Object -First 1;if($neutral){$span=[long]$neutral.tick-[long]$last.tick;$e.neutral_tick=$neutral.tick;$e.dwell_span_ms=$span;$e.dwell_valid=$e.endpoint_valid-and$span-ge$C.rotation.dwell-25-and$span-le$C.rotation.dwell+200}}
 $e.reason='actual bounded std/Ex trajectory only; no camera-angle/game-movement/prompt-acceptance claim';return $e
}
function DriveEvidence($States,[string]$Source,$C,$Sdk){
 $e=@{fields_complete=$false;ordered=$false;other_controls_neutral=$false;no_intermediate_neutral=$false;neutral_before_cleanup=$false;points=@()}
 $zero=@($Sdk.observed_neutral_before_cleanup|Where-Object source -eq $Source)
 if($zero.Count-ne1){return $e}
 $rows=@($States|Where-Object {$_.source-eq$Source-and$_.tick-le$zero[0].tick}|Sort-Object tick,sample)
 foreach($row in @($rows)+@($zero[0])){foreach($key in @('tick','sample','rc','buttons','lt','rt','lx','ly','rx','ry')){$v=$row.$key;if($null-eq$v-or-not($v-is[int]-or$v-is[long]-or$v-is[uint64])){return $e}}}
 $e.fields_complete=$true;$active=@($rows|Where-Object {$_.lx-ne0-or$_.lt-ne0-or$_.rt-ne0})
 $points=[Collections.Generic.List[object]]::new()
 foreach($row in $active){if(-not$points.Count-or$points[$points.Count-1].lx-ne$row.lx-or$points[$points.Count-1].lt-ne$row.lt-or$points[$points.Count-1].rt-ne$row.rt){$points.Add($row)}}
 $e.points=@($points.ToArray());$ordered=$points.Count-eq$C.drive_points.Count
 if($ordered){for($i=0;$i-lt$points.Count;$i++){$p=$points[$i];$expected=$C.drive_points[$i];if($p.lx-ne$expected[0]-or$p.lt-ne$expected[1]-or$p.rt-ne$expected[2]){$ordered=$false}}}
 $e.ordered=$ordered;$e.other_controls_neutral=@($rows|Where-Object {$_.buttons-ne0-or$_.ly-ne0-or$_.rx-ne0-or$_.ry-ne0}).Count-eq0
 if($active.Count){$first=$active[0].tick;$last=$active[$active.Count-1].tick;$e.no_intermediate_neutral=@($rows|Where-Object {$_.tick-gt$first-and$_.tick-lt$last-and$_.lx-eq0-and$_.lt-eq0-and$_.rt-eq0}).Count-eq0;$z=$zero[0];$e.neutral_before_cleanup=$z.tick-gt$last-and$z.rc-eq0-and$z.buttons-eq0-and$z.lx-eq0-and$z.ly-eq0-and$z.rx-eq0-and$z.ry-eq0-and$z.lt-eq0-and$z.rt-eq0}
 return $e
}
function CaseList{
 @{name='drive_phases';tool='pad_drive';args=@{phases=@(@{throttle=170;ms=400},@{throttle=120;steer=-24000;ms=400},@{brake=180;ms=400});brake_after=@{amount=200;ms=300}};drive_points=@(@(0,0,170),@(-24000,0,120),@(0,180,0),@(0,200,0));drive_ms=1500;image=$true;settle=100;state='action_reply'}
 @{name='drive_cancel';tool='pad_drive';args=@{throttle=180;steer=10000;ms=4000;async=$true;capture_after=$false};drive_points=,@(10000,0,180);drive_ms=4000;cancel=$true;image=$false}
 foreach($pair in @(@('up','walk'),@('right','run'),@('up-right','walk'),@('down-left','run'))){$direction=$pair[0];$speed=$pair[1];@{name="$direction.$speed";tool='pad_move';args=@{direction=$direction;speed=$speed;ms=250;capture_after=$false};vector=(Vector $direction $speed);image=$false}}
 @{name='button_settle';tool='pad';args=@{buttons='a';ms=150;capture_after=0;capture_settle_ms=200};button=4096;image=$true;settle=200;state='action_reply'}
 @{name='default_move_look';tool='pad_move';args=@{direction='up';ms=250};vector=(Vector up walk);image=$true;settle=100;state='action_reply'}
 @{name='present_fallback';tool='pad_move';args=@{direction='right';ms=250;capture_after=$true;capture_settle_ms=0};vector=(Vector right walk);image=$true;settle=0;fallback=$true;state='action_reply'}
 @{name='smooth';tool='pad_move';args=@{direction='right';ms=600;smooth=$true;capture_after=$false};vector=(Vector right walk);smooth=$true;image=$false}
 @{name='cancel';tool='pad_move';args=@{direction='right';ms=4000;smooth=$true;async=$true;capture_after=$false};cancel=$true;image=$false}
 @{name='held_look';tool='pad';args=@{buttons='a';hold=$true;capture_after=0;capture_settle_ms=100};button=4096;image=$true;settle=100;state='held_until_changed';held=$true}
 @{name='inverted_control';tool='pad_move';args=@{direction='up';ms=250;screen_rotation_deg=0;capture_after=0;capture_settle_ms=0};vector=(Vector up walk);model=180;display_sign=1;image=$true;settle=0;state='action_reply'}
 @{name='inverted_compensation';tool='pad_move';args=@{direction='up';ms=250;screen_rotation_deg=180;capture_after=0;capture_settle_ms=0};vector=(Vector up walk 180);model=180;display_sign=-1;image=$true;settle=0;state='action_reply'}
 @{name='rotate_cw_circle';tool='pad_rotate';args=@{stick='left';start_deg=0;direction='clockwise';turns=1;ms=2400;radius=32767;end_dwell_ms=150;capture_after=0;capture_settle_ms=100};rotation=@{points=25;radius=32767;winding=-1;sweep=360;dwell=150;rotation_ms=2400;end_x=0;end_y=32767;anchors=@(@(0,0,32767),@(6,32767,0),@(12,0,-32767),@(18,-32767,0),@(24,0,32767))};image=$true;settle=100;state='action_reply'}
 @{name='rotate_ccw_quarter';tool='pad_rotate';args=@{stick='right';start_deg=90;direction='counterclockwise';turns=0.25;ms=900;radius=20000;end_dwell_ms=150;capture_after=0;capture_settle_ms=100};rotation=@{points=7;radius=20000;winding=1;sweep=-90;dwell=150;rotation_ms=900;end_x=0;end_y=20000;anchors=@(@(0,20000,0),@(6,0,20000))};image=$true;settle=100;state='action_reply'}
 @{name='look_positive_calibration';tool='pad_look';args=@{degrees=90;calibration=@{rx=20000;degrees_per_second=60};capture_after=0;capture_settle_ms=100};look_rx=20000;look_ms=1500;image=$true;settle=100;state='action_reply'}
 @{name='look_inverted_calibration';tool='pad_look';args=@{degrees=90;calibration=@{rx=-20000;degrees_per_second=60};capture_after=0;capture_settle_ms=100};look_rx=-20000;look_ms=1500;image=$true;settle=100;state='action_reply'}
}
try{
 Add-Type -AssemblyName System.Drawing
 $plan=@(CaseList);if($OnlyCases.Count){$plan=@($plan|Where-Object {$_.name-in$OnlyCases});Require ($plan.Count-eq$OnlyCases.Count) 'every requested exact case exists'}
 Require ($plan.Count-gt0) 'nonempty actual controller plan'
 foreach($f in @($PSCommandPath,"$PSScriptRoot\vi_r1_identity.ps1","$PSScriptRoot\vi_exited_cli_accounting.ps1","$PSScriptRoot\vi_r1_lifecycle.ps1","$PSScriptRoot\..\test\vi_r1_tail.cs","$PSScriptRoot\..\test\vi_r2_spies.h","$PSScriptRoot\..\test\vi_controllercapture.cpp","$PSScriptRoot\..\test\controller_native_case.mjs",$node,"$McpRoot\build\index.js","$McpRoot\build\vinput.js","$McpRoot\build\exit.js","$McpRoot\node_modules\@modelcontextprotocol\sdk\dist\esm\client\stdio.js")){Pin $f};if(Test-Path -LiteralPath "$McpRoot\build\controller.js"){Pin "$McpRoot\build\controller.js"}
 foreach($arch in @('x64','x86')){
  $entry=@{arch=$arch;completed=$false;error=$null};$arches.Add($entry)
  try{
   $folder=Join-Path $run $arch;[void](New-Item -ItemType Directory -Force $folder);$cli=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'}))).Path);$dll=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath (Join-Path $CliDir $(if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'}))).Path);Pin $cli;Pin $dll
   $mh=Join-Path $RepoRoot "build\$arch\host_mh";$hde=if($arch-eq'x64'){'hde64'}else{'hde32'};$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'});$exe=Join-Path $folder 'vi_controllercapture.exe';$batch=Join-Path $folder 'build.cmd'
   @"
@echo off
call "$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"$RepoRoot\third_party\minhook\include" "$PSScriptRoot\..\test\vi_controllercapture.cpp" /Fo:"$folder\fixture.obj" /Fe:"$exe" /link /SUBSYSTEM:WINDOWS "$mh\buffer.obj" "$mh\hook.obj" "$mh\trampoline.obj" "$mh\$hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
   $entry.build=RunOwned $env:ComSpec @('/d','/c',$batch) 120000;Require ($entry.build.exit-eq0-and($entry.build.stdout+$entry.build.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'W4/WX actual fixture build zero warnings';Pin $exe
   $current=StartFixture $exe $cli $dll ($arch+'.target');$entry.ready=$current.ready
   foreach($c in $plan){$case=@{name="$arch.$($c.name)";completed=$false;error=$null;expected=$c};$cases.Add($case)
    try{
     NeutralSetup;$null=Control $(if($c.model-eq180){5}elseif($c.model-eq90){7}else{6});$arm=Control 1;$case.token=$arm.case_token
     $spec=@{repo_root=$RepoRoot;mcp_root=$McpRoot;cli_exe=$cli;pid=$current.pid;case_token=$arm.case_token;target_log=$current.log;png_path=(Join-Path $run ($case.name+'.png'));tool=$c.tool;arguments=$c.args;cancel_after_active=[bool]$c.cancel};$specPath=Join-Path $run ($case.name+'.case.json');$spec|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $specPath -Encoding utf8NoBOM
     $execution=RunOwned $node @("$PSScriptRoot\..\test\controller_native_case.mjs",$specPath) 85000;$case.execution=$execution;$line=@($execution.stdout-split'\r?\n'|Where-Object {$_-match'\S'});Require ($execution.exit-eq0-and$line.Count-eq1) 'bounded owned SDK case transport/cleanup';$sdk=$line[0]|ConvertFrom-Json;$case.sdk=$sdk;Require $sdk.completed 'SDK case/control cleanup complete';Check (-not$sdk.action.is_error-and$sdk.action.json.ok) "$($case.name) actual MCP action succeeds"
     $states=@(Rows $current.log|Where-Object {$_.event-eq'state'-and$_.case_token-eq$arm.case_token-and$_.rc-eq0});$case.samples=$states
     if($c.drive_points){$case.drive_evidence=@{};foreach($src in @('std','ex')){$ev=DriveEvidence $states $src $c $sdk;$case.drive_evidence[$src]=$ev;Check $ev.fields_complete "$($case.name) $src complete actual drive sample fields";Check $ev.ordered "$($case.name) $src independent requested throttle/brake/steer phase order";Check $ev.other_controls_neutral "$($case.name) $src all other controls neutral";Check $ev.no_intermediate_neutral "$($case.name) $src no observed inter-phase neutral gap";Check $ev.neutral_before_cleanup "$($case.name) $src final neutral before harness cleanup"};$drive=$sdk.action.json.pad_drive;Check ($drive.total_ms-eq$c.drive_ms-and$drive.game_movement_verified-eq$false-and$drive.stopped_verified-eq$false) "$($case.name) requested duration is not game motion or stopping proof"}
     if($c.rotation){$case.rotation_evidence=@{};foreach($src in @('std','ex')){$ev=RotationEvidence $states $src $c $sdk;$case.rotation_evidence[$src]=$ev;Check $ev.fields_complete "$($case.name) $src complete actual pre-cleanup sample fields";Check ($ev.point_count-eq$c.rotation.points) "$($case.name) $src every requested rotation point actually observed";Check $ev.normalized "$($case.name) $src normalized $($ev.axes) with all other controls neutral";Check $ev.winding_valid "$($case.name) $src ordered measured winding and sweep";Check $ev.anchors_valid "$($case.name) $src independently known start/cardinal/end vectors";Check ($ev.endpoint_valid-and$ev.dwell_valid) "$($case.name) $src observed endpoint dwell then fresh neutral before harness cleanup"};$rot=$sdk.action.json.pad_rotate;Check ($null-ne$rot-and$rot.stick-eq$c.args.stick-and$rot.rotation_ms-eq$c.rotation.rotation_ms-and$rot.end_dwell_ms-eq$c.rotation.dwell-and$rot.total_ms-eq$c.rotation.rotation_ms+$c.rotation.dwell-and$rot.requested_points-eq$c.rotation.points-and$rot.game_prompt_verified-eq$false) "$($case.name) one finite rotation receipt separates timing and denies game prompt verification";$case.camera_angle_measured=$false;$case.game_movement_measured=$false}
     if($c.vector){foreach($src in @('std','ex')){$matches=@($states|Where-Object {$_.source-eq$src-and$_.lx-eq$c.vector.x-and$_.ly-eq$c.vector.y-and$_.rx-eq0-and$_.ry-eq0-and$_.lt-eq0-and$_.rt-eq0-and$_.buttons-eq0});Check ($matches.Count-gt0) "$($case.name) $src actual normalized signed vector"};$m=[Math]::Sqrt($c.vector.x*$c.vector.x+$c.vector.y*$c.vector.y);Check ([Math]::Abs($m-$c.vector.m)-le1) "$($case.name) diagonal/cardinal magnitude within integer rounding"}
     if($c.look_rx){foreach($src in @('std','ex')){$observed=@($states|Where-Object {$_.source-eq$src-and$_.rx-eq$c.look_rx-and$_.ry-eq0-and$_.lx-eq0-and$_.ly-eq0-and$_.lt-eq0-and$_.rt-eq0-and$_.buttons-eq0});Check ($observed.Count-gt0) "$($case.name) $src actual calibrated RX sign with other controls neutral"};$turn=$sdk.action.json.pad_look;Check ($turn.applied_rx-eq$c.look_rx-and$turn.duration_ms-eq$c.look_ms-and$turn.requested_degrees-eq90-and$turn.predicted_degrees-eq90-and$turn.angle_verified-eq$false) "$($case.name) predicted duration/sign remains explicitly unverified angle"}
     if($c.button){foreach($src in @('std','ex')){Check (@($states|Where-Object {$_.source-eq$src-and$_.buttons-eq$c.button}).Count-gt0) "$($case.name) $src actual A press observed"}}
     if($c.smooth){foreach($src in @('std','ex')){$v=@($states|Where-Object source -eq $src|ForEach-Object {[Math]::Abs($_.lx)});$peak=-1;for($i=0;$i-lt$v.Count;$i++){if($v[$i]-eq20000){$peak=$i;break}};$middleBefore=$false;$middleAfter=$false;if($peak-ge0){for($i=0;$i-lt$peak;$i++){if($v[$i]-gt0-and$v[$i]-lt20000){$middleBefore=$true}};for($i=$peak+1;$i-lt$v.Count;$i++){if($v[$i]-gt0-and$v[$i]-lt20000){$middleAfter=$true}}};Check ($middleBefore-and$middleAfter) "$($case.name) $src actual ease-in and ease-out intermediates"}}
     if($c.cancel){Check ($sdk.cancel.json.ok-and-not$sdk.active_observation_missing-and-not$sdk.neutral_observation_missing) "$($case.name) observed active then exact job cancel then fresh neutral"}elseif(-not$c.held){Check (-not$sdk.neutral_observation_missing-and$null-ne$sdk.observed_neutral_before_cleanup) "$($case.name) fresh neutral observed before explicit harness cleanup"}
     if($c.image){Check ($sdk.image_count-eq1-and(Test-Path -LiteralPath $spec.png_path)) "$($case.name) one MCP response carries an actual image";if(Test-Path -LiteralPath $spec.png_path){$image=ImageFields $spec.png_path;$case.image=$image;Check ($image.version-eq1-and$image.case_token-eq$arm.case_token-and$image.std_active-gt0-and$image.ex_active-gt0) "$($case.name) image encodes this actual observed input case";if(-not$c.held){$zero=$image.std_rc-eq0-and$image.ex_rc-eq0-and$image.std_buttons-eq0-and$image.ex_buttons-eq0-and$image.std_lt-eq0-and$image.std_rt-eq0-and$image.ex_lt-eq0-and$image.ex_rt-eq0-and$image.std_lx-eq0-and$image.std_ly-eq0-and$image.std_rx-eq0-and$image.std_ry-eq0-and$image.ex_lx-eq0-and$image.ex_ly-eq0-and$image.ex_rx-eq0-and$image.ex_ry-eq0;Check ($zero-and$image.neutral_after-ge$image.last_active-and$image.tick-$image.neutral_after-ge$c.settle-25) "$($case.name) image observes neutral after finite input and requested settle"}else{Check ($image.std_buttons-eq$c.button-and$image.ex_buttons-eq$c.button) "$($case.name) image still observes held A before explicit cleanup"};if($c.model){Check ($image.model-eq$c.model-and$image.display_x-eq0-and[Math]::Sign($image.display_y)-eq$c.display_sign) "$($case.name) caller compensation matches known synthetic display model"}}
      if($c.look_rx-and$case.image){$span=[long]$case.image.neutral_after-[long]$case.image.first_active;Check ($case.image.first_active-gt0-and$span-ge$c.look_ms-100-and$span-le$c.look_ms+300) "$($case.name) actual observed RX duration matches finite requested time within poll bound"}
      $observation=$sdk.action.json.capture_after.observation;Check ($observation.action_state-eq$c.state-and$observation.settle_ms_requested-eq$c.settle-and$observation.settle_ms_elapsed-ge$c.settle) "$($case.name) truthful same-response settle/action-state receipt";if($c.fallback){Check ($sdk.action.json.capture_after.fallback.present_wait_satisfied-eq$false) "$($case.name) explicit Present fallback remains disclosed"}
     };$case.completed=$true
    }catch{$case.error=$_.Exception.Message;$instrument=$false;Check $false "$($case.name) infrastructure: $($case.error)"}finally{try{NeutralSetup}catch{$instrument=$false;Check $false "$($case.name) explicit own cleanup failed"}}
   };$entry.completed=$true
  }catch{$entry.error=$_.Exception.Message;$instrument=$false;Check $false "$arch infrastructure: $($entry.error)"}finally{StopCurrent}
 }
 $complete=$instrument-and$arches.Count-eq2-and@($arches|Where-Object completed -eq $true).Count-eq2-and$cases.Count-eq$plan.Count*2-and@($cases|Where-Object completed -eq $true).Count-eq$plan.Count*2
}catch{$problem=$_.Exception.Message;Check $false "setup: $problem"}
@{schema=1;completed=$complete;passed=@($checks|Where-Object pass -eq $true).Count;failed=@($checks|Where-Object pass -eq $false).Count;cases=$cases;arches=$arches;calls=$calls;processes=$processes;pins=$pins;checks=$checks;error=$problem;failure_is_expected_inversion=$false;game_motion_measured=$false;camera_degrees_measured=$false;backend='actual XInput over disconnected lower source; observed-state memory-DC image';internal_children='audited retained SDK/production ChildProcess; not individually EA65 inventoried'}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8NoBOM
Write-Host "RESULT $(@($checks|Where-Object pass -eq $true).Count) PASS $(@($checks|Where-Object pass -eq $false).Count) FAIL $run";if(-not$complete-or@($checks|Where-Object pass -eq $false).Count){exit 1};exit 0
