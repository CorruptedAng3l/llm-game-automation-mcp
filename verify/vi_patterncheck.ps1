# Decodes a capture of the synthetic capture targets (test\vi_pattern.h) back into the frame counter and checks
# the 8 colour bars pixel-exactly. Dot-source it:  . .\vi_patterncheck.ps1 ; $r = Test-VinputPattern a.png
#   returns @{ ok; frame; bars_ok; bits_ok; w; h; detail }
Add-Type -AssemblyName System.Drawing
function Test-VinputPattern([string]$Path, [int]$Tolerance = 2) {
  $bmp = [System.Drawing.Bitmap]::FromFile($Path)
  try {
    $w = $bmp.Width; $h = $bmp.Height
    $res = @{ ok = $false; frame = -1; bars_ok = $false; bits_ok = $false; w = $w; h = $h; detail = '' }
    if ($w -lt 512 -or $h -lt 40) { $res.detail = "too small ${w}x${h}"; return $res }
    $frame = [uint32]0; $bitsOk = $true
    for ($i = 0; $i -lt 32; $i++) {
      $c = $bmp.GetPixel($i * 16 + 8, 8)
      $v = ([int]$c.R + [int]$c.G + [int]$c.B) / 3
      if ($v -ge 255 - $Tolerance -and [Math]::Abs($c.R - $c.G) -le $Tolerance) { $frame = $frame -bor ([uint32]1 -shl (31 - $i)) }
      elseif ($v -gt $Tolerance) { $bitsOk = $false; $res.detail += "bit$i=($($c.R),$($c.G),$($c.B)) " }
    }
    $bars = @(@(255,255,255),@(255,255,0),@(0,255,255),@(0,255,0),@(255,0,255),@(255,0,0),@(0,0,255),@(0,0,0))
    $barsOk = $true
    for ($b = 0; $b -lt 8; $b++) {
      $x = [int](($b + 0.5) * $w / 8); $y = [int](16 + ($h - 16) / 2)
      $c = $bmp.GetPixel($x, $y); $e = $bars[$b]
      if ([Math]::Abs($c.R - $e[0]) -gt $Tolerance -or [Math]::Abs($c.G - $e[1]) -gt $Tolerance -or [Math]::Abs($c.B - $e[2]) -gt $Tolerance) {
        $barsOk = $false; $res.detail += "bar$b=($($c.R),$($c.G),$($c.B)) want ($($e -join ',')) "
      }
    }
    $res.frame = [long]$frame; $res.bits_ok = $bitsOk; $res.bars_ok = $barsOk; $res.ok = $bitsOk -and $barsOk
    return $res
  } finally { $bmp.Dispose() }
}
