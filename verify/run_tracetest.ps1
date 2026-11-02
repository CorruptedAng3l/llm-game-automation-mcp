param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\tracetest")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out | Out-Null; $Out=(Resolve-Path $Out).Path
if(-not ('ViTraceNative' -as [type])) { Add-Type 'using System; using System.Runtime.InteropServices; public static class ViTraceNative { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l); }' }
$results=@()
foreach($name in @('vi_channelprobe.exe','vi_channelprobe32.exe')) {
  $p=$null
  try {
    $p=Start-Process -FilePath "$D\$name" -ArgumentList "`"$Out\$name.log`"" -WindowStyle Hidden -PassThru
    $r=& $V inject --pid $p.Id | ConvertFrom-Json; if(-not $r.ok){throw 'Injection failed'}
    Start-Sleep -Milliseconds 400
    $s=& $V --pid $p.Id status | ConvertFrom-Json
    $h=[IntPtr][long]::Parse($s.hwnd.Substring(2),[Globalization.NumberStyles]::HexNumber)
    [ViTraceNative]::PostMessage($h,0x8001,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 100
    $before=& $V --pid $p.Id trace key esc | ConvertFrom-Json
    if(-not $before.ok){
      $results += [pscustomobject]@{target=$name;ok=$false;trace_supported=$false;reply=$before}
      $results | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath "$Out\result.json"
      throw 'Trace unsupported (expected on baseline)'
    }
    & $V --pid $p.Id key esc 160 | Out-Null
    Start-Sleep -Milliseconds 100
    $notPolled=& $V --pid $p.Id trace key esc | ConvertFrom-Json
    if($notPolled.injected_down -le $before.injected_down -or $notPolled.message_down -le $before.message_down -or $notPolled.channels.RawInputData.down_reads -le $before.channels.RawInputData.down_reads -or $notPolled.channels.GetAsyncKeyState.down_reads -ne $before.channels.GetAsyncKeyState.down_reads){throw 'Delivered versus not-polled distinction failed'}
    [ViTraceNative]::PostMessage($h,0x8001,[IntPtr]1,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 100
    & $V --pid $p.Id down esc | Out-Null
    Start-Sleep -Milliseconds 150
    $polled=& $V --pid $p.Id trace key esc | ConvertFrom-Json
    foreach($channel in @('GetAsyncKeyState','GetKeyState','GetKeyboardState','DirectInputState','DirectInputBuffer','RawInputData')) {
      if($polled.channels.$channel.down_reads -le 0){throw "No positive API read on $channel"}
    }
    if(($polled.channels.GetAsyncKeyState.last -band 0x8000) -eq 0){throw 'Last GAKS value did not show held key'}
    & $V --pid $p.Id up esc | Out-Null
    & $V --pid $p.Id config fakefocus=false | Out-Null
    & $V --pid $p.Id down f | Out-Null
    Start-Sleep -Milliseconds 150
    $noFocus=& $V --pid $p.Id trace key f | ConvertFrom-Json
    if(($noFocus.channels.GetAsyncKeyState.last -band 0x8000) -eq 0){throw 'fakefocus=false unexpectedly hid virtual GAKS'}
    & $V --pid $p.Id up f | Out-Null
    $results += [pscustomobject]@{target=$name;ok=$true;before=$before;delivered_not_polled=$notPolled;polled=$polled;fakefocus_false=$noFocus}
  } finally { if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false} }
}
$results | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath "$Out\result.json"
'PASS x64/x86 per-VK API observations, delivered-but-not-polled control, fakefocus=false'
