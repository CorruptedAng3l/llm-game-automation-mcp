using System;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
public sealed class ViRadarWireResult {public bool Complete;public string Text="",Error="";public uint ServerPid;}
// One privately owned pipe, finite request/reply buffers and cancellation. It
// authorizes only an exact fresh retained kernel handle + server PID/image.
public sealed class ViRadarPending : IDisposable {
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetNamedPipeServerProcessId(IntPtr pipe,out uint pid);
 [DllImport("kernel32.dll",SetLastError=true)]static extern uint GetProcessId(IntPtr process);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetProcessTimes(IntPtr process,out long c,out long e,out long k,out long u);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool QueryFullProcessImageNameW(IntPtr process,uint flags,StringBuilder value,ref uint size);
 [DllImport("kernel32.dll",SetLastError=true)]static extern uint WaitForSingleObject(IntPtr process,uint ms);
 readonly NamedPipeClientStream pipe;readonly CancellationTokenSource cancel;readonly Task<ViRadarWireResult> task;
 public bool IsCompleted {get{return task.IsCompleted;}}
 static bool Live(IntPtr process,uint pid,long ticks,string image){
  if(process==IntPtr.Zero||WaitForSingleObject(process,0)!=258||GetProcessId(process)!=pid)return false;
  long c,e,k,u;if(!GetProcessTimes(process,out c,out e,out k,out u)||DateTime.FromFileTimeUtc(c).Ticks!=ticks)return false;
  var text=new StringBuilder(32768);uint size=32768;
  return QueryFullProcessImageNameW(process,0,text,ref size)&&size>0&&String.Equals(Path.GetFullPath(text.ToString()),Path.GetFullPath(image),StringComparison.OrdinalIgnoreCase);
 }
 ViRadarPending(uint pid,long retained,long ticks,string image,string request,int ms){
  if(pid==0||ms<1||ms>20000)throw new ArgumentException("invalid finite radar pipe budget");
  cancel=new CancellationTokenSource(ms);pipe=new NamedPipeClientStream(".","vinput_"+pid,PipeDirection.InOut,PipeOptions.Asynchronous);
  task=Exchange(pid,new IntPtr(retained),ticks,image,request,ms,cancel.Token);
 }
 public static ViRadarPending Start(uint pid,long retained,long ticks,string image,string request,int ms){return new ViRadarPending(pid,retained,ticks,image,request,ms);}
 async Task<ViRadarWireResult> Exchange(uint pid,IntPtr retained,long ticks,string image,string request,int ms,CancellationToken token){
  var result=new ViRadarWireResult();try{
   var data=Encoding.UTF8.GetBytes(request+"\n");if(data.Length>8192){result.Error="request exceeds8192 bytes";return result;}
   await pipe.ConnectAsync(ms,token).ConfigureAwait(false);uint server;
   if(!GetNamedPipeServerProcessId(pipe.SafePipeHandle.DangerousGetHandle(),out server)){result.Error="server PID unavailable";return result;}
   result.ServerPid=server;if(server!=pid||!Live(retained,pid,ticks,image)){result.Error="fresh original kernel/server identity refused";return result;}
   await pipe.WriteAsync(data,0,data.Length,token).ConfigureAwait(false);await pipe.FlushAsync(token).ConfigureAwait(false);
   using(var bytes=new MemoryStream()){
    var block=new byte[2048];bool newline=false;while(!newline){int count=await pipe.ReadAsync(block,0,block.Length,token).ConfigureAwait(false);if(count==0)break;
     for(int i=0;i<count;i++){if(block[i]==10){newline=true;for(int j=i+1;j<count;j++)if(block[j]!=13&&block[j]!=32&&block[j]!=9){result.Error="extra reply after newline";return result;}break;}bytes.WriteByte(block[i]);if(bytes.Length>65536){result.Error="reply exceeds65536 bytes";return result;}}
    }
    result.Text=new UTF8Encoding(false,true).GetString(bytes.ToArray());result.Complete=newline&&result.Text.Length>0;if(!result.Complete)result.Error="reply ended before complete newline";
   }
  }catch(Exception ex){result.Error=ex.GetType().Name+": "+ex.Message;}return result;
 }
 public ViRadarWireResult Finish(int ms){if(ms<0||ms>20500)throw new ArgumentException("invalid bounded wait");try{if(task.Wait(ms))return task.GetAwaiter().GetResult();return new ViRadarWireResult{Error="bounded radar wait expired"};}catch(Exception ex){return new ViRadarWireResult{Error=ex.GetType().Name+": "+ex.Message};}}
 public void Disconnect(){cancel.Cancel();pipe.Dispose();}
 public void Dispose(){Disconnect();try{task.Wait(500);}catch{}cancel.Dispose();}
}
