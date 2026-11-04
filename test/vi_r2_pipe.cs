using System;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
public static class ViR2Native {
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostThreadMessageW(uint thread,uint message,UIntPtr w,IntPtr l);
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr window,uint message,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetPropW(IntPtr window,string name);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern IntPtr GetDesktopWindow();
}
public sealed class ViR2PipeResult {public bool Complete;public string Text="",Error="";public uint ServerPid;}
public static class ViR2Pipe {
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetNamedPipeServerProcessId(IntPtr pipe,out uint pid);
 [DllImport("kernel32.dll",SetLastError=true)]static extern uint GetProcessId(IntPtr process);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetProcessTimes(IntPtr process,out long c,out long e,out long k,out long u);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool QueryFullProcessImageNameW(IntPtr process,uint flags,StringBuilder value,ref uint size);
 [DllImport("kernel32.dll",SetLastError=true)]static extern uint WaitForSingleObject(IntPtr process,uint ms);
 static bool Live(IntPtr process,uint pid,long ticks,string image) {
  if(process==IntPtr.Zero||WaitForSingleObject(process,0)!=258||GetProcessId(process)!=pid)return false;
  long c,e,k,u;if(!GetProcessTimes(process,out c,out e,out k,out u)||DateTime.FromFileTimeUtc(c).Ticks!=ticks)return false;
  var text=new StringBuilder(32768);uint size=32768;
  return QueryFullProcessImageNameW(process,0,text,ref size)&&size>0&&String.Equals(Path.GetFullPath(text.ToString()),Path.GetFullPath(image),StringComparison.OrdinalIgnoreCase);
 }
 static async Task<ViR2PipeResult> Exchange(NamedPipeClientStream pipe,uint pid,IntPtr retained,long ticks,string image,string request,int ms,CancellationToken cancel) {
  var result=new ViR2PipeResult();
  try {
   await pipe.ConnectAsync(ms,cancel).ConfigureAwait(false);uint server;
   if(!GetNamedPipeServerProcessId(pipe.SafePipeHandle.DangerousGetHandle(),out server)){result.Error="server PID unavailable";return result;}
   result.ServerPid=server;if(server!=pid||!Live(retained,pid,ticks,image)){result.Error="fresh retained kernel/server identity refused";return result;}
   var data=Encoding.UTF8.GetBytes(request+"\n");if(data.Length>8192){result.Error="request too large";return result;}
   await pipe.WriteAsync(data,0,data.Length,cancel).ConfigureAwait(false);await pipe.FlushAsync(cancel).ConfigureAwait(false);
   using(var bytes=new MemoryStream()) {
    var block=new byte[2048];bool newline=false;
    while(!newline){int count=await pipe.ReadAsync(block,0,block.Length,cancel).ConfigureAwait(false);if(count==0)break;
     for(int i=0;i<count;i++){if(block[i]==10){newline=true;for(int j=i+1;j<count;j++)if(block[j]!=13&&block[j]!=32&&block[j]!=9){result.Error="extra response after newline";return result;}break;}bytes.WriteByte(block[i]);if(bytes.Length>65536){result.Error="reply exceeds64KiB";return result;}}
    }
    result.Text=new UTF8Encoding(false,true).GetString(bytes.ToArray());result.Complete=newline&&result.Text.Length>0;
    if(!result.Complete)result.Error="reply ended before complete newline";
   }
  }catch(Exception ex){result.Error=ex.GetType().Name+": "+ex.Message;}
  return result;
 }
 public static ViR2PipeResult Query(uint pid,long retained,long ticks,string image,string request,int ms) {
  if(pid==0||ms<1||ms>10000)return new ViR2PipeResult{Error="invalid bounded request"};
  using(var cancel=new CancellationTokenSource(ms))using(var pipe=new NamedPipeClientStream(".","vinput_"+pid,PipeDirection.InOut,PipeOptions.Asynchronous)) {
   var task=Exchange(pipe,pid,new IntPtr(retained),ticks,image,request,ms,cancel.Token);
   try {if(!task.Wait(ms+200)){cancel.Cancel();pipe.Dispose();return new ViR2PipeResult{Error="bounded pipe task did not complete"};}return task.GetAwaiter().GetResult();}
   catch(Exception ex){return new ViR2PipeResult{Error=ex.GetType().Name+": "+ex.Message};}
  }
 }
}
