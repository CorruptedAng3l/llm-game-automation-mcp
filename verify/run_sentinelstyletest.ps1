# Read actual construction style only. No foreground/activation or input operations.
param([string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\sentinelstyle",[switch]$KeepGoing)
$ErrorActionPreference='Stop';$D=(Resolve-Path $TargetDir).Path;New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
Add-Type 'using System;using System.Runtime.InteropServices;public static class ViSentinelStyleNative{[DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int n);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);}'
$results=@();$failed=0
foreach($arch in @('','32')){foreach($normal in @($false,$true)){
 $mode=if($normal){'normal'}else{'default'};$log="$Out\$arch.$mode.log";if(Test-Path $log){Remove-Item -LiteralPath $log};$p=$null
 try{
  $arguments="`"$log`"";if($normal){$arguments+=' --normal-sentinel'}
  $p=Start-Process "$D\vi_focusprobe$arch.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru
  $h=[IntPtr]::Zero;$until=[Environment]::TickCount64+2000
  do{$text=Get-Content $log -Raw -ErrorAction SilentlyContinue;if($text -match 'WINDOW hwnd=0x([0-9A-F]+)'){$h=[IntPtr]::new([Convert]::ToInt64($Matches[1],16));break};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until)
  [uint32]$owner=0;[ViSentinelStyleNative]::GetWindowThreadProcessId($h,[ref]$owner)|Out-Null;$style=[ViSentinelStyleNative]::GetWindowLongPtrW($h,-20).ToInt64();$noActivate=($style-band0x08000000)-ne0
  $pass=$h-ne[IntPtr]::Zero -and $owner-eq$p.Id -and $noActivate-eq(-not $normal)
  $results+=[pscustomobject]@{arch=$arch;mode=$mode;pid=$p.Id;hwnd=('0x{0:X}'-f$h.ToInt64());actual_owner=$owner;exstyle=('0x{0:X}'-f$style);noactivate=$noActivate;pass=$pass}
  "$(if($pass){'PASS'}else{'FAIL'}) $arch $mode created exstyle=0x$($style.ToString('X')) noactivate=$noActivate"
  if(-not $pass){++$failed;if(-not $KeepGoing){throw 'sentinel construction style mismatch'}}
 }finally{if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force}}
}}
$results|ConvertTo-Json -Depth 5|Set-Content "$Out\result.json";"$($results.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
