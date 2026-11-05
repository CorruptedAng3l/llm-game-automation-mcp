# Fresh live authority and separate readonly exited accounting; copied exact frozen AB1091 functions.
function Reap($P,[string]$Expected,$Created,[long]$Retained){
 $r=@{reaped=$false;identity_verified=$false;exit_known=$false;native_exit_code=$null;error=$null;deadline_ms=1500;termination_attempted=$false;identity_source='retained handle PID + creation + nonempty exact QueryFullProcessImageNameW'}
 try{
  $r.identity=ReadRetainedImage $P $Expected $Created $Retained
  $r.identity_verified=[bool]$r.identity.primary_identity_match
  if(-not$r.identity_verified){throw ('Retained kernel ownership unverified; termination refused: '+$r.identity.reason)}
  if($P.HasExited){$r.reaped=$true}else{
   if(-not$r.identity.matched){throw ('Retained original process not live: '+$r.identity.reason)}
   if(-not$P.HasExited){$r.termination_attempted=$true;$P.Kill()};$r.reaped=$P.WaitForExit(1500)
  }
 }catch{$r.error=$_.Exception.Message}
 finally{try{if($P.HasExited){$r.reaped=$true;$r.exit_known=$true;$r.native_exit_code=$P.ExitCode}}catch{if(-not$r.error){$r.error=$_.Exception.Message}}}
 return $r
}
function AccountOrReapCli($P,[string]$Expected,$Created,[long]$Retained,[uint32]$ExpectedPid,$StartupCache){
 # This read-only query cannot authorize Reap, Kill, Control, Post or AssertOwned.
 $proof=Get-ViExitedCliAccounting $Retained $ExpectedPid $Created $Expected $StartupCache
 if($proof.native_signaled){
  $identity=ReadRetainedImage $P $Expected $Created $Retained
  $r=@{reaped=$true;identity_verified=[bool]$identity.primary_identity_match;identity=$identity;exit_known=$proof.native_exit_known;native_exit_code=$null;error=$null;deadline_ms=1500;termination_attempted=$false;identity_source='current strict EA65 identity, unchanged; separate read-only exited accounting';exit_accounting=$proof;completion_owned=$false}
 }else{
  # Live action authority is still the unmodified fresh EA65 Reap path.
  $r=Reap $P $Expected $Created $Retained
  $proof=Get-ViExitedCliAccounting $Retained $ExpectedPid $Created $Expected $StartupCache
  $r.exit_accounting=$proof;$r.completion_owned=$false
 }
 if($proof.native_exit_known-and$proof.native_signaled){$r.exit_known=$true;$r.native_exit_code=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0)}
 $r.completion_owned=(Test-ViExitedCliAccounting $proof)-and$r.reaped-and-not$r.error
 if(-not$r.completion_owned-and-not$r.error){$r.error='Owned CLI exit accounting refused: '+$proof.reason}
 return $r
}
