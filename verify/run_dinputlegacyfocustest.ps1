# Owned background DirectInput 1-7 A/W/CreateEx focus and event-stride controls.
param([string]$Out="$PSScriptRoot\..\build\test\legacy_input\focus_after",[string]$Vinput='',
      [ValidateSet('a','w','exa','exw','8a','8w')][string[]]$Apis=@('a','w','exa','exw','8a','8w'),
      [ValidateSet('x64','x86')][string[]]$Architectures=@('x64','x86'),[switch]$KeepGoing)
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path;$dist=Join-Path $root 'dist'
. "$PSScriptRoot\vi_physicalwindow.ps1"
. "$PSScriptRoot\vi_logwindow.ps1"
$cli=if($Vinput){(Resolve-Path $Vinput).Path}else{Join-Path $dist 'vinput.exe'}
$env:NoDefaultCurrentDirectoryInExePath='1';$env:TEMP=Join-Path $root 'build\temp';$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP,$Out|Out-Null;$Out=(Resolve-Path $Out).Path
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$nativeWindows=[Collections.Generic.List[object]]::new();$owned=@();$failures=0;$notMeasured=0;$finishedAll=$false
function Require($pass,$name){$checks.Add([pscustomobject]@{pass=[bool]$pass;name=$name});Write-Host "$(if($pass){'PASS'}else{'FAIL'}) $name";if(-not$pass){$script:failures++;if(-not$KeepGoing){throw $name}}}
function Read-Log($path){if(-not(Test-Path -LiteralPath $path)){return ''};$stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete);$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}}
function Wait-Log($path,$pattern,[int]$start=0,[int]$timeout=2000){$until=[Environment]::TickCount64+$timeout;do{$text=Read-Log $path;if($text.Length-ge$start-and$text.Substring($start)-match$pattern){return $true};Start-Sleep -Milliseconds 25}while([Environment]::TickCount64-lt$until);return $false}
function Vi($targetPid,[string[]]$arguments){$text=&$cli --json --pid $targetPid @arguments;$exit=$LASTEXITCODE;$reply=$text|ConvertFrom-Json;if($exit-ne0-or-not$reply.ok){throw "CLI failed ($exit) $($arguments-join' '): $text"};return $reply}
try{
 foreach($arch in $Architectures){foreach($api in $Apis){
  $modes=@(@{name='state';buffer='state';stride=16},@{name='buffer16';buffer='buffered';stride=16})
  if($api-like'8*'){$modes=@(@{name='buffer_native';buffer='buffered';stride=$(if($arch-eq'x64'){24}else{20})})}
  foreach($mode in $modes){
   $name="$arch.$api.$($mode.name)";$log=Join-Path $Out ($name+'.log');$exe=Join-Path $dist $(if($arch-eq'x64'){'vi_dinputlegacy_focus.exe'}else{'vi_dinputlegacy_focus32.exe'})
   $process=Start-Process -FilePath $exe -ArgumentList @(('"'+$log+'"'),$api,$mode.buffer,$mode.stride) -WindowStyle Hidden -PassThru
   $identity=[pscustomobject]@{process=$process;pid=$process.Id;path=$exe;creation=$process.StartTime.ToUniversalTime().Ticks};$owned+=,$identity
   try{
    Require (Wait-Log $log 'READY .*real_foreground_before_inject=0') "$name native target starts offscreen without real foreground"
    $nativeFailure=Wait-Log $log 'STATUS k_acq=8[0-9A-F]{7} m_acq=8[0-9A-F]{7} k_state=8[0-9A-F]{7} m_state=8[0-9A-F]{7}'
    Require $nativeFailure "$name native foreground-cooperative Acquire/state fail while background"
    if(-not$nativeFailure){throw 'native failure control unavailable; abort this case'}
    $verdict='not_measured'
    for($attempt=1;$attempt-le3;$attempt++){
     $windowStart=[ViLogWindow]::Read($log);$witness=[ViPhysicalWindow]::Sample(350);$logWindow=[ViLogWindow]::Finish($log,$windowStart);$windowText=$logWindow.text
     $inputSeen=$windowText-match'KEY |MOUSE b0|DELTA |BUFFER_MOUSE';$verdict=if($logWindow.valid){Get-ViNativeWindowVerdict $witness $inputSeen}else{'not_measured'}
     $nativeWindows.Add([pscustomobject]@{case=$name;attempt=$attempt;status=$verdict;witness_source='uninjected_GetLastInputInfo_and_GetAsyncKeyState';physical=$witness;input_seen=$inputSeen;log_snapshot=$logWindow;log_window=$windowText})
     if($verdict-ne'not_measured'){break};Start-Sleep -Milliseconds 100
    }
    if($verdict-eq'not_measured'){$notMeasured++;$checks.Add([pscustomobject]@{pass=$null;status='not_measured';name="$name no physical or virtual input before injection";reason='physical activity, unavailable witness, or invalid log contaminated all three bounded windows'});Write-Host "NOT MEASURED $name native no-input control: physical witness/log unavailable after3 bounded attempts"}
    else{Require ($verdict-eq'passed') "$name no physical or virtual input in a measured quiet pre-injection window"}
    $inject=&$cli --json inject --pid $process.Id|ConvertFrom-Json;Require ($LASTEXITCODE-eq0-and$inject.ok) "$name inject succeeds"
    $zero='STATUS k_acq=[0-7][0-9A-F]{7} m_acq=[0-7][0-9A-F]{7} k_state=[0-7][0-9A-F]{7} m_state=[0-7][0-9A-F]{7} zero_k=1 zero_m=1'
    $offset=(Read-Log $log).Length
    Require (Wait-Log $log $zero $offset 3500) "$name fakefocus supplies successful Acquire and zero-filled keyboard/mouse state"
    $status=Vi $process.Id @('status');Require (-not$status.really_foreground-and$status.fakefocus-and$status.isolate) "$name remains background-isolated with fakefocus"
    $offset=(Read-Log $log).Length;Vi $process.Id @('down','a')|Out-Null;Vi $process.Id @('down','lbutton')|Out-Null
    Require (Wait-Log $log 'KEY dik=0x1E down' $offset) "$name virtual key down consumed by real legacy interface"
    Require (Wait-Log $log 'MOUSE b0 down' $offset) "$name virtual mouse down consumed by real legacy interface"
    Vi $process.Id @('up','a')|Out-Null;Vi $process.Id @('up','lbutton')|Out-Null
    Require (Wait-Log $log 'KEY dik=0x1E up' $offset) "$name virtual key up consumed by real legacy interface"
    Require (Wait-Log $log 'MOUSE b0 up' $offset) "$name virtual mouse up consumed by real legacy interface"
    if($mode.buffer-eq'buffered'){Require ((Read-Log $log)-notmatch'FAIL ABI') "$name cbObjectData=$($mode.stride) respects stride and canaries"}
    $offset=(Read-Log $log).Length;Vi $process.Id @('config','fakefocus=false')|Out-Null
    Require (Wait-Log $log 'STATUS k_acq=8[0-9A-F]{7} m_acq=8[0-9A-F]{7} k_state=8[0-9A-F]{7} m_state=8[0-9A-F]{7}' $offset) "$name explicit fakefocus=false preserves native acquisition/state failure"
    $offset=(Read-Log $log).Length;Vi $process.Id @('config','fakefocus=true')|Out-Null
    Require (Wait-Log $log $zero $offset) "$name fakefocus re-enable restores successful zero state"
    $after=Vi $process.Id @('status');Require (-not$after.really_foreground-and$after.counters.di_fake-gt0) "$name fake-input counters prove successful background fallback"
    $evidence.Add([pscustomobject]@{case=$name;identity=$identity|Select-Object pid,path,creation;before=$status;after=$after})
    if($after.log_path){Copy-Item -LiteralPath $after.log_path -Destination (Join-Path $Out ($name+'.vinput.log'))}
   }finally{
    $fresh=Get-Process -Id $process.Id -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$exe-and$fresh.StartTime.ToUniversalTime().Ticks-eq$identity.creation){Stop-Process -Id $process.Id -Force -Confirm:$false}
   }
  }
 }}
 $finishedAll=$true
}finally{
 foreach($item in $owned){$fresh=Get-Process -Id $item.pid -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$item.path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$item.creation){Stop-Process -Id $item.pid -Force -Confirm:$false}}
 $checks|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Out 'checks.json');$evidence|ConvertTo-Json -Depth 18|Set-Content (Join-Path $Out 'evidence.json')
 $nativeWindows|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Out 'native_windows.json')
 @{schema=1;status=$(if($failures-or-not$finishedAll){'failed'}elseif($notMeasured){'not_measured'}else{'passed'});completed=$finishedAll;failed_check_count=$failures;not_measured_check_count=$notMeasured;positive_case_count=$evidence.Count;checks=$checks}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Out 'result.json')
}
Write-Host "$($checks.Count-$failures-$notMeasured) PASS / $failures FAIL / $notMeasured NOT MEASURED";if($failures){exit 1};if($notMeasured){exit 3};exit 0
