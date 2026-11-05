// Owned, non-input named-pipe peer: proves capability refusal precedes original input.
using System;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
public static class ViPadPhasePeer {
 public static Task<string> Start(int pid,string response) {
  // First model a matching armed cursor guard, then an older pad protocol.
  // Both replies are synthetic data; no input request is implemented or applied.
  var first=new NamedPipeServerStream("vinput_"+pid,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous);
  return Task.Run(async()=>{
   string last=null;
   for(int phase=0;phase<2;phase++) {
    using(var pipe=phase==0?first:new NamedPipeServerStream("vinput_"+pid,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous))
    using(var deadline=new CancellationTokenSource(5000)) {
     await pipe.WaitForConnectionAsync(deadline.Token);
     var reader=new StreamReader(pipe,new UTF8Encoding(false),false,4096,true);
     var writer=new StreamWriter(pipe,new UTF8Encoding(false),4096,true);
     var read=reader.ReadLineAsync();
     if(await Task.WhenAny(read,Task.Delay(2000))!=read)throw new TimeoutException("owned peer read deadline");
     last=await read;
     string expected=phase==0?"\"cmd\":\"readiness\"":"\"cmd\":\"ping\"";
     if(last==null||last.IndexOf(expected,StringComparison.Ordinal)<0)throw new InvalidDataException("Owned peer received an unexpected capability phase; original input is never implemented");
     await writer.WriteLineAsync(phase==0?"{\"ok\":true,\"hook_ready\":true,\"cursor_hooks_ready\":true,\"cursor_guard_version\":1,\"keybd_event_guard_version\":1,\"keybd_event_hooks_ready\":true}":response);
     await writer.FlushAsync();
    }
   }
   return last;
  });
 }
}
