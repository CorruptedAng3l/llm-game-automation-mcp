using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
// Preserve the original >64KiB journal assertion with a bounded one-MiB reader.
public sealed class ViPadPhaseTail {
 readonly object gate=new object();readonly StringBuilder text=new StringBuilder();readonly Task work;
 public long Characters;public bool Complete;public string Error="";
 public ViPadPhaseTail(StreamReader reader){work=Task.Run(async()=>{try{char[] block=new char[4096];for(;;){int n=await reader.ReadAsync(block,0,block.Length);if(n==0){Complete=true;return;}lock(gate){Characters+=n;if(Characters<=1048576)text.Append(block,0,n);else Error="output exceeded one MiB";}}}catch(Exception e){Error=e.Message;}});}
 public bool Wait(int ms){try{return work.Wait(ms);}catch(AggregateException){return true;}}
 public string Text(){lock(gate)return text.ToString();}
}
