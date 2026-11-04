# READ-ONLY exited-owned-CLI accounting. This never grants post/kill authority.
# The existing EA65 retained identity leaf is deliberately unchanged.
if(-not('ViOwnedExitedCliAccountingV1'-as[type])){Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public sealed class ViOwnedCliStartupCacheV1 {
 public readonly string proof_type="strict_live_original_cli_startup_v1";
 public readonly long retained_handle, creation_utc_ticks;
 public readonly uint pid;
 public readonly string expected_image, observed_kernel_image;
 internal ViOwnedCliStartupCacheV1(long h,uint p,long c,string e,string k){retained_handle=h;pid=p;creation_utc_ticks=c;expected_image=e;observed_kernel_image=k;}
}
public sealed class ViOwnedExitedCliAccountingV1 {
 public readonly string proof_type="read_only_owned_exited_cli_accounting_v1";
 public readonly bool read_only=true, live_action_authorized=false;
 internal ViOwnedExitedCliAccountingV1(){}
 public bool accounted {get;internal set;} public bool native_signaled {get;internal set;} public bool native_exit_known {get;internal set;} public bool startup_cache_valid {get;internal set;} public bool current_pid_match {get;internal set;} public bool current_creation_match {get;internal set;} public bool current_primary_identity_match {get;internal set;}
 public string reason {get;internal set;} public string expected_image {get;internal set;} public string current_kernel_image {get;internal set;}
 public bool? current_image_match {get;internal set;}
 public long retained_handle {get;internal set;} public long expected_creation_utc_ticks {get;internal set;} public long current_creation_utc_ticks {get;internal set;}
 public uint expected_pid {get;internal set;} public uint current_kernel_pid {get;internal set;} public uint native_wait_result {get;internal set;} public uint native_exit_code {get;internal set;}
 public int wait_error {get;internal set;} public int exit_error {get;internal set;} public int pid_error {get;internal set;} public int times_error {get;internal set;} public int image_error {get;internal set;}
}
public static class ViOwnedExitedCliReadOnly {
 [DllImport("kernel32.dll",SetLastError=true)]static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetExitCodeProcess(IntPtr h,out uint code);
 [DllImport("kernel32.dll",SetLastError=true)]static extern uint GetProcessId(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetProcessTimes(IntPtr h,out long created,out long exited,out long kernel,out long user);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,ExactSpelling=true,SetLastError=true)]static extern bool QueryFullProcessImageNameW(IntPtr h,uint flags,StringBuilder image,ref uint count);
 static bool Eq(string a,string b){return !String.IsNullOrEmpty(a)&&!String.IsNullOrEmpty(b)&&String.Equals(a,b,StringComparison.OrdinalIgnoreCase);}
 public static ViOwnedCliStartupCacheV1 CacheFromStrictObservation(long h,uint p,long c,string expected,bool matched,bool primary,bool exited,long observedHandle,uint observedPid,long observedCreated,string observedExpected,string actual,string kernel,bool errorsAbsent){
  if(h==0||h==-1||p==0||c<=0||String.IsNullOrWhiteSpace(expected)||!matched||!primary||exited||h!=observedHandle||p!=observedPid||c!=observedCreated||!Eq(expected,observedExpected)||!Eq(expected,actual)||!Eq(expected,kernel)||!errorsAbsent)throw new ArgumentException("Strict live original startup observation required; no cache was created");
  return new ViOwnedCliStartupCacheV1(h,p,c,expected,kernel);
 }
 public static ViOwnedExitedCliAccountingV1 Read(long retained,uint pid,long created,string expected,object startupCache){
  var r=new ViOwnedExitedCliAccountingV1();r.retained_handle=retained;r.expected_pid=pid;r.expected_creation_utc_ticks=created;r.expected_image=expected;
  if(retained==0||retained==-1||pid==0||created<=0||String.IsNullOrWhiteSpace(expected)){r.reason="invalid_expected_identity";return r;}
  IntPtr h;try{h=new IntPtr(retained);}catch(OverflowException){r.reason="invalid_retained_handle";return r;}
  r.native_wait_result=WaitForSingleObject(h,0);r.native_signaled=r.native_wait_result==0;if(r.native_wait_result==0xffffffff)r.wait_error=Marshal.GetLastWin32Error();
  uint code;r.native_exit_known=GetExitCodeProcess(h,out code);if(r.native_exit_known)r.native_exit_code=code;else r.exit_error=Marshal.GetLastWin32Error();
  r.current_kernel_pid=GetProcessId(h);if(r.current_kernel_pid==0)r.pid_error=Marshal.GetLastWin32Error();r.current_pid_match=r.current_kernel_pid==pid;
  long c,e,k,u;if(GetProcessTimes(h,out c,out e,out k,out u)&&c>0){try{r.current_creation_utc_ticks=DateTime.FromFileTimeUtc(c).Ticks;}catch(ArgumentOutOfRangeException){r.times_error=87;}}else r.times_error=Marshal.GetLastWin32Error();
  r.current_creation_match=r.current_creation_utc_ticks==created;
  var image=new StringBuilder(32768);uint count=(uint)image.Capacity;bool imageRead=QueryFullProcessImageNameW(h,0,image,ref count);r.current_kernel_image=image.ToString();if(!imageRead)r.image_error=Marshal.GetLastWin32Error();
  if(imageRead&&!String.IsNullOrEmpty(r.current_kernel_image))r.current_image_match=Eq(r.current_kernel_image,expected);
  r.current_primary_identity_match=r.current_pid_match&&r.current_creation_match&&r.current_image_match==true;
  if(imageRead&&r.current_image_match!=true){r.reason="current_kernel_image_empty_or_mismatch";return r;}
  if(!imageRead&&!String.IsNullOrEmpty(r.current_kernel_image)){r.reason="failed_image_query_returned_nonempty_data";return r;}
  if(!imageRead&&r.image_error==0){r.reason="image_query_failure_without_error";return r;}
  if(!r.native_signaled){r.reason=r.native_wait_result==258?"original_handle_not_signaled":"original_handle_wait_failed";return r;}
  if(!r.native_exit_known){r.reason="native_exit_code_unavailable";return r;}
  if(!r.current_pid_match){r.reason="current_kernel_pid_unavailable_or_mismatch";return r;}
  if(!r.current_creation_match){r.reason="current_kernel_creation_unavailable_or_mismatch";return r;}
  var cache=startupCache as ViOwnedCliStartupCacheV1;
  if(cache==null){r.reason=startupCache==null?"strict_startup_cache_missing":"strict_startup_cache_wrong_type";return r;}
  r.startup_cache_valid=cache.retained_handle==retained&&cache.pid==pid&&cache.creation_utc_ticks==created&&Eq(cache.expected_image,expected)&&Eq(cache.observed_kernel_image,expected);
  if(!r.startup_cache_valid){r.reason="strict_startup_cache_identity_mismatch";return r;}
  r.accounted=true;r.reason=imageRead?"exited_current_image_matches_and_strict_startup_cached":"exited_current_image_unavailable_strict_startup_cached";return r;
 }
}
'@}
function New-ViStrictCliStartupCache($Observation,[long]$RetainedHandle,[uint32]$ExpectedPid,[long]$ExpectedCreated,[string]$ExpectedImage){
 # Reject coercible strings/missing flags; the immutable cache copies actual evidence.
 if($null-eq$Observation-or$Observation.matched-isnot[bool]-or$Observation.primary_identity_match-isnot[bool]-or$Observation.original_exited-isnot[bool]){throw 'Strict startup observation flags unavailable or malformed'}
 foreach($field in @('retained_handle','kernel_pid','kernel_creation_utc_ticks')){
  $value=$Observation.$field
  if($value-isnot[byte]-and$value-isnot[sbyte]-and$value-isnot[int16]-and$value-isnot[uint16]-and$value-isnot[int32]-and$value-isnot[uint32]-and$value-isnot[int64]-and$value-isnot[uint64]){throw ('Strict startup integral evidence required: '+$field)}
 }
 foreach($field in @('expected_path','actual_path','kernel_path')){if($Observation.$field-isnot[string]){throw ('Strict startup string evidence required: '+$field)}}
 $errorsAbsent=$null-eq$Observation.pid_error-and$null-eq$Observation.times_error-and$null-eq$Observation.image_error-and$null-eq$Observation.observer_error
 [ViOwnedExitedCliReadOnly]::CacheFromStrictObservation($RetainedHandle,$ExpectedPid,$ExpectedCreated,$ExpectedImage,$Observation.matched,$Observation.primary_identity_match,$Observation.original_exited,[long]$Observation.retained_handle,[uint32]$Observation.kernel_pid,[long]$Observation.kernel_creation_utc_ticks,[string]$Observation.expected_path,[string]$Observation.actual_path,[string]$Observation.kernel_path,$errorsAbsent)
}
function Get-ViExitedCliAccounting([long]$RetainedHandle,[uint32]$ExpectedPid,[long]$ExpectedCreated,[string]$ExpectedImage,$StartupCache){
 [ViOwnedExitedCliReadOnly]::Read($RetainedHandle,$ExpectedPid,$ExpectedCreated,$ExpectedImage,$StartupCache)
}
function Test-ViExitedCliAccounting($Proof){
 # This predicate is completion accounting only, never a lifecycle/action check.
 $Proof-is[ViOwnedExitedCliAccountingV1]-and$Proof.accounted-and$Proof.native_signaled-and$Proof.native_exit_known-and$Proof.startup_cache_valid-and$Proof.current_pid_match-and$Proof.current_creation_match-and$Proof.read_only-and-not$Proof.live_action_authorized
}
