# Owned fake status pipe: tests doctor formatting/suggestions only, never injects or sends input.
param([ValidateSet('old','humanvirtual','humanreal','unarmed','failed')][string]$Mode,[string]$Ready)
$ErrorActionPreference='Stop'
$state=@{ok=$true;pid=$PID;bits=64;app_container=$false;cursor_mode=1;capture_path='none';frame_api='none';blockfocus=$true;isolate=$true;counters=@{};claim=@{active=$false;owner='';expires_in_ms=0}}
if($Mode-ne'old'){$state.user_took_foreground=$Mode-like'human*';$state.cursor_mode_name=if($Mode-eq'humanreal'){'auto'}else{'virtual'};$state.effective_cursor=if($Mode-eq'humanreal'){'real'}else{'virtual'};$state.foreground_guard=@{enabled=$true;armed=($Mode-ne'unarmed'-and$Mode-ne'humanvirtual');failed_count=if($Mode-eq'failed'){2}else{0}}}
$reply=$state|ConvertTo-Json -Compress -Depth 7
Set-Content -LiteralPath $Ready -Value $PID
while($true){
 $pipe=[IO.Pipes.NamedPipeServerStream]::new("vinput_$PID",[IO.Pipes.PipeDirection]::InOut,1,[IO.Pipes.PipeTransmissionMode]::Byte)
 try{$pipe.WaitForConnection();$reader=[IO.StreamReader]::new($pipe,[Text.UTF8Encoding]::new($false),$false,1024,$true);$request=$reader.ReadLine();$writer=[IO.StreamWriter]::new($pipe,[Text.UTF8Encoding]::new($false),1024,$true);$writer.WriteLine($reply);$writer.Flush();$writer.Dispose();$reader.Dispose()}finally{$pipe.Dispose()}
}
