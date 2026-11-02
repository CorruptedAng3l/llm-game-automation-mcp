param([string]$Out="$PSScriptRoot\..\build\test\keytest")
$ErrorActionPreference='Stop'
$D=(Resolve-Path "$PSScriptRoot\..\dist").Path; $V="$D\vinput.exe"
New-Item -ItemType Directory -Force $Out | Out-Null
$Out=(Resolve-Path $Out).Path
$p=$null; $results=@()
try {
  $p=Start-Process -FilePath "$D\vi_testinput.exe" -ArgumentList "`"$Out\target.log`" -28000 600 640 360" -WindowStyle Hidden -PassThru
  $i=& $V inject --pid $p.Id | ConvertFrom-Json; if(-not $i.ok){throw 'Injection failed'}
  Start-Sleep -Milliseconds 300
  foreach($c in @(@('numpad2',98),@('insert',45),@('capslock',20),@('lwin',91),@('num+',107),@('oem102',226),@('VK_BROWSER_BACK',166),@('rbutton',2),@('mbutton',4),@('xbutton2',6))) {
    $down=& $V --pid $p.Id down $c[0] | ConvertFrom-Json
    $state=& $V --pid $p.Id status | ConvertFrom-Json
    $up=& $V --pid $p.Id up $c[0] | ConvertFrom-Json
    $ok=$down.ok -and $up.ok -and (@($state.virtual_down | ForEach-Object { [long]$_ }) -contains [long]$c[1])
    $results += [pscustomobject]@{key=$c[0];vk=$c[1];ok=$ok}
    if(-not $ok){throw "Named VK failed: $($c[0])"}
  }
  $down=& $V --pid $p.Id down 'w+space+num+' | ConvertFrom-Json
  $s=& $V --pid $p.Id status | ConvertFrom-Json
  if(-not $down.ok -or 87 -notin $s.virtual_down -or 32 -notin $s.virtual_down -or 107 -notin $s.virtual_down){throw 'Chord failed'}
  & $V --pid $p.Id up 'w+space+num+' | Out-Null
  foreach($bad in @('vk256','vk12junk','0x100','0x2Djunk','f25','w+','w++space')) {
    $badResult=& $V --pid $p.Id down $bad | ConvertFrom-Json
    if($badResult.ok){throw "Invalid VK accepted: $bad"}
  }
  $listing=& $V keys | ConvertFrom-Json
  if(-not $listing.ok -or $listing.keys.Count -lt 255){throw 'Key listing incomplete'}
  $results | ConvertTo-Json | Set-Content -LiteralPath "$Out\result.json"
  "PASS named VKs, held mouse buttons, simultaneous chord, invalid keys, $($listing.keys.Count) aliases"
} finally { if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -Confirm:$false} }
