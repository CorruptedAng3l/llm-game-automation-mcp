param([string]$Out="$PSScriptRoot\..\build\test\steering_semantics\poll_scoring",[switch]$LegacyScorer)
$ErrorActionPreference='Stop';. "$PSScriptRoot\vi_pwmmeasure.ps1"
New-Item -ItemType Directory -Force $Out|Out-Null
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new()
function Check($Ok,$Name){$checks.Add(@{pass=[bool]$Ok;name=$Name});"$(if($Ok){'PASS'}else{'FAIL'}) $Name"}
function Polls([int]$On,[int]$Count=13,[int]$Step=16){$p=@();for($cycle=0;$cycle-lt12;$cycle++){for($i=0;$i-lt$Count;$i++){$p+=@{at_ms=[long](($cycle*$Count+$i)*$Step);down=$i-lt$On}}};$p+=@{at_ms=[long](12*$Count*$Step);down=$true};return $p}
# 15-16ms Windows polls quantize a requested 50ms ON interval to four reads.
# The old wall-duration rule rejects 64/200=.320. Target-poll scoring measures
# 4/13=.308 and explicitly reports its one-poll resolution (16/200=.080).
$quantized=@(Polls 4);$score=Measure-ViPwmPolls $quantized 25 200 12;$legacyArea=0L
for($i=0;$i-lt$quantized.Count-1;$i++){if($quantized[$i].down){$legacyArea+=$quantized[$i+1].at_ms-$quantized[$i].at_ms}}
$legacyFraction=$legacyArea/(12.0*200);$evidence.Add(@{case='quantized';score=$score;legacy_fraction=$legacyFraction})
$accepted=if($LegacyScorer){[math]::Abs($legacyFraction-0.25)-le0.06}else{$score.pass}
Check $accepted 'quantized target polls at requested25% are accepted with measured resolution'
Check ($score.complete_cycles-eq12-and$score.cycles[0].samples.Count-eq13-and$score.polls.Count-eq157) 'all target polls and complete cycle samples retained'
foreach($duty in @(25,50,75)){$on=$duty/5;$p=@(Polls $on 20);$s=Measure-ViPwmPolls $p $duty 320 12;$evidence.Add(@{case="correct$duty";score=$s});Check $s.pass "correct$duty passes over12 observed cycles"}
$wrong=Measure-ViPwmPolls @(Polls 10 20) 25 320 12;$evidence.Add(@{case='wrong50_as25';score=$wrong});Check (-not$wrong.pass) '50% output rejected for25% request'
$stuck=@(Polls 20 20);$s=Measure-ViPwmPolls $stuck 25 320 12;Check (-not$s.pass) 'stuck key rejected for25% request'
$sparse=@(Polls 1 4 80);$s=Measure-ViPwmPolls $sparse 25 320 12;Check (-not$s.pass) 'undersampled target rejected instead of widening tolerance'
$truncated=@($quantized|Select-Object -First 40);$s=Measure-ViPwmPolls $truncated 25 200 12;Check (-not$s.pass) 'too few complete cycles rejected'
$outOfOrder=@($quantized);$outOfOrder[20]=@{at_ms=1L;down=$false};$s=Measure-ViPwmPolls $outOfOrder 25 200 12;Check (-not$s.pass) 'nonmonotonic target timestamps rejected'
foreach($duty in @(0,100)){$p=@(Polls $(if($duty){20}else{0}) 20);if(-not$duty){$p[-1].down=$false};$s=Measure-ViPwmPolls $p $duty 320 12;Check $s.pass "constant$duty positive control"}
$failed=@($checks|Where-Object{-not$_.pass}).Count
@{legacy_scorer=[bool]$LegacyScorer;checks=$checks;evidence=$evidence}|ConvertTo-Json -Depth 12|Set-Content "$Out\result.json" -Encoding utf8NoBOM
"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
