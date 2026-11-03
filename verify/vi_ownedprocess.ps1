# Only pass Process objects returned by this fixture's own Start-Process calls.
function Stop-ViOwnedProcess([Diagnostics.Process]$Process){
 if(-not$Process-or$Process.HasExited){return}
 $fresh=Get-Process -Id $Process.Id -ErrorAction SilentlyContinue
 if($fresh-and$fresh.Path-eq$Process.Path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$Process.StartTime.ToUniversalTime().Ticks){
  try{$Process.Kill();[void]$Process.WaitForExit(2000)}catch{if(-not$Process.HasExited){throw}}
 }
}
