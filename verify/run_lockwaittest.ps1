param([string]$Out="$PSScriptRoot\..\build\test\lockwaittest",[string]$CliDir="$PSScriptRoot\..\dist",[switch]$ExpectRed)
$ErrorActionPreference='Stop'
$Out=[IO.Path]::GetFullPath($Out);$CliDir=(Resolve-Path -LiteralPath $CliDir).Path
New-Item -ItemType Directory -Force -Path $Out|Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Check([bool]$Condition,[string]$Name){
 $checks.Add([pscustomobject]@{pass=$Condition;name=$Name})
 if($Condition){"PASS $Name"}else{"FAIL $Name"}
}
function Run-OwnCli([string]$Cli,[string[]]$Arguments,[string]$Tag,[int]$LimitMs=3500){
 $info=[Diagnostics.ProcessStartInfo]::new($Cli)
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 $clock=[Diagnostics.Stopwatch]::StartNew()
 try{
  if(-not $process.Start()){throw 'Could not start own CLI'}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $finished=$process.WaitForExit($LimitMs)
  if(-not $finished){$process.Kill();$process.WaitForExit()}
  $clock.Stop();$outText=$stdout.GetAwaiter().GetResult();$errText=$stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText("$Out\$Tag.stdout.json",$outText)
  [IO.File]::WriteAllText("$Out\$Tag.stderr.log",$errText)
  $json=$null;if($outText.Trim()){$json=$outText|ConvertFrom-Json}
  [pscustomobject]@{finished=$finished;exit=$process.ExitCode;elapsed_ms=$clock.ElapsedMilliseconds;json=$json;stdout=$outText;stderr=$errText}
 }finally{$process.Dispose()}
}
foreach($arch in @('x64','x86')){
 $cli=Join-Path $CliDir $(if($arch -eq 'x64'){'vinput.exe'}else{'vinput32.exe'})
 $lock=Join-Path $Out "owned_$([guid]::NewGuid().ToString('N')).lock"
 $missing=Join-Path $Out "must_never_launch_$([guid]::NewGuid().ToString('N')).exe"
 $body="{`"pid`":$PID,`"owner`":`"lock-wait-$arch`",`"secret`":`"DO_NOT_PRINT_SECRET`"}"
 [IO.File]::WriteAllText($lock,$body)
 try{
  $base=@('--json','launch','--exe',$missing,'--lock',$lock)
  foreach($budgetFlag in @('--lock-timeout','--lock-max-wait')){
   $tag="$arch-$($budgetFlag.TrimStart('-'))"
   $reply=Run-OwnCli $cli ($base+@($budgetFlag,'1','--lock-retry','900')) $tag
   Check ($reply.finished -and $reply.exit -eq 3 -and $reply.elapsed_ms -ge 800 -and $reply.elapsed_ms -lt 2500) "$arch $budgetFlag bounds a 900-second interval to one second"
   if(-not $ExpectRed){
    Check ($reply.json.ok -eq $false -and $reply.json.lock_timed_out -and $reply.json.lock_attempts -eq 1 -and $reply.json.lock_wait_ms -ge 800) "$arch $budgetFlag reports actual timeout and attempt count"
    Check ($reply.json.lock_holder_hint -match 'holder UNVERIFIED' -and $reply.json.lock_holder_hint -match "raw_pid=$PID" -and $reply.json.lock_holder_hint -match "lock-wait-$arch" -and $reply.stdout -notmatch 'DO_NOT_PRINT_SECRET') "$arch $budgetFlag exposes bounded unverified holder hint without arbitrary lock data"
    Check (@($reply.stdout.Trim().Split("`n")).Count -eq 1 -and $reply.stderr -match 'retry interval=900 s' -and $reply.stderr -match 'next attempt in \d+ ms') "$arch $budgetFlag emits one JSON and explicit interval/capped sleep"
   }
   Check ([IO.File]::ReadAllText($lock) -ceq $body) "$arch $budgetFlag preserves foreign lock bytes"
  }
  if(-not $ExpectRed){
   foreach($bad in @(@('--lock-timeout','-1'),@('--lock-timeout','1.5'),@('--lock-timeout','abc'),@('--lock-timeout','86401'),@('--lock-timeout'),@('--lock-retry','0'),@('--lock-retry','1x'),@('--lock-timeout','1','--lock-max-wait','2'))){
    $reply=Run-OwnCli $cli ($base+$bad) "$arch-invalid-$($checks.Count)"
    Check ($reply.finished -and $reply.exit -eq 2 -and $reply.json.ok -eq $false -and $reply.elapsed_ms -lt 1500 -and [IO.File]::ReadAllText($lock) -ceq $body) "$arch rejects malformed/conflicting wait options before lock changes: $($bad -join ' ')"
   }
  }
 }finally{if(Test-Path -LiteralPath $lock){Remove-Item -LiteralPath $lock}}
}
$failed=@($checks|Where-Object{-not $_.pass}).Count
[pscustomobject]@{ok=($failed -eq 0);checks=$checks;failed=$failed;expected_red=[bool]$ExpectRed}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$Out\result.json"
if($ExpectRed){if($failed -lt 2){throw 'Baseline unexpectedly passed bounded waiting controls'};'EXPECTED RED: old CLI exceeds requested bounded wait; only own waiter PIDs killed';$global:LASTEXITCODE=0}
elseif($failed){throw "$failed lock-wait controls failed"}
else{$global:LASTEXITCODE=0}
