using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
public static class ViDialogTokenNative {
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
}
// No desktop writes. Process output memory is bounded independently of EOF.
public sealed class ViDialogTokenTail {
 readonly object gate=new object();readonly StringBuilder text=new StringBuilder();readonly Task work;
 public long Characters;public bool Complete;public string Error="";
 public ViDialogTokenTail(StreamReader reader){work=Task.Run(async()=>{try{char[] block=new char[2048];for(;;){int count=await reader.ReadAsync(block,0,block.Length);if(count == 0){Complete=true;return;}
  lock(gate){Characters+=count;text.Append(block,0,count);if(text.Length>65536)text.Remove(0,text.Length-65536);}
 }}catch(Exception ex){Error=ex.GetType().Name+": "+ex.Message;}});}
 public bool Wait(int ms){try{return work.Wait(ms);}catch(AggregateException){return true;}}
 public string Text(){lock(gate)return text.ToString();}
}
