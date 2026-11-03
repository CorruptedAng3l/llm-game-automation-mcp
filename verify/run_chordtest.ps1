# Key chords reach a message-loop game with their modifier: TranslateMessage builds WM_CHAR from the thread's REAL key
# state, so a posted Ctrl+A used to arrive as the character 'a'. Background only: hidden fixture, no OS input.
param([string]$Out="$PSScriptRoot\..\build\test\chord")
$ErrorActionPreference='Stop'
. "$PSScriptRoot\_ui_common.ps1"
$script:pass=0;$script:fail=0
function Require($condition,[string]$description){ if($condition){$script:pass++;Write-Output "PASS $description"}else{$script:fail++;Write-Output "FAIL $description"} }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out=(Resolve-Path $Out).Path
$t=$null
try {
  $t=Start-UiTarget $Out 'chord' @('640','360') 'vi_testinput.exe'
  function Chars([string]$keys){
    $before=@(Get-Content -LiteralPath $t.Log | Where-Object { $_ -match 'WM_CHAR' }).Count
    $r=Vi $t.Pid @('key',$keys)
    Start-Sleep -Milliseconds 400
    $all=@(Get-Content -LiteralPath $t.Log | Where-Object { $_ -match 'WM_CHAR (\d+)' })
    $new=@($all | Select-Object -Skip $before | ForEach-Object { if($_ -match 'WM_CHAR (\d+)'){[int]$Matches[1]} })
    return [pscustomobject]@{ok=$r.ok;chars=$new}
  }
  $a=Chars 'a';          Require ($a.ok -and ($a.chars -join ',') -eq '97') "plain a -> WM_CHAR 97 (positive control): $($a.chars -join ',')"
  $s=Chars 'shift+a';    Require ($s.ok -and ($s.chars -join ',') -eq '65') "shift+a -> WM_CHAR 65 (capital A): $($s.chars -join ',')"
  $c=Chars 'ctrl+a';     Require ($c.ok -and ($c.chars -join ',') -eq '1')  "ctrl+a -> WM_CHAR 1 (Ctrl+A control code), not 97: $($c.chars -join ',')"
  $z=Chars 'ctrl+z';     Require ($z.ok -and ($z.chars -join ',') -eq '26') "ctrl+z -> WM_CHAR 26: $($z.chars -join ',')"
  $cs=Chars 'ctrl+shift+a'; Require ($cs.ok -and ($cs.chars -join ',') -eq '1') "ctrl+shift+a -> WM_CHAR 1: $($cs.chars -join ',')"
  $a2=Chars 'a';         Require ($a2.ok -and ($a2.chars -join ',') -eq '97') "plain a after the chords -> 97 again (modifiers released): $($a2.chars -join ',')"
  $log=Get-Content -LiteralPath $t.Log -Raw
  Require ($log -match 'WM_KEYDOWN vk=0x11[\s\S]*?WM_KEYDOWN vk=0x41') 'the Ctrl key-down message precedes the A key-down message'
  $gk=Vi $t.Pid @('status')
  Require (-not ($gk.virtual_down | Where-Object { $_ -in 16,17,18,160,161,162,163 })) 'no virtual modifier is left held'
  Write-Output "RESULT pass=$($script:pass) fail=$($script:fail)"
  if($script:fail){ throw "chord test: $($script:fail) failed" }
  'PASS chords: Ctrl/Shift modifiers reach TranslateMessage-derived WM_CHAR. NOT MEASURED here: Unity/ColossalFramework text fields (needs a live check).'
} finally { Stop-UiTarget $t }
