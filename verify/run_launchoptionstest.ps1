param([string]$Out="$PSScriptRoot\..\build\test\launchoptionstest",[string]$CliDir="$PSScriptRoot\..\dist",[switch]$ExpectRed)
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path;$CliDir=(Resolve-Path -LiteralPath $CliDir).Path;$Out=[IO.Path]::GetFullPath($Out)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
New-Item -ItemType Directory -Force -Path $Out,"$repo\build\temp"|Out-Null
$env:TEMP="$repo\build\temp";$env:TMP=$env:TEMP
if(-not('ViLaunchMeasure'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViLaunchMeasure{
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowDpiAwarenessContext(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr h);
 public static Rect[] Measure(IntPtr h){var old=SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));try{Rect a,b;GetClientRect(h,out a);GetWindowRect(h,out b);return new[]{a,b};}finally{if(old!=IntPtr.Zero)SetThreadDpiAwarenessContext(old);}}
}
'@}
$checks=[Collections.Generic.List[object]]::new();$proofs=[Collections.Generic.List[object]]::new()
function Check([bool]$Condition,[string]$Name){$checks.Add([pscustomobject]@{pass=$Condition;name=$Name});if($Condition){"PASS $Name"}else{"FAIL $Name"}}
function Own-Launch([string]$Cli,[string[]]$Arguments,[string]$Tag){
 $info=[Diagnostics.ProcessStartInfo]::new($Cli);$info.WorkingDirectory=$repo;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;$info.Environment['NoDefaultCurrentDirectoryInExePath']='1'
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 try{
  if(-not $process.Start()){throw 'Could not start own CLI'}
  # A launched live child can retain the CLI's stdout handle. Read the one JSON
  # line, not EOF, so the harness can inspect and clean up that still-running child.
  $stdout=$process.StandardOutput.ReadLineAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $finished=$process.WaitForExit(15000);if(-not $finished){$process.Kill();$process.WaitForExit()}
  $outText='';if($stdout.Wait(1000)){$outText=[string]$stdout.GetAwaiter().GetResult()}
  $errText=if($stderr.IsCompleted){$stderr.GetAwaiter().GetResult()}else{'stderr remains inherited by live child; no EOF wait performed'}
  [IO.File]::WriteAllText("$Out\$Tag.stdout.json",$outText);[IO.File]::WriteAllText("$Out\$Tag.stderr.log",$errText)
  $json=$null;if($outText.Trim()){$json=$outText|ConvertFrom-Json}
  [pscustomobject]@{finished=$finished;exit=$process.ExitCode;json=$json;stdout=$outText}
 }finally{$process.Dispose()}
}
foreach($arch in @('x64','x86')){
 $dir="$Out\$arch";New-Item -ItemType Directory -Force -Path $dir,"$dir\child cwd"|Out-Null
 $vcvars=if($arch -eq 'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$cli=Join-Path $CliDir $(if($arch -eq 'x64'){'vinput.exe'}else{'vinput32.exe'})
 $batch=@"
@echo off
setlocal
set "TEMP=$repo\build\temp"
set "TMP=$repo\build\temp"
call "$vcDir\$vcvars" >nul || exit /b 2
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /O2 "$repo\test\vi_launchoptionstest.cpp" /Fo:"$dir\fixture.obj" /Fe:"$dir\fixture.exe" user32.lib || exit /b 2
exit /b 0
"@
 [IO.File]::WriteAllText("$dir\build.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$dir\build.cmd" *> "$dir\build.log"
 if($LASTEXITCODE -or (Get-Content "$dir\build.log" -Raw)-match '(?im)warning [A-Z]+\d+|error [A-Z]+\d+'){throw 'Owned launch fixture build failed'}
 $dll=Join-Path "$repo\dist" $(if($arch -eq 'x64'){'vinput64.dll'}else{'vinput32.dll'})
 $marker="marker_$([guid]::NewGuid().ToString('N')).txt";Set-Content -LiteralPath "$dir\child cwd\$marker" -Value 'OWN CHILD DIRECTORY'
 foreach($mode in @('cwd','size','override','splash','splashmain')){
  if($ExpectRed -and $mode -notin @('cwd','size')){continue}
  $log="$dir\$mode.log";$began=[DateTime]::UtcNow;$reply=$null
  try{
   $relative=[IO.Path]::GetRelativePath($repo,"$dir\fixture.exe")
   $arguments=@('--json','launch','--exe',$relative,'--args',"`"$log`" `"$marker`" $mode",'--dll',$dll,'--fg-guard','0','--settle','1','--timeout','5')
   if($mode -eq 'cwd'){$arguments+=@('--cwd',"$dir\child cwd",'--no-inject')}
   else{$arguments+=@('--client-size','640x360')}
   $location=(Get-Location).ProviderPath;$nativeCwd=[Environment]::CurrentDirectory
   $reply=Own-Launch $cli $arguments "$arch-$mode"
   if(-not $reply.finished -or -not $reply.json.ok){throw "Owned target launch failed: $($reply.stdout)"}
   $body=Get-Content -LiteralPath $log -Raw;$proofs.Add([pscustomobject]@{arch=$arch;mode=$mode;reply=$reply.json;fixture=$body})
   Check ((Get-Location).ProviderPath -ceq $location -and [Environment]::CurrentDirectory -ceq $nativeCwd) "$arch $mode launch preserves caller directory"
   if($mode -eq 'cwd'){
    $actual=([regex]::Match($body,'(?m)^cwd=(.*)')).Groups[1].Value.TrimEnd("`r")
    Check ($actual -ceq "$dir\child cwd" -and $body -match '(?m)^marker=1' -and $body -match '(?m)^marker_text=OWN CHILD DIRECTORY') "$arch child gets explicit cwd and reads relative marker only there"
    if(-not $ExpectRed){Check ($reply.json.cwd -ceq "$dir\child cwd" -and $reply.json.cwd_source -eq 'explicit') "$arch JSON reports exact explicit child cwd"}
   }else{
    $hwndText=([regex]::Match($body,'(?m)^main_hwnd=(0x[0-9A-Fa-f]+)')).Groups[1].Value
    $window=[IntPtr][Convert]::ToInt64($hwndText.Substring(2),16);$measure=[ViLaunchMeasure]::Measure($window);$client=$measure[0];$outer=$measure[1]
    [uint32]$foregroundPid=0;[void][ViLaunchMeasure]::GetWindowThreadProcessId([ViLaunchMeasure]::GetForegroundWindow(),[ref]$foregroundPid)
    if($mode -in @('size','splashmain')){
     Check ($client.right -eq 640 -and $client.bottom -eq 360) "$arch independent client rect reaches640x360 including menu/borders"
     if(-not $ExpectRed){Check ($reply.json.client_size.applied -and $reply.json.client_size.hwnd -eq $hwndText -and $reply.json.client_size.observed_w -eq $client.right -and $reply.json.client_size.observed_h -eq $client.bottom -and $reply.json.client_size.one_shot) "$arch geometry JSON names actual adopted HWND and measured one-shot result"}
     if($mode -eq 'splashmain'){
      $splashText=([regex]::Match($body,'(?m)^splash_hwnd=(0x[0-9A-Fa-f]+)')).Groups[1].Value;$splash=[IntPtr][Convert]::ToInt64($splashText.Substring(2),16)
      $splashArea=[ViLaunchMeasure]::Measure($splash)[0]
      $splashWidth=[int]([regex]::Match($body,'(?m)^splash_client_w=(\d+)')).Groups[1].Value;$splashHeight=[int]([regex]::Match($body,'(?m)^splash_client_h=(\d+)')).Groups[1].Value
      Check ($reply.json.client_size.window_class -eq 'ViLaunchMain' -and $splashArea.right -eq $splashWidth -and $splashArea.bottom -eq $splashHeight) "$arch splash-first launch sizes adopted main without inflating splash"
     }
    }elseif($mode -eq 'splash'){
     Check ($reply.exit -eq 0 -and $reply.json.ok -and -not $reply.json.client_size.request_accepted -and -not $reply.json.client_size.applied -and $reply.json.client_size.warning -match 'splash') "$arch known splash is left alone with explicit running-target warning"
    }else{
     $originalWidth=[int]([regex]::Match($body,'(?m)^initial_client_w=(\d+)')).Groups[1].Value;$originalHeight=[int]([regex]::Match($body,'(?m)^initial_client_h=(\d+)')).Groups[1].Value
     Check ($reply.exit -eq 0 -and $reply.json.ok -and -not $reply.json.client_size.applied -and $reply.json.client_size.warning -match 'did not retain' -and $client.right -eq $originalWidth -and $client.bottom -eq $originalHeight) "$arch application override is warning with running target, not duplicate-launch failure"
    }
    $initialX=[int]([regex]::Match($body,'(?m)^initial_x=(-?\d+)')).Groups[1].Value;$initialY=[int]([regex]::Match($body,'(?m)^initial_y=(-?\d+)')).Groups[1].Value
    Check ($outer.left -eq $initialX -and $outer.top -eq $initialY -and $outer.left -lt -20000 -and [ViLaunchMeasure]::IsWindowVisible($window) -and $foregroundPid -ne $reply.json.pid) "$arch $mode keeps offscreen position/visibility and target background"
   }
  }finally{
   $ownedPid=0;if($reply -and $reply.json.pid){$ownedPid=[int]$reply.json.pid}
   if(-not $ownedPid -and (Test-Path -LiteralPath $log)){try{$raw=Get-Content -LiteralPath $log -Raw;$ownedPid=[int]([regex]::Match($raw,'(?m)^pid=(\d+)')).Groups[1].Value}catch{}}
   if($ownedPid){$child=Get-Process -Id $ownedPid -ErrorAction SilentlyContinue;if($child -and $child.Path -eq "$dir\fixture.exe" -and $child.StartTime.ToUniversalTime() -ge $began){Stop-Process -Id $ownedPid -Force -Confirm:$false}}
  }
 }
 if(-not $ExpectRed){
  foreach($bad in @(@('--client-size','0x20'),@('--client-size','20x0'),@('--client-size','640,360'),@('--client-size','640x360junk'),@('--client-size','8193x20'),@('--client-size','-1x20'),@('--cwd',"$dir\missing"),@('--cwd',"$dir\fixture.exe"),@('--client-size','20x20','--no-inject'),@('--cwd',"$dir\child cwd",'--steam','1','--wait-exe','fixture.exe'))){
   $privateLock="$dir\invalid_$([guid]::NewGuid().ToString('N')).lock";$ledger="$dir\invalid_$([guid]::NewGuid().ToString('N')).log"
   $reply=Own-Launch $cli (@('--json','launch','--exe',"$dir\fixture.exe",'--args',"`"$ledger`" `"$marker`" invalid",'--lock',$privateLock,'--lock-timeout','1')+$bad) "$arch-invalid-$($checks.Count)"
   Check ($reply.finished -and $reply.exit -eq 2 -and -not $reply.json.ok -and -not $reply.json.pid -and -not (Test-Path -LiteralPath $privateLock) -and -not (Test-Path -LiteralPath $ledger)) "$arch invalid cwd/size rejected before lock or target startup: $($bad -join ' ')"
  }
 }
}
$failed=@($checks|Where-Object{-not $_.pass}).Count
[pscustomobject]@{ok=($failed -eq 0);checks=$checks;proofs=$proofs;failed=$failed;expected_red=[bool]$ExpectRed}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath "$Out\result.json"
if($ExpectRed){if($failed -lt 4){throw 'Baseline unexpectedly supports cwd/client size'};'EXPECTED RED: cwd/client-size missing on baseline';$global:LASTEXITCODE=0}
elseif($failed){throw "$failed launch-option controls failed"}
else{$global:LASTEXITCODE=0}
