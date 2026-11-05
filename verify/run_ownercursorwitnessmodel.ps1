param([string]$Out="$PSScriptRoot\session10_vb6\model",[string]$WitnessSource="$PSScriptRoot\vi_owner_cursor_witness.ps1")
$ErrorActionPreference='Stop';. $WitnessSource;[void](New-Item -ItemType Directory -Force $Out)
$checks=[Collections.Generic.List[object]]::new()
function Check([bool]$Good,[string]$Name){$checks.Add(@{pass=$Good;name=$Name});"$(if($Good){'PASS'}else{'FAIL'}) $Name"}
$moving=@{read_ok=$true;forwarded=$true;native_calls=1;before_x=31292;before_y=76;game_x=30397;game_y=-71;after_x=30399;after_y=-271;native_x=30397;native_y=-71}
Check (-not($moving.game_x-eq$moving.before_x-and$moving.game_y-eq$moving.before_y)) 'old delayed physical-snapshot equality is red while the desk moves'
Check (Test-ViOwnerCursorWitness $moving) 'same-call exact native return stays green with changing physical endpoints'
$wrong=$moving.Clone();$wrong.game_x=120;$wrong.game_y=90;Check (-not(Test-ViOwnerCursorWitness $wrong)) 'wrong virtual return cannot pass the physical-reader control'
$missing=$moving.Clone();$missing.forwarded=$false;Check (-not(Test-ViOwnerCursorWitness $missing)) 'unreached lower reader cannot claim physical pass-through'
$missing=$moving.Clone();$missing.native_calls=0;Check (-not(Test-ViOwnerCursorWitness $missing)) 'zero native invocations cannot claim physical pass-through'
$missing=$moving.Clone();$missing.read_ok=$false;Check (-not(Test-ViOwnerCursorWitness $missing)) 'failed physical read remains a failure'
$missing=$moving.Clone();$missing.game_x='30397';Check (-not(Test-ViOwnerCursorWitness $missing)) 'untyped receipt cannot pass numeric equality by coercion'
Check (-not(Test-ViOwnerCursorWitness $null)) 'missing receipt is not success'
$failed=@($checks|Where-Object {-not$_.pass}).Count;@{completed=$true;passed=$checks.Count-$failed;failed=$failed;checks=$checks;scope='deterministic cursor witness assertion controls; no OS/UI/input calls'}|ConvertTo-Json -Depth 8|Set-Content "$Out\result.json"
"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1};exit 0
