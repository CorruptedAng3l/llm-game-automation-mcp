param([string]$Out="$PSScriptRoot\..\build\test\raw_registration\after",[string]$Vinput='',
 [ValidateSet('pre','late','alternate','remove','failed','buffer')][string[]]$Modes=@('pre','late','alternate','remove','failed','buffer'),[ValidateSet('x64','x86')][string[]]$Architectures=@('x64','x86'),[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$dist=Join-Path $root 'dist';$cli=if($Vinput){(Resolve-Path $Vinput).Path}else{Join-Path $dist 'vinput.exe'}
$env:NoDefaultCurrentDirectoryInExePath='1';$env:TEMP=Join-Path $root 'build\temp';$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $Out,$env:TEMP|Out-Null;$Out=(Resolve-Path $Out).Path
if(-not('ViRawControl' -as [type])){Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class ViRawControl{[DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);}'}
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=@();$fails=0
function Check($pass,$name){$checks.Add([pscustomobject]@{pass=[bool]$pass;name=$name});Write-Host "$(if($pass){'PASS'}else{'FAIL'}) $name";if(-not$pass){$script:fails++;if(-not$KeepGoing){throw $name}}}
function Read-Log($path){if(-not(Test-Path -LiteralPath $path)){return ''};$stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete);$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}}
function Wait-Log($path,$pattern,[int]$start=0,[int]$timeout=2000){$until=[Environment]::TickCount64+$timeout;do{$text=Read-Log $path;if($text.Length-ge$start-and$text.Substring($start)-match$pattern){return $true};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$until);return $false}
function Vi($targetPid,[string[]]$arguments){$text=&$cli --json --pid $targetPid @arguments;$exit=$LASTEXITCODE;return [pscustomobject]@{exit=$exit;reply=($text|ConvertFrom-Json)}}
function Command($window,$targetPid,$number){$actual=[uint32]0;[void][ViRawControl]::GetWindowThreadProcessId($window,[ref]$actual);if($actual-ne$targetPid){throw 'owned raw fixture HWND changed'};if(-not[ViRawControl]::PostMessageW($window,0x8000+$number,[UIntPtr]::Zero,[IntPtr]::Zero)){throw 'owned raw fixture control post failed'}}
try{
 foreach($arch in $Architectures){foreach($mode in $Modes){
  $name="$arch.$mode";$log=Join-Path $Out ($name+'.log');$exe=Join-Path $dist $(if($arch-eq'x64'){'vi_rawonly.exe'}else{'vi_rawonly32.exe'});$p=Start-Process -FilePath $exe -ArgumentList @(('"'+$log+'"'),$mode) -WindowStyle Hidden -PassThru;$identity=[pscustomobject]@{process=$p;pid=$p.Id;path=$exe;creation=$p.StartTime.ToUniversalTime().Ticks};$owned+=,$identity;$status=$null
  try{
   Check (Wait-Log $log 'READY main=0x[0-9A-F]+ alternate=0x[0-9A-F]+\r?\n') "$name raw-only fixture ready without any desktop input"
   Check ((Read-Log $log)-match'INVALID_HANDLE result=4294967295 error=6') "$name pre-inject invalid-handle native control returns6"
   $inject=&$cli --json inject --pid $p.Id|ConvertFrom-Json;Check ($LASTEXITCODE-eq0-and$inject.ok) "$name inject succeeds"
   $probe=(Vi $p.Id @('status')).reply;Check ($probe.ok-and$probe.hooks_ok.Contains('GetRawInputData ')) "$name external status confirms the synthetic-handle decoder hook"
   $match=[regex]::Match((Read-Log $log),'READY main=0x([0-9A-F]+) alternate=0x[0-9A-F]+\r?\n');$barrierWindow=[IntPtr][Convert]::ToInt64($match.Groups[1].Value,16);Command $barrierWindow $p.Id 10
   Check (Wait-Log $log 'INJECTED_READY') "$name confirms GetRawInputData hook before synthetic handles"
   for($adopt=0;$adopt-lt100;$adopt++){$status=(Vi $p.Id @('status')).reply;if($status.hwnd-and$status.hwnd-ne'0x0'){break};Start-Sleep -Milliseconds 25};$record=[ordered]@{case=$name;pid=$p.Id;before=$status};$evidence.Add($record);$window=[IntPtr][Convert]::ToInt64($status.hwnd,16)
   Check ($status.ok-and-not$status.really_foreground-and$status.isolate) "$name receiver remains background-isolated"
   $registered=$mode-notin@('remove','failed');Check ($status.raw_mouse-eq$registered) "$name live registration matches successful registration/remove state"
   Check ($status.raw_registration.query_ok) "$name fresh authoritative registration query reports success"
   if($mode-eq'failed'){Check ((Read-Log $log)-match'REGISTER result=0 error=87 flags=1'-and$status.counters.reg_raw_failed-ge1-and$status.counters.reg_raw_ok-eq0) "$name failed registration preserves native error87 and cannot invent subscription"}
   if($mode-eq'late'-or$mode-eq'remove'){Check ($status.counters.reg_raw_ok-ge1) "$name successful post-injection registration/removal is accounted after OS success"}
   if($registered){Check ($status.raw_registration.mouse_target_owned-and$status.raw_registration.mouse_target-ne'0x0') "$name chosen registered raw target is a live owned HWND"}
   $offset=(Read-Log $log).Length;$move=Vi $p.Id @('move','37','-19','--steps','1');Check ($move.exit-eq0-and$move.reply.ok) "$name virtual movement command accepted"
   if($mode-eq'buffer'){
    if($arch-eq'x64'){Command $window $p.Id 4;Check (Wait-Log $log 'BUFFER_SMALL count=4294967295 error=122 needed=48 untouched=1' $offset) "$name too-small native buffer preserves all canaries and reports required synthetic size";Command $window $p.Id 1;$expected='RAW_MOUSE source=buffer receiver=main dx=37 dy=-19'}else{$expected='RAW_MOUSE source=post receiver=main dx=37 dy=-19'}
    Check (Wait-Log $log $expected $offset) "$name auto delivers actual payload through supported backend after a recent buffer probe"
    if($arch-eq'x64'){Check (Wait-Log $log 'BUFFER_DRAIN count=1 error=0 returned_bytes=0 canary=1 caller_capacity=8192' $offset) "$name saved caller capacity permits one aligned append after native cleared pcb"}
    $record.after_auto=(Vi $p.Id @('status')).reply
    if($arch-eq'x64'){Check ($record.after_auto.counters.rib_injected-eq1-and$record.after_auto.counters.raw_queued-eq1) "$name exactly one queued record is returned after the small-buffer retry"}else{Check ($record.after_auto.counters.raw_queued-eq0-and$record.after_auto.counters.raw_posted-eq1) "$name x86 supported auto path posts once and never queues an unsupported record"}
    if($arch-eq'x86'){$unsupported=Vi $p.Id @('config','raw=buffer');Check ($unsupported.exit-ne0-and-not$unsupported.reply.ok-and$unsupported.reply.error-match'x86|32-bit') "$name explicit unsupported buffer mode is rejected before input"}
    [void](Vi $p.Id @('config','raw=auto'));Start-Sleep -Milliseconds 3300;$offset=(Read-Log $log).Length;[void](Vi $p.Id @('move','13','17','--steps','1'))
    Check (Wait-Log $log 'RAW_MOUSE source=post receiver=main dx=13 dy=17' $offset) "$name expired buffer observation returns auto to posted payloads without duplicates"
    $offset=(Read-Log $log).Length;Command $window $p.Id 3;Check (Wait-Log $log 'BUFFER_BAD_HEADER count=4294967295 error=87' $offset) "$name failed native buffer query preserves original invalid-parameter error"
    [void](Vi $p.Id @('move','17','9','--steps','1'));Check (Wait-Log $log 'RAW_MOUSE source=post receiver=main dx=17 dy=9' $offset) "$name failed or unsupported buffer calls do not renew automatic buffered routing"
   }elseif($registered){$receiver=if($mode-eq'alternate'){'alternate'}else{'main'};Check (Wait-Log $log "RAW_MOUSE source=post receiver=$receiver dx=37 dy=-19" $offset) "$name WM_INPUT-only consumer reads exact auto payload at correct owned receiver";if($mode-eq'alternate'){Check (-not((Read-Log $log).Substring($offset)-match'RAW_MOUSE .*receiver=main')) "$name alternate target receives once without duplicate main delivery"}}
   else{Start-Sleep -Milliseconds 200;Check (-not((Read-Log $log).Substring($offset)-match'RAW_MOUSE')) "$name auto respects removed/absent registration and emits no raw record"}
   [void](Vi $p.Id @('config','raw=post'));$offset=(Read-Log $log).Length;[void](Vi $p.Id @('move','11','7','--steps','1'))
   Check (Wait-Log $log 'RAW_MOUSE source=post receiver=(main|alternate) dx=11 dy=7' $offset) "$name explicit raw=post delivers exact synthetic payload"
   [void](Vi $p.Id @('config','raw=off'));$offset=(Read-Log $log).Length;[void](Vi $p.Id @('move','19','21','--steps','1'));Start-Sleep -Milliseconds 150
   Check (-not((Read-Log $log).Substring($offset)-match'RAW_MOUSE')) "$name explicit raw=off prevents raw delivery"
   $offset=(Read-Log $log).Length;Command $window $p.Id 2;Check (Wait-Log $log 'INVALID_HANDLE result=4294967295 error=5' $offset) "$name isolated real-handle policy denies before native read (invalid-handle branch control only)"
   [void](Vi $p.Id @('config','isolate=false'));$offset=(Read-Log $log).Length;Command $window $p.Id 2;Check (Wait-Log $log 'INVALID_HANDLE result=4294967295 error=6' $offset) "$name isolate=false preserves native invalid-handle passthrough"
   [void](Vi $p.Id @('config','isolate=true'));$record.after=(Vi $p.Id @('status')).reply
   $doctor=Vi $p.Id @('doctor');if($mode-eq'remove'){Check (($doctor.reply.suggestions-join' ')-match'Auto respects removal') "$name doctor distinguishes observed removal from unknown intent"};Check ($doctor.reply.ok-and$doctor.reply.raw_registration.query_ok) "$name doctor exposes truthful current raw registration diagnostics"
  }finally{if($status-and$status.log_path-and(Test-Path -LiteralPath $status.log_path)){Copy-Item -LiteralPath $status.log_path -Destination (Join-Path $Out ($name+'.vinput.log'))};$fresh=Get-Process -Id $p.Id -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$exe-and$fresh.StartTime.ToUniversalTime().Ticks-eq$identity.creation){Stop-Process -Id $p.Id -Force -Confirm:$false}}
 }}
}finally{$checks|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Out 'checks.json');$evidence|ConvertTo-Json -Depth 18|Set-Content (Join-Path $Out 'evidence.json');foreach($item in $owned){$fresh=Get-Process -Id $item.pid -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$item.path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$item.creation){Stop-Process -Id $item.pid -Force -Confirm:$false}}}
Write-Host "$($checks.Count-$fails) PASS / $fails FAIL";if($fails){exit 1};exit 0
