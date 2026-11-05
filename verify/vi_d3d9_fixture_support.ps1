# Private D3D9 test lifecycle. Authority is the original retained process handle;
# readonly exited accounting never authorizes a post or termination.
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r1_tail.cs"}
function D9Require([bool]$Good,[string]$Name){if(-not$Good){throw $Name}}
function D9Pin([string]$Path){$pins.Add(@{path=[IO.Path]::GetFullPath($Path);sha256=(Get-FileHash -LiteralPath $Path).Hash;bytes=(Get-Item -LiteralPath $Path).Length})}
function D9Rows([string]$Path){if(Test-Path -LiteralPath $Path){foreach($line in ((Get-Content -LiteralPath $Path -Raw)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}}
function D9Live($T){$r=ReadRetainedImage $T.process $T.path $T.created $T.retained_handle;$T.latest=$r;D9Require $r.matched 'fresh exact retained kernel fixture identity'}
function D9Track($P,[string]$Image){
 $t=@{process=$P;pid=0;path=$Image;created=[long]0;retained_handle=[long]0;cache=$null;cleanup=$null;startup_error=$null};$processes.Add($t)
 try{$t.pid=$P.Id;$t.retained_handle=$P.Handle.ToInt64()}catch{$t.startup_error=$_.Exception.Message};return $t
}
function D9Validate($T){D9Require ($T.retained_handle-ne0-and-not$T.startup_error) 'original process handle retained';$T.created=$T.process.StartTime.ToUniversalTime().Ticks;$T.startup=ObserveStartupImage $T.process $T.path $T.created $T.retained_handle;D9Require $T.startup.matched 'exact owned startup image/PID/creation';$T.cache=New-ViStrictCliStartupCache (ReadRetainedImage $T.process $T.path $T.created $T.retained_handle) $T.retained_handle ([uint32]$T.pid) $T.created $T.path}
function D9Finish($T){if(-not$T-or$T.cleanup){return};try{$T.cleanup=AccountOrReapCli $T.process $T.path $T.created $T.retained_handle ([uint32]$T.pid) $T.cache}catch{$T.cleanup=@{completion_owned=$false;reaped=$false;termination_attempted=$false;error=$_.Exception.Message}};if(-not$T.cleanup.completion_owned-or$T.cleanup.error){$script:instrument=$false;Write-Warning ('Owned cleanup/accounting failed: '+$T.cleanup.error)}}
function D9RunOwned([string]$Image,[string[]]$Arguments,[int]$Budget=15000,[bool]$AwaitAck=$false){
 $Image=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Image).Path);$script:serial++;$prefix=Join-Path $run ('call_'+$serial)
 $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Image;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardInput=$AwaitAck;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$run;$si.Environment['TEMP']="$run\temp";$si.Environment['TMP']="$run\temp";foreach($arg in $Arguments){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;$t=$null;$outTail=$null;$errTail=$null;$record=@{image=$Image;arguments=$Arguments;exit=$null;stdout='';stderr='';cleanup=$null};$calls.Add($record)
 try{D9Require ($p.Start()) 'owned compiler/CLI started';$t=D9Track $p $Image;$outTail=[ViWndProcAwTail]::new($p.StandardOutput);$errTail=[ViWndProcAwTail]::new($p.StandardError);D9Validate $t;if($AwaitAck){$p.StandardInput.WriteLine('RUN');$p.StandardInput.Flush();$p.StandardInput.Close()};D9Require ($p.WaitForExit($Budget)) 'owned compiler/CLI bounded exit';$proof=Get-ViExitedCliAccounting $t.retained_handle ([uint32]$t.pid) $t.created $Image $t.cache;D9Require (Test-ViExitedCliAccounting $proof) 'readonly original-handle CLI exit accounting';$record.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);$record.exit_accounting=$proof
  $null=$outTail.Wait(500);$null=$errTail.Wait(500);$record.stdout=$outTail.Text();$record.stderr=$errTail.Text();$record.stdout_complete=$outTail.Complete;$record.stderr_complete=$errTail.Complete
  D9Require ($outTail.Complete-and$errTail.Complete-and-not$outTail.Error-and-not$errTail.Error-and$outTail.Characters-le65536-and$errTail.Characters-le65536) 'bounded complete separate stdout/stderr';return $record
 }finally{try{if($t){D9Finish $t;$record.cleanup=$t.cleanup}}finally{if($outTail){$null=$outTail.Wait(500);$record.stdout=$outTail.Text()};if($errTail){$null=$errTail.Wait(500);$record.stderr=$errTail.Text()};[IO.File]::WriteAllText($prefix+'.stdout.log',$record.stdout);[IO.File]::WriteAllText($prefix+'.stderr.log',$record.stderr);if($t){$t.process.Dispose();$t.Remove('process')};$p.Dispose()}}
}
function D9Reply($Call){$lines=@($Call.stdout-split'\r?\n'|Where-Object {$_-match'\S'});D9Require ($lines.Count-eq1) 'one native CLI JSON record';$reply=$lines[0]|ConvertFrom-Json;D9Require ($reply.ok-is[bool]) 'typed native CLI result';return $reply}
function D9Control([uint32]$Action){
 D9Live $current;[uint32]$targetPid=0;$h=[IntPtr]::new([long][uint64]$current.ready.hwnd)
 D9Require ([ViD9Test]::GetWindowThreadProcessId($h,[ref]$targetPid)-eq$current.ready.thread-and$targetPid-eq$current.pid-and[ViD9Test]::GetPropW($h,'ViD3D9WindowedFixtureLifetime.v1').ToInt64()-eq[long][uint64]$current.ready.lifetime_cookie) 'fresh exact own control HWND/PID/thread/lifetime'
 $script:token++;$mine=$token;D9Require ([ViD9Test]::PostMessageW($h,0x8001,[UIntPtr]::new([uint64]$Action),[IntPtr]::new([long]$mine))) 'own control accepted'
 $end=[Environment]::TickCount64+4000;do{$ack=@(D9Rows $current.log|Where-Object {$_.event-eq'control'-and$_.token-eq$mine-and$_.action-eq$Action})|Select-Object -Last 1;if($ack){D9Require ($ack.ok-eq$true) 'exact native control ACK';return $ack};D9Live $current;Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$end);throw 'control ACK unavailable'
}
function D9Start([string]$Exe,[string]$Cli,[string]$Log,[string]$Api,[string]$Policy){
 $Exe=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Exe).Path);$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$Exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WorkingDirectory=Split-Path $Exe -Parent;foreach($arg in @($Log,$Api,$Policy)){[void]$si.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;D9Require ($p.Start()) 'owned D3D9 fixture started';$t=D9Track $p $Exe;$script:current=$t;$t.cli=$Cli;$t.log=$Log;D9Validate $t
 $end=[Environment]::TickCount64+7000;do{$ready=@(D9Rows $Log|Where-Object event -eq ready)|Select-Object -First 1;if($ready-or$p.HasExited){break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$end)
 $guard=@(D9Rows $Log|Where-Object event -eq guards)|Select-Object -First 1;D9Require ($ready-and$ready.pid-eq$t.pid-and$ready.thread-gt0-and$ready.hwnd-gt0-and$ready.lifetime_cookie-gt0-and$ready.actual_offscreen_checked-eq$true-and$ready.window_visible-eq$true-and$guard.safe-eq$true-and$guard.terminal_desktop_writers-eq19-and$guard.own_geometry_only_exception-eq$true) 'actual safe offscreen window and terminal desktop-writer guards';$t.ready=$ready;$t.guard=$guard;D9Live $t;return $t
}
function D9Stop{
 if(-not$current){return};try{
  if(-not$current.process.HasExited){try{$null=D9Control 8;[void]$current.process.WaitForExit(1500)}catch{$current.stop_control_error=$_.Exception.Message}}
  D9Finish $current;$current.guard_cleanup=@(D9Rows $current.log|Where-Object event -eq guard_cleanup)|Select-Object -Last 1
  if(-not$current.guard_cleanup-or$current.guard_cleanup.guards_kept_through_cleanup-ne$true-or$current.guard_cleanup.geometry_after_failed-ne0){$script:instrument=$false;Write-Warning 'Terminal geometry guard cleanup witness unavailable or failed'}
 }finally{$current.process.Dispose();$current.Remove('process');$script:current=$null}
}
