# Windowless named-pipe controls only. The peer implements no original input.
param([string]$PeerRoot=(Split-Path $PSScriptRoot -Parent),[Parameter(Mandatory)][string]$Vinput,[string]$Out="$PSScriptRoot\_out\padphase_peer")
$ErrorActionPreference='Stop';$Vinput=(Resolve-Path -LiteralPath $Vinput).Path;$PeerRoot=(Resolve-Path -LiteralPath $PeerRoot).Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path -LiteralPath $Out).Path
Add-Type -Path "$PeerRoot\verify\vi_padphase_peer.cs"
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new()
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name"}
$modern=$null-ne[ViPadPhasePeer].GetProperty('HostPid');$legacyFixturePid=$PID+100000
function Start-Peer {if($modern){return [ViPadPhasePeer]::Start('{"ok":true}')}else{return [ViPadPhasePeer]::Start($legacyFixturePid,'{"ok":true}')}}
function Peer-Pid {if($modern){return [ViPadPhasePeer]::HostPid}else{return $legacyFixturePid}}
foreach($escaped in @('{"cmd":"pad\u002dtimeline","states_json":"[{\"at_ms\":0,\"buttons\":\"a\"}]","stagger_ms":60}','{"cmd":"pad-timeline","states_json":"[{\"at_ms\":0,\"buttons\":\"a\"}]","stagger\u005fms":60}')){
 $peer=Start-Peer;$targetPid=Peer-Pid;$start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Vinput;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 foreach($arg in @('--json','--pid',"$targetPid",'json',$escaped)){[void]$start.ArgumentList.Add($arg)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$start;$wire=$null;$peerError=$null
 try{
  if(-not$process.Start()){throw 'peer-only CLI did not start'};$outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync()
  if(-not$process.WaitForExit(12000)){throw 'peer-only CLI exceeded deadline'}
  $text=$outTask.GetAwaiter().GetResult();$errorText=$errTask.GetAwaiter().GetResult();$reply=$text|ConvertFrom-Json
  try{$wire=$peer.GetAwaiter().GetResult()}catch{$peerError=$_.Exception.Message}
  $evidence.Add(@{requested_pid=$targetPid;actual_peer_pid=$PID;legacy_fixture_pid=$legacyFixturePid;wire=$wire;peer_error=$peerError;exit=$process.ExitCode;reply=$reply;stderr=$errorText;original_input_implemented=$false})
  Check ($targetPid-eq$PID) 'capability peer names its actual kernel server PID'
  Check (-not$peerError-and$process.ExitCode-ne0-and$reply.input_injected-eq$false-and$reply.pad_timeline_support_checked-eq$true-and($wire|ConvertFrom-Json).cmd-eq'ping') 'escaped pad request reaches exact old-capability refusal without original input'
 }finally{$process.Dispose()}
}
if($modern){
 Check ([ViPadPhasePeer]::ValidRequest('{ "wait_ms":0, "hooks_only":true, "cmd":"readiness" }',0)) 'typed readiness permits JSON whitespace and field order'
 Check ([ViPadPhasePeer]::ValidRequest('{"cmd":"p\u0069ng","pad_timeline_support":true,"owner":"test","force_claim":false}',1)) 'typed ping decodes strings and permits only typed ownership stamps'
 $negative=@(
  @('{"cmd":"ping","pad_timeline_support":true}',0),
  @('{"cmd":"readiness","hooks_only":true,"wait_ms":0}',1),
  @('{"cmd":"pad-timeline","note":"\"cmd\":\"readiness\"","hooks_only":true,"wait_ms":0}',0),
  @('{"cmd":"readiness","cmd":"pad-timeline","hooks_only":true,"wait_ms":0}',0),
  @('{"nested":{"cmd":"readiness"},"hooks_only":true,"wait_ms":0}',0),
  @('{"cmd":"readiness","hooks_only":"true","wait_ms":0}',0),
  @('{"cmd":"readiness","hooks_only":true,"wait_ms":1}',0),
  @('{"cmd":"ping","pad_timeline_support":1}',1),
  @('{"cmd":"ping","pad_timeline_support":true,"states_json":"[]"}',1),
  @('{"cmd":"ping","pad_timeline_support":true,"owner":1}',1),
  @('{"cmd":"status"}',1),@('not json',0)
 )
 foreach($case in $negative){Check (-not[ViPadPhasePeer]::ValidRequest($case[0],[int]$case[1])) 'wrong phase, malformed, nested, duplicate or original-input request is not implemented'}
 $peer=[ViPadPhasePeer]::Start('{"ok":true}');$client=[IO.Pipes.NamedPipeClientStream]::new('.','vinput_'+$PID,[IO.Pipes.PipeDirection]::InOut)
 try{$client.Connect(2000);$writer=[IO.StreamWriter]::new($client,[Text.UTF8Encoding]::new($false),4096,$true);$writer.WriteLine('{"cmd":"pad-timeline","states_json":"[]"}');$writer.Flush();$refused=$false;try{$null=$peer.GetAwaiter().GetResult()}catch{$refused=$_.Exception.Message-match'original input is never implemented'};Check $refused 'live peer closes on original input instead of replying or applying it'}finally{$client.Dispose()}
}else{Check $false 'legacy peer lacks exact typed request validation'}
$failed=@($checks|Where-Object pass -eq $false).Count
@{completed=$true;scope='owned windowless capability pipe; no DLL, window, device, GUI or input implementation';passed=$checks.Count-$failed;failed=$failed;checks=$checks;evidence=$evidence;peer_sha256=(Get-FileHash "$PeerRoot\verify\vi_padphase_peer.cs").Hash;cli_sha256=(Get-FileHash $Vinput).Hash}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json"
Write-Host "$($checks.Count-$failed) PASS / $failed FAIL (peer-only)";if($failed){exit 1}
