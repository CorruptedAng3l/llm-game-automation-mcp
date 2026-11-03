# Owner input in a game vinput is driving. BACKGROUND test: the fixture window is hidden and never foreground.
# "Real user input" is a window message posted from outside vinput (it is not in vinput's posted-message ring, so the
# subclass sees it exactly like a physical click). "The game is the real foreground" is faked through the test hook
# (VINPUT_TEST_HOOKS=1 + config test_realfg); nothing here grabs the foreground or sends OS input.
param([string]$Out="$PSScriptRoot\..\build\test\ownerinput")
$ErrorActionPreference='Stop'
. "$PSScriptRoot\_ui_common.ps1"
# Soft checks so a red run reports every assertion, not only the first failure.
$script:pass=0;$script:fail=0
function Require($condition,[string]$description){ if($condition){$script:pass++;Write-Output "PASS $description"}else{$script:fail++;Write-Output "FAIL $description"} }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out=(Resolve-Path $Out).Path
if (-not ('ViOwnerNative' -as [type])) {
  Add-Type @'
using System; using System.Runtime.InteropServices;
public static class ViOwnerNative {
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
}
'@
}
function MakeL([int]$x,[int]$y){ [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }
function Real([IntPtr]$h,[uint32]$m,[int]$w,[int]$x,[int]$y){ [void][ViOwnerNative]::PostMessageW($h,$m,[IntPtr]$w,(MakeL $x $y)) }
function LastCursor($t){
  $rows=@(Get-Content -LiteralPath $t.Log | Where-Object { $_ -match ' CURSOR (-?\d+) (-?\d+)$' })
  if(-not $rows){ return $null }
  $null=$rows[-1] -match ' CURSOR (-?\d+) (-?\d+)$'; return "$($Matches[1]),$($Matches[2])"
}
$env:VINPUT_TEST_HOOKS='1'
$t=$null
try {
  $t=Start-UiTarget $Out 'owner' @('640','360') 'vi_testinput.exe'
  $pid_=$t.Pid
  $st=Vi $pid_ @('status')
  $h=[IntPtr][Convert]::ToInt64($st.hwnd,16)
  Require ($st.PSObject.Properties.Name -contains 'owner_active') 'status reports owner_active'
  Require ($st.owner_active -eq $false) 'owner_active is false before any real input'

  # --- background control: real mouse is isolated, agent works
  $c=Vi $pid_ @('click','left','50','40')
  Require $c.ok 'agent click works in the background'
  Start-Sleep -Milliseconds 250
  $s0=Vi $pid_ @('status')
  Require ($s0.cursor_effective -eq 'virtual') 'background: effective cursor is virtual'
  Real $h 0x0201 1 300 200; Real $h 0x0202 0 300 200; Start-Sleep -Milliseconds 250
  $s1=Vi $pid_ @('status')
  Require ((Get-LogText $t) -notmatch 'WM_LBUTTONDOWN 300 200') 'background: a real click is NOT delivered (isolation unchanged)'
  Require ($s1.counters.wm_mouse_dropped -gt $s0.counters.wm_mouse_dropped) 'background: wm_mouse_dropped counts the real click'
  Require ($s1.owner_active -eq $false -and $s1.cursor_effective -eq 'virtual') 'background: real input does not make the owner active'
  Require ($s1.PSObject.Properties.Name -contains 'owner_input_count' -and $s1.owner_input_count -eq 0) 'background: dropped input is not counted as owner input'

  # --- the game is now (faked) the real foreground and the owner clicks
  $cfg=Vi $pid_ @('config','test_realfg=1')
  Require $cfg.ok 'test hook accepted (VINPUT_TEST_HOOKS=1)'
  Real $h 0x0200 0 300 200; Real $h 0x0201 1 300 200; Real $h 0x0202 0 300 200
  Start-Sleep -Milliseconds 300
  $s2=Vi $pid_ @('status')
  $log=Get-LogText $t
  Require ($log -match 'WM_LBUTTONDOWN 300 200') 'foreground: the owner click is delivered to the game'
  Require ($s2.counters.wm_mouse_dropped -eq $s1.counters.wm_mouse_dropped) 'foreground: nothing dropped'
  Require ($s2.owner_active -eq $true) 'foreground: owner_active is true after real input'
  Require ($s2.cursor_effective -eq 'real' -and $s2.effective_cursor -eq 'real') 'foreground: the virtual cursor stands down (cursor_effective real)'
  $pt=New-Object ViOwnerNative+POINT; [void][ViOwnerNative]::GetCursorPos([ref]$pt); [void][ViOwnerNative]::ScreenToClient($h,[ref]$pt)
  Start-Sleep -Milliseconds 200
  $seen=LastCursor $t
  Require ($seen -eq "$($pt.X),$($pt.Y)") "foreground: the game's GetCursorPos reads the real cursor ($seen)"
  # guard: no hand-back from the human once deliberate input arrived
  $g1=(Vi $pid_ @('status')).foreground_guard
  Start-Sleep -Milliseconds 600
  $g2=(Vi $pid_ @('status')).foreground_guard
  Require ($g2.restored_count -eq $g1.restored_count) "foreground: the guard does not hand focus away from the owner ($($g1.restored_count) -> $($g2.restored_count))"

  # --- the agent's next command takes the cursor back
  $m=Vi $pid_ @('mouse','120','90')
  Require $m.ok 'agent move works while the owner was active'
  Start-Sleep -Milliseconds 250
  $s3=Vi $pid_ @('status')
  Require ($s3.owner_active -eq $false -and $s3.cursor_effective -eq 'virtual') 'agent command: owner_active false, cursor virtual again'
  Require ((LastCursor $t) -eq '120,90') 'agent command: the game reads the virtual cursor again (120,90)'
  # the persisted cursor=1 must not defeat the owner next time (the original bug)
  Real $h 0x0200 0 310 210; Start-Sleep -Milliseconds 250
  $s4=Vi $pid_ @('status')
  Require ($s4.owner_active -eq $true -and $s4.cursor_effective -eq 'real') 'second owner input: real cursor again although cursor mode stayed virtual'
  # real keyboard in the foreground counts as owner input
  $before=$s4.owner_input_count
  [void][ViOwnerNative]::PostMessageW($h,0x0100,[IntPtr]0x51,[IntPtr]0x00100001); Start-Sleep -Milliseconds 200
  $s5=Vi $pid_ @('status')
  Require ($s5.owner_input_count -gt $before) 'real key in the foreground is owner input'

  # --- back to background: isolation again
  $cfg=Vi $pid_ @('config','test_realfg=0'); Start-Sleep -Milliseconds 100
  $m=Vi $pid_ @('mouse','60','60'); Start-Sleep -Milliseconds 200
  Real $h 0x0201 1 333 222; Start-Sleep -Milliseconds 250
  $s6=Vi $pid_ @('status')
  Require ((Get-LogText $t) -notmatch 'WM_LBUTTONDOWN 333 222') 'back in the background: real click dropped again'
  Require ($s6.owner_active -eq $false -and $s6.cursor_effective -eq 'virtual') 'back in the background: virtual cursor, owner inactive'
  Write-Output "RESULT pass=$($script:pass) fail=$($script:fail)"; if($script:fail){ throw "owner input test: $($script:fail) failed" }
  "PASS owner input: pass-through in the real foreground, virtual cursor stands down until the next agent command, guard does not hand back, background isolation unchanged. NOT MEASURED: the true OS foreground check (fake predicate)."
} finally {
  Remove-Item Env:VINPUT_TEST_HOOKS -ErrorAction SilentlyContinue
  Stop-UiTarget $t
}
