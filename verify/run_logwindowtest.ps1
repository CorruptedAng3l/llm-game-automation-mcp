param([string]$Out="$PSScriptRoot\..\build\test\_out\log_window")
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_logwindow.ps1"
New-Item -ItemType Directory -Force $Out|Out-Null;$path=Join-Path (Resolve-Path $Out) 'owned.log'
$checks=[Collections.Generic.List[object]]::new()
function Check($pass,$name,$evidence){$checks.Add(@{pass=[bool]$pass;name=$name;evidence=$evidence});"$(if($pass){'PASS'}else{'FAIL'}) $name"}
[IO.File]::WriteAllText($path,"READY é`n");$start=[ViLogWindow]::Read($path);[IO.File]::AppendAllText($path,"KEY down`n");$w=[ViLogWindow]::Finish($path,$start)
Check ($w.valid-and$w.text-eq"KEY down`n"-and$w.start_bytes-gt8) 'UTF8 byte boundary preserves actual positive input' $w
$start=[ViLogWindow]::Read($path);[IO.File]::WriteAllText($path,'');$redCaught=$false;try{[IO.File]::ReadAllText($path).Substring([int]$start.offset)|Out-Null}catch [ArgumentOutOfRangeException]{$redCaught=$true}
Check $redCaught 'RED old offset Substring throws on actual mid-window truncate' $null
$w=[ViLogWindow]::Finish($path,$start);Check (-not$w.valid-and$w.reason-eq'log_shrank') 'GREEN actual truncation is unmeasured, never an exception or clean negative' $w
[IO.File]::WriteAllText($path,'native old');$start=[ViLogWindow]::Read($path);[IO.File]::WriteAllText($path,'different and longer replacement');$w=[ViLogWindow]::Finish($path,$start)
Check (-not$w.valid-and$w.reason-eq'log_rewritten') 'same-path replacement cannot establish quiet window' $w
$start=[ViLogWindow]::Read($path);$lock=[IO.File]::Open($path,'Open','ReadWrite','None');try{$w=[ViLogWindow]::Finish($path,$start)}finally{$lock.Dispose()}
Check (-not$w.valid-and$w.reason-eq'end_read_failed') 'locked log bounded read fails inconclusively' $w
$start=[ViLogWindow]::Read($path);$w=[ViLogWindow]::Finish($path,$start);Check ($w.valid-and$w.text-eq'') 'subsequent clean retry can establish empty window' $w
$checks|ConvertTo-Json -Depth 7|Set-Content "$Out\checks.json";$failed=@($checks|Where-Object{-not$_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
