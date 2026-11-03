# Real local/restricted/UNC pipe clients plus an actual AppContainer input/capture target.
# No account/network configuration changes or foreground operations.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\pipe_security\after",[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$D=(Resolve-Path $TargetDir).Path;$V=(Resolve-Path "$CliDir\vinput.exe").Path
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
$tools=Join-Path $Out 'tools';New-Item -ItemType Directory -Force -Path @($tools,"$tools\temp")|Out-Null
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new()
function Check($Ok,[string]$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name";if(-not$Ok-and-not$KeepGoing){throw $Name}}
function Vi($GamePid,[string[]]$Arguments){$reply=& $V --json --pid $GamePid @Arguments|ConvertFrom-Json;if($LASTEXITCODE-ne0-or-not$reply.ok){throw "vinput failed: $Arguments"};return $reply}
function Client([string]$Exe,[int]$GamePid,[string]$Identity='owner',[string]$HostName='.'){
 $start=[Diagnostics.ProcessStartInfo]::new($Exe);$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 foreach($a in @([string]$GamePid,$Identity,$HostName)){[void]$start.ArgumentList.Add($a)}
 $child=[Diagnostics.Process]::Start($start);$read=$child.StandardOutput.ReadToEndAsync();$errors=$child.StandardError.ReadToEndAsync()
 if(-not$child.WaitForExit(5000)){$child.Kill();$child.WaitForExit();return [pscustomobject]@{setup_error=-1;connected=$false;connect_error=-1;ping=$false;unavailable='owned client timed out, not an access-denial result'}}
 $text=$read.GetAwaiter().GetResult();if(-not$text){throw "client emitted no result: $($errors.GetAwaiter().GetResult())"};$result=$text|ConvertFrom-Json;$result|Add-Member -NotePropertyName process_exit -NotePropertyValue $child.ExitCode;return $result
}
try{
 foreach($arch in @('x64','x86')){
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'}
  $batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_pipesecurityclient.cpp" /Fo:"$tools\client$suffix.obj" /Fd:"$tools\client$suffix.pdb" /Fe:"$tools\vi_pipesecurityclient$suffix.exe" || exit /b 2
exit /b 0
"@
  [IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log"
  Check ($LASTEXITCODE-eq0) "$arch independent client builds /W4 /WX"
 }
 foreach($suffix in @('','32')){
  $arch=if($suffix){'x86'}else{'x64'};$client="$tools\vi_pipesecurityclient$suffix.exe"
  $target=Start-Process -FilePath "$D\vi_channelprobe$suffix.exe" -ArgumentList "`"$Out\target$suffix.log`"" -WindowStyle Hidden -PassThru;$owned.Add($target)
  $injected=& $V --json inject --pid $target.Id|ConvertFrom-Json;Check ($LASTEXITCODE-eq0-and$injected.ok) "$arch ordinary injection"
  $local=Client $client $target.Id;Check ($local.setup_error-eq0-and$local.connected-and$local.ping) "$arch actual local owner connects and executes ping"
  Check ($local.dacl_error-eq0-and$local.ace_count-eq2-and$local.dacl.Contains(';;;SY)')-and$local.dacl.Contains(';;;'+$local.token_user_sid+')')-and$local.dacl-notmatch';;;(WD|AU|AC|S-1-15-2-2)\)') "$arch actual ordinary DACL has only SYSTEM and process-user SID"
  $denied=Client $client $target.Id 'deny-owner'
  Check ($denied.setup_error-eq0-and$denied.impersonated-and$denied.reverted-and($denied.token_user_attributes-band16)-ne0) "$arch actual impersonation token has owner SID deny-only"
  Check (-not$denied.connected-and$denied.connect_error-eq5) "$arch owner-SID-disabled actual client denied ACCESS_DENIED"
  $restricted=Client $client $target.Id 'restrict-world'
  Check ($restricted.setup_error-eq0-and$restricted.impersonated-and$restricted.reverted) "$arch actual WD-restricted impersonation established"
  Check (-not$restricted.connected-and$restricted.connect_error-eq5) "$arch WD-restricted actual client denied ACCESS_DENIED"
  # A fresh narrow ping-only reference proves the UNC redirector and credentials
  # work in THIS run. A missing network path is never counted as rejection.
  $referenceOut="$Out\reference$suffix.json";$reference=Start-Process -FilePath $client -ArgumentList '--reference' -WindowStyle Hidden -RedirectStandardOutput $referenceOut -PassThru;$owned.Add($reference)
  $until=[Environment]::TickCount64+3000
  do{$ready=Get-Content -LiteralPath $referenceOut -Raw -ErrorAction SilentlyContinue;if($ready-match'"ready":true'){break};Start-Sleep -Milliseconds 20}while([Environment]::TickCount64-lt$until)
  Check ($ready-match'"ready":true') "$arch owned owner-only ping reference is ready"
  $remoteReference=Client $client $reference.Id 'owner' 'localhost'
  Check ($remoteReference.setup_error-eq0-and$remoteReference.connected-and$remoteReference.ping) "$arch actual localhost UNC reference connects; network availability is independently positive"
  $remote=Client $client $target.Id 'owner' 'localhost'
  Check ($remote.setup_error-eq0-and-not$remote.connected-and$remote.connect_error-eq5) "$arch actual localhost UNC product client rejected ACCESS_DENIED"
  $stillLocal=Client $client $target.Id
  Check ($stillLocal.connected-and$stillLocal.ping) "$arch local owner still works after rejected independent clients"
  $state=Vi $target.Id @('status');Check (-not$state.really_foreground-and-not$state.app_container) "$arch target remains ordinary and background"
  $records.Add(@{arch=$arch;pid=$target.Id;owner=$local;deny_owner=$denied;restricted=$restricted;reference=$remoteReference;remote=$remote;local_after=$stillLocal})
  Stop-Process -Id $reference.Id -Force -Confirm:$false;Stop-Process -Id $target.Id -Force -Confirm:$false
 }
 $acOut=Join-Path $Out 'appcontainer';New-Item -ItemType Directory -Force $acOut|Out-Null
 & icacls $acOut /grant '*S-1-15-2-1:(OI)(CI)M'|Out-Null;if($LASTEXITCODE){throw 'own AppContainer output ACL setup failed'}
 foreach($suffix in @('','32')){
  $arch=if($suffix){'x86'}else{'x64'};$exe="$D\vi_channelprobe$suffix.exe";& icacls $exe /grant '*S-1-15-2-1:(RX)'|Out-Null;if($LASTEXITCODE){throw 'own synthetic fixture read ACL setup failed'}
  $started=& "$D\vi_aclaunch.exe" $exe "$acOut\target$suffix.log"|ConvertFrom-Json;Check ($LASTEXITCODE-eq0-and$started.ok) "$arch actual AppContainer target starts"
  $target=Get-Process -Id $started.pid;$owned.Add($target)
  $injected=& $V --json inject --pid $target.Id|ConvertFrom-Json;Check ($LASTEXITCODE-eq0-and$injected.ok) "$arch actual AppContainer injection and namespaced pipe"
  $state=Vi $target.Id @('status');Check ($state.app_container-and-not$state.really_foreground) "$arch actual target token reports AppContainer in background"
  Vi $target.Id @('key','f8','120')|Out-Null
  $click=Vi $target.Id @('click','left','80','70','--settle-ms','100');Check $click.ok "$arch AppContainer virtual coordinate click"
  $capture=Vi $target.Id @('capture',"$acOut\capture$suffix.png",'--method','printwindow')
  Check ($capture.capture_path-eq'printwindow'-and(Test-Path -LiteralPath "$acOut\capture$suffix.png")) "$arch AppContainer successful capture through namespaced pipe"
  $text=Get-Content -LiteralPath "$acOut\target$suffix.log" -Raw
  Check ($text-match'WM_KEY msg=0x100 vk=0x77'-and$text-match'WM_LBUTTONDOWN lp=80,70') "$arch actual AppContainer consumer observes key and mouse messages"
  $after=Vi $target.Id @('status');Check ($after.counters.gaks-gt0-and$after.counters.captures-gt0) "$arch AppContainer input polling and capture counters are positive"
  $records.Add(@{arch=$arch;appcontainer_pid=$target.Id;profile_sid=$started.appcontainer_sid;before=$state;click=$click;capture=$capture;after=$after})
  Stop-Process -Id $target.Id -Force -Confirm:$false
 }
}finally{foreach($target in $owned){if(-not$target.HasExited){Stop-Process -Id $target.Id -Force -Confirm:$false}};@{checks=$checks;evidence=$records}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json" -Encoding utf8NoBOM}
$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
