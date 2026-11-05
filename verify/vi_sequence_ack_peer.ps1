# Owned, windowless peer: models armed cursor capability, then lost sequence ACK.
# The readiness reply is synthetic protocol data; this peer never injects input.
param([Parameter(Mandatory)][string]$Log,[ValidateSet('disconnect','partial','refuse','invalid_bool','nested_id','unsupported_retap')][string]$Mode)
$ErrorActionPreference='Stop'
$name='vinput_'+$PID
$phaseCount=if($Mode-eq'unsupported_retap'){2}else{3}
for($phase=0;$phase-lt$phaseCount;$phase++){
 $pipe=[IO.Pipes.NamedPipeServerStream]::new($name,[IO.Pipes.PipeDirection]::InOut,1,[IO.Pipes.PipeTransmissionMode]::Byte,[IO.Pipes.PipeOptions]::CurrentUserOnly)
 try{
  if($phase-eq0){[IO.File]::WriteAllText($Log,"READY`n")}
  $pipe.WaitForConnection();$reader=[IO.StreamReader]::new($pipe,[Text.UTF8Encoding]::new($false),$false,1024,$true);$request=$reader.ReadLine();$reader.Dispose();[IO.File]::AppendAllText($Log,"REQUEST $request`n")
  $expected=@('readiness','status','sequence')[$phase];if(($request|ConvertFrom-Json).cmd-ne$expected){throw "unexpected owned-peer phase $phase request: $request"}
  $reply=if($phase-eq0){'{"ok":true,"hook_ready":true,"cursor_hooks_ready":true,"cursor_guard_version":1,"keybd_event_guard_version":1,"keybd_event_hooks_ready":true}'}elseif($phase-eq1){'{"ok":true,"key_sequence_version":1}'}else{switch($Mode){'disconnect'{''}'partial'{'{"ok":true,"accepted":'}'refuse'{'{"ok":false,"input_injected":false,"error":"synthetic definite refusal"}'}'invalid_bool'{'{"ok":"true","accepted":true,"job_id":"synthetic"}'}'nested_id'{'{"ok":true,"accepted":true,"nested":{"job_id":"synthetic"},"job_id":""}'}}}
  if($reply){$bytes=[Text.Encoding]::UTF8.GetBytes($reply+$(if($phase-lt2-or$Mode-ne'partial'){"`n"}else{''}));$pipe.Write($bytes,0,$bytes.Length);$pipe.Flush();$pipe.WaitForPipeDrain()}
 }finally{$pipe.Dispose()}
}
