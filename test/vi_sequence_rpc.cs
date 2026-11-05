using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Threading.Tasks;
// Actual MCP newline JSON-RPC adapter. It never starts, kills or probes a process.
// PowerShell owns original process handles and performs strict live identity checks.
public sealed class ViSequenceRpcReader {
 readonly object gate = new object();
 readonly Queue<string> lines = new Queue<string>();
 readonly StringBuilder tail = new StringBuilder();
 readonly Task work;
 string error = ""; bool complete; long characters;
 public ViSequenceRpcReader(StreamReader reader) {
  work = Task.Run(async delegate {
   try {
    var partial = new StringBuilder(); var block = new char[2048];
    for (;;) {
     int count = await reader.ReadAsync(block, 0, block.Length);
     if (count == 0) { lock(gate) { if(partial.Length != 0) error = "unterminated MCP record"; complete = true; } return; }
     lock(gate) {
      characters += count; tail.Append(block,0,count); if(tail.Length > 65536) tail.Remove(0,tail.Length-65536);
      if(characters > 1048576) { error = "MCP cumulative 1MiB budget exceeded"; return; }
      for(int i=0;i<count;i++) {
       char c=block[i]; if(c=='\n') { if(lines.Count>=64) { error="MCP pending record budget exceeded"; return; } lines.Enqueue(partial.ToString().TrimEnd('\r')); partial.Clear(); }
       else { partial.Append(c); if(partial.Length>65536) { error="MCP record exceeds64KiB"; return; } }
      }
     }
    }
   } catch(Exception ex) { lock(gate) error = ex.GetType().Name+": "+ex.Message; }
  });
 }
 public bool TryNext(out string line) { lock(gate) { if(lines.Count!=0) { line=lines.Dequeue(); return true; } line=null; return false; } }
 public string Error { get { lock(gate) return error; } }
 public bool Complete { get { lock(gate) return complete; } }
 public long Characters { get { lock(gate) return characters; } }
 public string Tail() { lock(gate) return tail.ToString(); }
 public bool Wait(int ms) { try { return work.Wait(ms); } catch(AggregateException) { return true; } }
 public static bool Write(StreamWriter writer,string text,int ms,out string error) {
  error=""; try { if(!writer.WriteLineAsync(text).Wait(ms)) { error="MCP write deadline exceeded; delivery unknown"; return false; } return true; }
  catch(Exception ex) { error=ex.GetType().Name+": "+ex.Message; return false; }
 }
}
