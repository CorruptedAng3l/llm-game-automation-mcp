# One connection to the owned target; no status/title queries while its UI is paused.
if(-not('ViSelfRawPressure' -as [type])){Add-Type -TypeDefinition @'
using System;using System.IO;using System.IO.Pipes;using System.Text;
using System.Diagnostics;using System.Runtime.InteropServices;
public static class ViSelfRawPressure {
 [DllImport("user32.dll",SetLastError=true)]static extern bool PostMessageW(IntPtr window,uint message,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
 public sealed class Receipt {public string accepted_reply,rejected_reply;public int filled,queue_error;public long elapsed_ms;}
 static string Exchange(StreamWriter writer,StreamReader reader,string request) {
  writer.WriteLine(request);writer.Flush();var pending=reader.ReadLineAsync();
  if(!pending.Wait(4000))throw new TimeoutException("owned pressure pipe reply deadline");
  var result=pending.GetAwaiter().GetResult();if(result==null)throw new IOException("owned pressure pipe disconnected");return result;
 }
 public static Receipt Run(uint pid,IntPtr window) {
  uint owner;if(GetWindowThreadProcessId(window,out owner)==0||owner!=pid)throw new InvalidOperationException("pressure HWND not owned by target");
  var receipt=new Receipt();var watch=Stopwatch.StartNew();
  using(var pipe=new NamedPipeClientStream(".","vinput_"+pid,PipeDirection.InOut,PipeOptions.Asynchronous)) {
   pipe.Connect(1500);
   using(var writer=new StreamWriter(pipe,new UTF8Encoding(false),1024,true))
   using(var reader=new StreamReader(pipe,new UTF8Encoding(false),false,1024,true)) {
    receipt.accepted_reply=Exchange(writer,reader,"{\"cmd\":\"move\",\"dx\":37,\"dy\":-19,\"raw\":true,\"steps\":1,\"step_ms\":0}");
    bool accepted=true;
    while(receipt.filled<20000) {
     accepted=PostMessageW(window,0,UIntPtr.Zero,IntPtr.Zero);
     if(!accepted){receipt.queue_error=Marshal.GetLastWin32Error();break;}++receipt.filled;
    }
    if(accepted)throw new InvalidOperationException("own UI queue did not reach quota within20000 harmless posts");
    // One target command emits5000 raw records after quota. Rejected submissions
    // must not mutate the earlier accepted record when a4096-slot marker wraps.
    receipt.rejected_reply=Exchange(writer,reader,"{\"cmd\":\"move\",\"dx\":5000,\"dy\":0,\"raw\":true,\"steps\":5000,\"step_ms\":0}");
   }
  }
  watch.Stop();receipt.elapsed_ms=watch.ElapsedMilliseconds;return receipt;
 }
}
'@}
