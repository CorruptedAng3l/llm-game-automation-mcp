function Get-ViCaptureTiming([string]$Text,[long]$Token){
 $start=[regex]::Match($Text,"CAPTURE_TIMING_START token=$Token frame=\d+\r?\n")
 if(-not$start.Success){throw 'capture timing START acknowledgement missing'}
 $end=[regex]::Match($Text.Substring($start.Index+$start.Length),"CAPTURE_TIMING_END token=$Token maxms=([\d.]+)")
 if(-not$end.Success){throw 'capture timing END acknowledgement missing'}
 $phase=$Text.Substring($start.Index,$start.Length+$end.Index+$end.Length)
 $values=@([regex]::Matches($phase,'maxms=([\d.]+)')|ForEach-Object{[double]::Parse($_.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)})
 [pscustomobject]@{token=$Token;worst_ms=($values|Measure-Object -Maximum).Maximum;samples=$values.Count;phase=$phase}
}
