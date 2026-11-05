// Owned, non-input named-pipe peer: proves capability refusal precedes original input.
using System;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
public static class ViPadPhasePeer {
 public static int HostPid { get { return Process.GetCurrentProcess().Id; } }
 public static bool ValidRequest(string request,int phase) {
  if(request==null||request.Length>65536||phase<0||phase>1)return false;
  try { using(var document=JsonDocument.Parse(request,new JsonDocumentOptions{MaxDepth=4})) {
   var root=document.RootElement;if(root.ValueKind!=JsonValueKind.Object)return false;
   var names=new HashSet<string>(StringComparer.Ordinal);
   foreach(var field in root.EnumerateObject()) {
    if(!names.Add(field.Name))return false;
    if(field.Name=="owner") {if(field.Value.ValueKind!=JsonValueKind.String)return false;continue;}
    if(field.Name=="force_claim") {if(field.Value.ValueKind!=JsonValueKind.True&&field.Value.ValueKind!=JsonValueKind.False)return false;continue;}
    if(field.Name=="cmd")continue;
    if(phase==0&&(field.Name=="hooks_only"||field.Name=="wait_ms"))continue;
    if(phase==1&&field.Name=="pad_timeline_support")continue;
    return false;
   }
   JsonElement command;if(!root.TryGetProperty("cmd",out command)||command.ValueKind!=JsonValueKind.String||command.GetString()!=(phase==0?"readiness":"ping"))return false;
   JsonElement value;
   if(phase==1)return root.TryGetProperty("pad_timeline_support",out value)&&value.ValueKind==JsonValueKind.True;
   int wait;return root.TryGetProperty("hooks_only",out value)&&value.ValueKind==JsonValueKind.True&&root.TryGetProperty("wait_ms",out value)&&value.ValueKind==JsonValueKind.Number&&value.TryGetInt32(out wait)&&wait==0;
  }}catch(JsonException){return false;}
 }
 public static Task<string> Start(string response) {
  // First model a matching armed cursor guard, then an older pad protocol.
  // Both replies are synthetic data; no input request is implemented or applied.
  // Transport validates the kernel pipe-server PID, so name only our actual host.
  int pid=HostPid;
  var first=new NamedPipeServerStream("vinput_"+pid,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous|PipeOptions.CurrentUserOnly);
  return Task.Run(async()=>{
   string last=null;
   for(int phase=0;phase<2;phase++) {
    using(var pipe=phase==0?first:new NamedPipeServerStream("vinput_"+pid,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous|PipeOptions.CurrentUserOnly))
    using(var deadline=new CancellationTokenSource(5000)) {
     await pipe.WaitForConnectionAsync(deadline.Token);
     var reader=new StreamReader(pipe,new UTF8Encoding(false),false,4096,true);
     var writer=new StreamWriter(pipe,new UTF8Encoding(false),4096,true);
     var read=reader.ReadLineAsync();
     if(await Task.WhenAny(read,Task.Delay(2000))!=read)throw new TimeoutException("owned peer read deadline");
     last=await read;
     if(!ValidRequest(last,phase))throw new InvalidDataException("Owned peer received an unexpected capability phase "+phase+"; original input is never implemented; wire="+(last==null?"<closed before request>":last.Substring(0,Math.Min(last.Length,1024))));
     await writer.WriteLineAsync(phase==0?"{\"ok\":true,\"hook_ready\":true,\"cursor_hooks_ready\":true,\"cursor_guard_version\":1,\"keybd_event_guard_version\":1,\"keybd_event_hooks_ready\":true}":response);
     await writer.FlushAsync();
    }
   }
   return last;
  });
 }
}
