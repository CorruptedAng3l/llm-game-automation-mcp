# Exact lower-reader return from the same hooked call, never a delayed desk snapshot.
function Test-ViOwnerCursorWitness($Witness) {
 if($null-eq$Witness-or$Witness.read_ok-ne$true-or$Witness.forwarded-ne$true-or$Witness.native_calls-lt1){return $false}
 foreach($name in @('game_x','game_y','native_x','native_y')){
  if($Witness.$name-isnot[long]-and$Witness.$name-isnot[int]){return $false}
 }
 return $Witness.game_x-eq$Witness.native_x-and$Witness.game_y-eq$Witness.native_y
}
function Read-ViOwnerCursorWitness($Target,[IntPtr]$Window) {
 [uint32]$owner=0;[void][ViOwnerNative]::GetWindowThreadProcessId($Window,[ref]$owner)
 if($Target.Proc.HasExited-or$owner-ne$Target.Pid){throw 'owned cursor witness target changed'}
 if(-not(Get-Variable -Name viOwnerWitnessToken -Scope Script -ErrorAction SilentlyContinue)){$script:viOwnerWitnessToken=0}
 $script:viOwnerWitnessToken++;$token=$script:viOwnerWitnessToken
 if(-not[ViOwnerNative]::PostMessageW($Window,0x80B6,[IntPtr]$token,[IntPtr]::Zero)){throw 'own cursor witness request failed'}
 $deadline=[Environment]::TickCount64+2000
 do {
  $line=@(Get-Content -LiteralPath $Target.Log|Where-Object {$_-like "OWNER_CURSOR_WITNESS token=$token *"})|Select-Object -Last 1
  if($line){
   $pattern='^OWNER_CURSOR_WITNESS token=(\d+) read_ok=([01]) forwarded=([01]) before=(-?\d+),(-?\d+) game=(-?\d+),(-?\d+) after=(-?\d+),(-?\d+) native=(-?\d+),(-?\d+) native_calls=(\d+) qpc_begin=(\d+) qpc_end=(\d+)$'
   $m=[regex]::Match($line,$pattern);if(-not$m.Success){throw 'cursor witness receipt malformed'}
   return [pscustomobject]@{token=[long]$m.Groups[1].Value;read_ok=$m.Groups[2].Value-eq'1';forwarded=$m.Groups[3].Value-eq'1';before_x=[long]$m.Groups[4].Value;before_y=[long]$m.Groups[5].Value;game_x=[long]$m.Groups[6].Value;game_y=[long]$m.Groups[7].Value;after_x=[long]$m.Groups[8].Value;after_y=[long]$m.Groups[9].Value;native_x=[long]$m.Groups[10].Value;native_y=[long]$m.Groups[11].Value;native_calls=[int]$m.Groups[12].Value;qpc_begin=$m.Groups[13].Value;qpc_end=$m.Groups[14].Value;raw=$line}
  }
  if($Target.Proc.HasExited){throw 'owned cursor witness target exited'};Start-Sleep -Milliseconds 5
 } while([Environment]::TickCount64-lt$deadline)
 throw 'owned cursor witness reply unavailable; rebuild fixture with witness support'
}
