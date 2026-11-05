# Static assertion-preservation check; no native program is executed.
from pathlib import Path
import hashlib
root=Path(__file__).resolve().parents[1]
s=(root/'verify/run_launchtest.ps1').read_text(encoding='utf-8')
block=s[s.index('  # A global flag before the command'):s.index('  $failed = @($checks')].replace('\r\n','\n')
assert hashlib.sha256(block.encode()).hexdigest()=='b264087eb71058ff4dbf952e6d20a6040c330d9045f780b85dc7631813468ddc', 'original prefix/suffix/noinject/injected/validation/missing-DLL/foreign-lock assertions changed'
f=(root/'verify/run_foregroundtest.ps1').read_text(encoding='utf-8')
for expected in ["if(-not $reply.ok -or $reply.foreground_restored -ne $true -or (Test-Path -LiteralPath $lock)){throw 'Launch lacks verified foreground cleanup/owned-lock release'}", "if($bad.ok -or (Test-Path -LiteralPath $lock)){throw 'Launch failure did not release its own lock'}"]: assert expected in f, 'original foreground/lock condition changed'
for name in ['run_launchtest.ps1','run_foregroundtest.ps1','vi_legacy_launch_guard.ps1']:
 text=(root/'verify'/name).read_text(encoding='utf-8')
 assert 'Stop-Process' not in text and 'ReadToEndAsync' not in text, 'bare PID cleanup or unbounded stream reader returned'
assert '-BuildOnly' not in block, 'runtime cases silently became compile-only'
print('LEGACY_LAUNCH_PRESERVATION 3 PASS / 0 FAIL')
