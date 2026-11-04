# Owned old-protocol responder for the timed-isolation compatibility negative.
# No injection, windows, native input or foreground API use.
param([Parameter(Mandatory=$true)][string]$Log,[Parameter(Mandatory=$true)][string]$Ready)
$ErrorActionPreference='Stop'
$end=[Environment]::TickCount64+20000
[IO.File]::WriteAllText($Ready,[string]$PID)
while([Environment]::TickCount64-lt$end){
 $pipe=[IO.Pipes.NamedPipeServerStream]::new("vinput_$PID",[IO.Pipes.PipeDirection]::InOut,1,[IO.Pipes.PipeTransmissionMode]::Byte,[IO.Pipes.PipeOptions]::Asynchronous)
 try{
  $wait=$pipe.WaitForConnectionAsync();$left=[Math]::Max(1,[int]($end-[Environment]::TickCount64));if(-not$wait.Wait($left)){break}
  $reader=[IO.StreamReader]::new($pipe,[Text.UTF8Encoding]::new($false),$false,1024,$true)
  $line=$reader.ReadLine();[IO.File]::AppendAllText($Log,$line+"`n")
  # A string containing a support-looking fragment is not a top-level capability.
  $wire=[Text.Encoding]::UTF8.GetBytes('{"ok":true,"note":"\"isolation_deadline_version\":1"}'+"`n")
  $pipe.Write($wire,0,$wire.Length);$pipe.Flush();$reader.Dispose()
 }finally{$pipe.Dispose()}
}
