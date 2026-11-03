# Local owned fake-pipe controls for new optional status fields, both CLI architectures; no OS input.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\doctorhints",[switch]$KeepGoing)
$ErrorActionPreference='Stop';New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$worker=$null
function Check($ok,$name){$checks.Add(@{pass=[bool]$ok;name=$name});"$(if($ok){'PASS'}else{'FAIL'}) $name";if(-not$ok-and-not$KeepGoing){throw $name}}
try{foreach($mode in @('old','humanvirtual','humanreal','unarmed','failed')){
 $ready="$Out\$mode.ready";Remove-Item -LiteralPath $ready -ErrorAction SilentlyContinue
 $worker=Start-Process -FilePath (Get-Command pwsh).Source -ArgumentList "-NoProfile -File `"$PSScriptRoot\vi_doctorfixture.ps1`" -Mode $mode -Ready `"$ready`"" -WindowStyle Hidden -PassThru
 for($i=0;$i-lt100;$i++){if(Test-Path -LiteralPath $ready){break};if($worker.HasExited){throw 'owned fixture exited'};Start-Sleep -Milliseconds 20}
 foreach($cli in @('vinput.exe','vinput32.exe')){
  $V=(Resolve-Path "$CliDir\$cli").Path;$reply=& $V --json --pid $worker.Id doctor|ConvertFrom-Json
  Check ($reply.ok-and$reply.command-eq'doctor') "$cli $mode one available doctor reply"
  $hints=$reply.suggestions-join' '
  if($mode-eq'old'){Check ($null-eq$reply.foreground.user_took_foreground-and$null-eq$reply.cursor.configured-and$null-eq$reply.cursor.effective-and$hints-notmatch'honoring human|Manual mouse control|not observed an external|recorded failed hand-backs') "$cli older status optional fields stay null without invented advice"}
  elseif($mode-eq'humanvirtual'){Check ($reply.foreground.user_took_foreground-and$reply.cursor.configured-eq'virtual'-and$reply.cursor.effective-eq'virtual'-and$hints-match'config cursor=auto'-and$hints-match'Coordinate with the human') "$cli human forced virtual cursor gives manual-control hint"}
  elseif($mode-eq'humanreal'){Check ($reply.foreground.user_took_foreground-and$reply.cursor.configured-eq'auto'-and$reply.cursor.effective-eq'real'-and$hints-notmatch'Manual mouse control still uses') "$cli human auto cursor avoids irrelevant forced-virtual hint"}
  elseif($mode-eq'unarmed'){Check ($reply.foreground.guard.armed-eq$false-and$hints-match'not observed an external restore destination'-and$hints-match'explicit restore only under') "$cli unarmed guard explains missing destination and locked inspection"}
  else{Check ($reply.foreground.guard.failed_count-eq2-and$hints-match'recorded failed hand-backs'-and$hints-match'counter alone does not identify why') "$cli failed guard counter gives qualified log suggestion"}
  if($mode-eq'humanvirtual'){Check ($reply.foreground.user_took_foreground-and$reply.foreground.guard.armed-eq$false-and$hints-notmatch'not observed an external restore destination') "$cli intentional human disarm is not diagnosed as a missing restore destination"}
  $evidence.Add(@{mode=$mode;cli=$cli;reply=$reply})
 }
 Stop-Process -Id $worker.Id -Force -Confirm:$false;$worker=$null
}}
finally{if($worker-and-not$worker.HasExited){Stop-Process -Id $worker.Id -Force -Confirm:$false};@{checks=$checks;evidence=$evidence}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json"}
$failed=@($checks|Where-Object {-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};$global:LASTEXITCODE=0
