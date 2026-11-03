# Read-only witness in the uninjected runner. Never generates or redirects input.
if(-not('ViPhysicalWindow'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;using System.Threading;
public static class ViPhysicalWindow {
 [StructLayout(LayoutKind.Sequential)]struct LastInput{public uint size,time;}
 [DllImport("user32.dll")]static extern bool GetLastInputInfo(ref LastInput value);
 [DllImport("user32.dll")]static extern short GetAsyncKeyState(int key);
 [DllImport("kernel32.dll")]static extern uint GetTickCount();
 public sealed class Evidence{public uint first_tick,last_tick,initial_age_ms;public int samples,tick_changes,held_samples,read_failures;}
 public static Evidence Sample(int ms){var result=new Evidence();var watch=System.Diagnostics.Stopwatch.StartNew();bool first=true;uint previous=0;
  do{var input=new LastInput{size=(uint)Marshal.SizeOf<LastInput>()};if(!GetLastInputInfo(ref input)){result.read_failures++;}else{
    if(first){result.first_tick=input.time;result.initial_age_ms=unchecked(GetTickCount()-input.time);first=false;}else if(previous!=input.time)result.tick_changes++;
    previous=input.time;result.last_tick=input.time;}
   bool held=false;for(int key=1;key<256;key++)if((GetAsyncKeyState(key)&0x8000)!=0){held=true;break;}if(held)result.held_samples++;result.samples++;Thread.Sleep(20);
  }while(watch.ElapsedMilliseconds<ms);return result;
 }
}
'@}
function Get-ViNativeWindowVerdict($Witness,[bool]$InputSeen){
 if($Witness.read_failures-gt0){return 'not_measured'}
 if($Witness.tick_changes-gt0-or$Witness.held_samples-gt0-or$Witness.initial_age_ms-lt500){return 'not_measured'}
 if($InputSeen){return 'failed'}
 return 'passed'
}
