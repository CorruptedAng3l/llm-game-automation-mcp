using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
// No desktop writes. Process output memory is bounded independently of EOF.
public sealed class ViCaptureDiagTail {
 readonly object gate=new object();readonly StringBuilder text=new StringBuilder();readonly Task work;
 public long Characters;public bool Complete;public string Error="";
 public ViCaptureDiagTail(StreamReader reader){work=Task.Run(async()=>{try{char[] block=new char[2048];for(;;){int count=await reader.ReadAsync(block,0,block.Length);if(count == 0){Complete=true;return;}
  lock(gate){Characters+=count;text.Append(block,0,count);if(text.Length>65536)text.Remove(0,text.Length-65536);}
 }}catch(Exception ex){Error=ex.GetType().Name+": "+ex.Message;}});}
 public bool Wait(int ms){try{return work.Wait(ms);}catch(AggregateException){return true;}}
 public string Text(){lock(gate)return text.ToString();}
}
