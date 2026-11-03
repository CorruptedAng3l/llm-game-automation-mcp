# A fake service applies INSERT before losing/refusing ACK, or hangs UI/status after ACK.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\keytransport",[switch]$KeepGoing)
$ErrorActionPreference='Stop';$D=(Resolve-Path "$PSScriptRoot\..\dist").Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
if(-not ('ViTransportNative' -as [type])){Add-Type 'using System;using System.Text;using System.Runtime.InteropServices;public static class ViTransportNative{[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);}'}
$checks=@();$evidence=@();$owned=@()
function Check($ok,$name){$script:checks+=[pscustomobject]@{pass=[bool]$ok;name=$name};"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not $ok -and -not $KeepGoing){throw $name}}
try{
 foreach($arch in @('','32')){
  foreach($mode in @('drop-ack','server-error','hang-after-ack','hang-preflight')){
   $label="$arch.$mode";$log="$Out\$label.log"
   if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
   $p=Start-Process -FilePath "$D\vi_keytransport$arch.exe" -ArgumentList "`"$log`" --$mode" -WindowStyle Hidden -PassThru;$owned+=$p
   for($n=0;$n -lt 100;$n++){if((Test-Path $log) -and (Get-Content $log -Raw) -match 'PIPE_READY'){break};Start-Sleep -Milliseconds 10}
   $fixtureLog=Get-Content -LiteralPath $log -Raw
   if($fixtureLog -notmatch 'WINDOW_READY[^\r\n]*hwnd=0x([0-9A-Fa-f]+)'){throw 'fake service has no owned window record'}
   $h=[IntPtr]::new([Convert]::ToInt64($Matches[1],16))
   $watch=[Diagnostics.Stopwatch]::StartNew();$raw=& $V --json --pid $p.Id key insert 1 --expect-text 'OVERLAY PANEL' --expect-timeout-ms 1500;$exitCode=$LASTEXITCODE;$watch.Stop()
   $reply=$raw|ConvertFrom-Json;$caption=[Text.StringBuilder]::new(128);[ViTransportNative]::GetWindowText($h,$caption,128)|Out-Null
   $applied=@(Get-Content -LiteralPath $log|Where-Object{$_ -match ' KEY_APPLIED '}).Count
   Check (-not $reply.ok -and $exitCode -ne 0) "$label returns a failing process result"
   if($mode -eq 'drop-ack' -or $mode -eq 'server-error'){
    Check ($applied -eq 1 -and $caption.ToString() -match 'PANEL OPEN') "$label independent UI really applied exactly one INSERT"
    Check ($null -eq $reply.input_injected -and $reply.delivery_unknown) "$label cannot claim noninjection when delivery is ambiguous"
    Check ($reply.input_acknowledged -eq $false -and (($mode -eq 'drop-ack' -and $reply.input_response_received -eq $false) -or ($mode -eq 'server-error' -and $reply.input_response_received -eq $true))) "$label distinguishes lost response from server failure without claiming a successful input ACK"
   }elseif($mode -eq 'hang-after-ack'){
    Check ($applied -eq 1 -and $caption.ToString() -match 'PANEL OPEN' -and $reply.input_injected -and $reply.input_acknowledged) "$label retains acknowledged applied input while UI hangs"
    Check ($reply.expectation.timed_out -and $reply.expectation.polls -eq 0 -and $watch.ElapsedMilliseconds -lt 2200) "$label bounds the post-ACK status read without attempting OCR ($($watch.ElapsedMilliseconds)ms)"
   }else{
    Check ($applied -eq 0 -and $reply.input_injected -eq $false -and -not $reply.delivery_unknown) "$label preflight timeout is known noninjection"
    Check ($watch.ElapsedMilliseconds -lt 1800) "$label preflight has its separate one-second deadline ($($watch.ElapsedMilliseconds)ms)"
   }
   $evidence+=[pscustomobject]@{arch=$arch;mode=$mode;pid=$p.Id;exit=$exitCode;elapsed_ms=$watch.ElapsedMilliseconds;reply=$reply;applied=$applied;actual_window_title=$caption.ToString()}
   if(-not $p.HasExited){Stop-Process -Id $p.Id -Force}
  }
 }
}finally{
 foreach($p in $owned){if(-not $p.HasExited){Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue}}
 $checks|ConvertTo-Json -Depth 6|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 15|Set-Content "$Out\evidence.json"
}
$failed=@($checks|Where-Object{-not $_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
