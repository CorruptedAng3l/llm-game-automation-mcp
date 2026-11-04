# Scores the states actually read by the target. Poll timestamps describe the
# observer's resolution; they are not weights assigned to the preceding state.
function Measure-ViPwmPolls {
 param([object[]]$Polls,[ValidateRange(0,100)][int]$Duty,[int]$PeriodMs,[int]$ExpectedCycles)
 $points=@($Polls);$gaps=@();$rises=@();$cycles=@();$monotonic=$true
 for($i=0;$i-lt$points.Count;$i++){
  if($i){$gap=[long]$points[$i].at_ms-[long]$points[$i-1].at_ms;if($gap-lt0){$monotonic=$false};if($gap-gt0){$gaps+=$gap}}
  if($points[$i].down-and($i-eq0-or-not$points[$i-1].down)){$rises+=$i}
 }
 $ordered=@($gaps|Sort-Object);$p90=if($ordered.Count){[double]$ordered[[math]::Min($ordered.Count-1,[int][math]::Ceiling($ordered.Count*0.90)-1)]}else{0.0}
 $maxGap=if($ordered.Count){[double]$ordered[-1]}else{0.0}
 $median=if($ordered.Count){[double]$ordered[[int][math]::Floor($ordered.Count/2)]}else{0.0}
 for($i=0;$i-lt$rises.Count-1;$i++){
  $part=@($points[$rises[$i]..($rises[$i+1]-1)]);$on=@($part|Where-Object{$_.down}).Count
  $cycles+=@{index=$i;start_ms=[long]$part[0].at_ms;end_ms=[long]$points[$rises[$i+1]].at_ms;duration_ms=[long]$points[$rises[$i+1]].at_ms-[long]$part[0].at_ms;on_samples=$on;off_samples=$part.Count-$on;fraction=$on/[double]$part.Count;samples=$part}
 }
 $mixed=$Duty-gt0-and$Duty-lt100
 $scored=if($mixed){@($cycles|ForEach-Object{$_.samples})}else{$points}
 $onCount=@($scored|Where-Object{$_.down}).Count;$fraction=if($scored.Count){$onCount/[double]$scored.Count}else{0.0}
 # One target poll of uncertainty at a boundary, plus two endpoint samples.
 # The cap prevents an undersampled observer from accepting a wrong duty.
 $uncertainty=if($scored.Count){$p90/[double]$PeriodMs+2.0/$scored.Count}else{1.0}
 $tolerance=[math]::Max(0.02,[math]::Min(0.08,$uncertainty))
 $enough=$monotonic-and$points.Count-ge64-and$p90-gt0-and$p90-le$PeriodMs*0.08-and$maxGap-le$PeriodMs*0.5
 if($mixed){$enough=$enough-and$cycles.Count-ge($ExpectedCycles-3)-and$cycles.Count-le$ExpectedCycles-and@($cycles|Where-Object{$_.on_samples-eq0-or$_.off_samples-eq0-or$_.duration_ms-lt$PeriodMs*0.7-or$_.duration_ms-gt$PeriodMs*1.3}).Count-eq0}
 return @{pass=[bool]($enough-and[math]::Abs($fraction-$Duty/100.0)-le$tolerance);fraction=$fraction;target_fraction=$Duty/100.0;tolerance=$tolerance;poll_resolution_ms=$p90;median_poll_ms=$median;max_poll_gap_ms=$maxGap;enough_samples=[bool]$enough;on_samples=$onCount;off_samples=$scored.Count-$onCount;complete_cycles=$cycles.Count;expected_cycles=$ExpectedCycles;period_ms=$PeriodMs;cycles=$cycles;polls=$points}
}
