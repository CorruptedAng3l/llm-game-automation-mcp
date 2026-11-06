# Retain one kernel handle once; never reopen by PID/name during cleanup.
# Read-only exited accounting never authorizes a termination.
. "$PSScriptRoot/vi_r1_identity.ps1"
. "$PSScriptRoot/vi_exited_cli_accounting.ps1"
function New-ViEarlyOwnedRecord($Process,[string]$ExpectedImage,[long]$ExpectedCreated){
 $record=@{process=$Process;pid=0;path=[IO.Path]::GetFullPath($ExpectedImage);created=$ExpectedCreated;retained_handle=[long]0;startup_verified=$false;startup=$null;cache=$null;startup_error=$null;cleanup=$null}
 try{$record.pid=$Process.Id;$record.retained_handle=$Process.Handle.ToInt64();$record.startup=ReadRetainedImage $Process $record.path $ExpectedCreated $record.retained_handle
  if(-not$record.startup.matched-or$record.startup.kernel_pid-ne$record.pid){throw 'strict original-handle startup image/PID/creation mismatch'}
  $record.cache=New-ViStrictCliStartupCache $record.startup $record.retained_handle ([uint32]$record.pid) $ExpectedCreated $record.path;$record.startup_verified=$true
 }catch{$record.startup_error=$_.Exception.Message};return $record
}
function Stop-ViEarlyOwnedRecord($Record){
 if($Record.cleanup){return $Record.cleanup}
 $result=@{completed=$false;termination_requested=$false;kill_error=$null;wait_ms=5000;original_handle_signaled=$false;exit_accounting=$null;error=$null}
 try{
  if(-not$Record.startup_verified-or-not$Record.cache){throw 'no verified original startup handle; cleanup refused'}
  if(-not$Record.process.WaitForExit(0)){
   $authority=ReadRetainedImage $Record.process $Record.path $Record.created $Record.retained_handle
   if(-not$authority.matched-or$authority.kernel_pid-ne$Record.pid){throw 'fresh retained kernel ownership not proved; termination refused'}
   $result.termination_requested=$true
   try{$Record.process.Kill()}catch{$result.kill_error=$_.Exception.Message}
  }
  # Always wait on the SAME retained Process object after termination. A
  # disappearing image path or reused numeric PID can never skip this wait.
  $result.original_handle_signaled=$Record.process.WaitForExit(5000)
  if(-not$result.original_handle_signaled){throw 'original process did not signal exit within5000ms'}
  $result.exit_accounting=Get-ViExitedCliAccounting $Record.retained_handle ([uint32]$Record.pid) $Record.created $Record.path $Record.cache
  if(-not(Test-ViExitedCliAccounting $result.exit_accounting)){throw ('original exited process accounting unavailable: '+$result.exit_accounting.reason)}
  $result.completed=$true
 }catch{$result.error=$_.Exception.Message};$Record.cleanup=$result;return $result
}
