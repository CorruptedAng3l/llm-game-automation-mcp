# Fixture ownership only: no PID reopening, process writes, or .NET Path fallback.
if(-not('ViDeferredStartupImage'-as[type])){Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ViDeferredStartupImage {
 [DllImport("kernel32.dll",SetLastError=true)]public static extern uint GetProcessId(IntPtr process);
 [DllImport("kernel32.dll",SetLastError=true)]public static extern bool GetProcessTimes(IntPtr process,out long created,out long exited,out long kernel,out long user);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,ExactSpelling=true,SetLastError=true)]public static extern bool QueryFullProcessImageNameW(IntPtr process,uint flags,StringBuilder path,ref uint count);
}
'@}
function ReadRetainedImage($P,[string]$Expected,[long]$Created,[long]$Retained){
 $r=@{matched=$false;primary_identity_match=$false;reason='kernel_identity_unavailable';expected_path=$Expected;actual_path=$null;retained_handle=$Retained;kernel_pid=0;kernel_creation_utc_ticks=$null;kernel_path=$null;pid_error=$null;times_error=$null;image_error=$null;original_exited=$false}
 try{
  if(-not$P-or-not$Expected-or$Created-le0-or$Retained-eq0-or$Retained-eq-1){$r.reason='invalid_expected_identity';return $r}
  $handle=[IntPtr]::new($Retained);$r.kernel_pid=[ViDeferredStartupImage]::GetProcessId($handle);if(-not$r.kernel_pid){$r.pid_error=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
  [long]$createdFile=0;[long]$exitFile=0;[long]$kernel=0;[long]$user=0
  if([ViDeferredStartupImage]::GetProcessTimes($handle,[ref]$createdFile,[ref]$exitFile,[ref]$kernel,[ref]$user)-and$createdFile-gt0){$r.kernel_creation_utc_ticks=[DateTime]::FromFileTimeUtc($createdFile).Ticks}else{$r.times_error=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
  $buffer=[Text.StringBuilder]::new(32768);[uint32]$characters=$buffer.Capacity
  if([ViDeferredStartupImage]::QueryFullProcessImageNameW($handle,0,$buffer,[ref]$characters)){$r.kernel_path=$buffer.ToString()}else{$r.image_error=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
  if($r.kernel_pid-ne0-and$r.kernel_pid-ne$P.Id){$r.reason='kernel_pid_mismatch';return $r}
  if($r.kernel_creation_utc_ticks-and$r.kernel_creation_utc_ticks-ne$Created){$r.reason='kernel_creation_mismatch';return $r}
  if($r.kernel_path-and$r.kernel_path-ne$Expected){$r.reason='kernel_image_mismatch';return $r}
  $r.primary_identity_match=$r.kernel_pid-eq$P.Id-and$r.kernel_creation_utc_ticks-eq$Created-and-not[string]::IsNullOrEmpty($r.kernel_path)-and$r.kernel_path-eq$Expected
  $r.original_exited=$P.HasExited
  if($r.primary_identity_match){$r.actual_path=$r.kernel_path;$r.matched=-not$r.original_exited;$r.reason=if($r.original_exited){'original_process_exited'}else{'retained_kernel_identity_match'}}elseif($r.original_exited){$r.reason='original_process_exited'}
 }catch{$r.observer_error=$_.Exception.Message}
 return $r
}
function ObserveStartupImage($P,[string]$Expected,[long]$Created,[long]$Retained,[int]$Budget=2000){
 $history=[Collections.Generic.List[object]]::new();$end=[Environment]::TickCount64+$Budget;$attempts=0
 $result=@{matched=$false;reason='kernel_identity_unavailable';expected_path=$Expected;actual_path=$null;creation_utc_ticks=$Created;retained_handle=$Retained;deadline_ms=$Budget;attempts=0;observations=$history;observations_dropped=0;secondary_role='fresh .NET Path/StartTime are diagnostics only; never authority or fallback';secondary_conflicts=0}
 do{
  $attempts++;$fresh=$null;$row=ReadRetainedImage $P $Expected $Created $Retained;$row.attempt=$attempts
  $row.fresh_path=$null;$row.fresh_creation_utc_ticks=$null;$row.fresh_error=$null;$row.fresh_path_conflict=$false;$row.fresh_creation_conflict=$false
  try{
   try{$fresh=Get-Process -Id $P.Id -ErrorAction Stop;$row.fresh_creation_utc_ticks=$fresh.StartTime.ToUniversalTime().Ticks;$row.fresh_path=$fresh.Path}catch{$row.fresh_error=$_.Exception.Message}
   $row.fresh_path_conflict=[bool]($row.fresh_path-and$row.fresh_path-ne$Expected);$row.fresh_creation_conflict=[bool]($row.fresh_creation_utc_ticks-and$row.fresh_creation_utc_ticks-ne$Created)
   if($row.fresh_path_conflict-or$row.fresh_creation_conflict){$result.secondary_conflicts++}
   $result.reason=$row.reason
   if($row.matched-and-not$P.HasExited){$result.matched=$true;$result.actual_path=$row.kernel_path;break}
   if($row.reason-in@('kernel_pid_mismatch','kernel_creation_mismatch','kernel_image_mismatch','invalid_expected_identity','original_process_exited')){break}
  }finally{if($fresh){$fresh.Dispose()};$history.Add($row);if($history.Count-gt16){$history.RemoveAt(0);$result.observations_dropped++}}
  if([Environment]::TickCount64-lt$end){Start-Sleep -Milliseconds 10}
 }while([Environment]::TickCount64-lt$end)
 $result.attempts=$attempts;return $result
}
