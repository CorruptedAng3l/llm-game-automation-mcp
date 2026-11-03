# Append-only byte snapshots. A missing, replaced, truncated, or unreadable log
# cannot establish a clean native-input window.
if(-not('ViLogWindow'-as[type])){Add-Type @'
using System;using System.IO;using System.Text;using System.Threading;
public static class ViLogWindow {
 public sealed class Snapshot {public byte[] bytes;public string reason;public bool valid;public long offset {get{return bytes==null?0:bytes.LongLength;}}}
 public sealed class Window {public bool valid;public string reason,text;public long start_bytes,end_bytes;}
 public static Snapshot Read(string path){string reason="unavailable";for(int attempt=0;attempt<3;attempt++){
  try{using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
   long length=file.Length;if(length>8*1024*1024)return new Snapshot{reason="log_too_large"};
   var bytes=new byte[(int)length];int got=0;while(got<bytes.Length){int n=file.Read(bytes,got,bytes.Length-got);if(n==0)break;got+=n;}
   if(got==bytes.Length&&file.Length>=length)return new Snapshot{valid=true,bytes=bytes,reason="snapshot"};reason="short_read";
  }}catch(IOException){reason="read_failed";}catch(UnauthorizedAccessException){reason="read_denied";}
  Thread.Sleep(10);
 }return new Snapshot{reason=reason};}
 public static Window Finish(string path,Snapshot start){var end=Read(path);var w=new Window{start_bytes=start.offset,end_bytes=end.offset,text=""};
  if(!start.valid){w.reason="start_"+start.reason;return w;}if(!end.valid){w.reason="end_"+end.reason;return w;}
  if(end.bytes.Length<start.bytes.Length){w.reason="log_shrank";return w;}
  for(int i=0;i<start.bytes.Length;i++)if(start.bytes[i]!=end.bytes[i]){w.reason="log_rewritten";return w;}
  w.valid=true;w.reason="append_only";w.text=Encoding.UTF8.GetString(end.bytes,start.bytes.Length,end.bytes.Length-start.bytes.Length);return w;
 }
}
'@}
