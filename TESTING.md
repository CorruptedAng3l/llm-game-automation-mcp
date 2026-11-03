# Testing

All automated tests run against **synthetic targets** built from `test/` - small programs that log every input they
receive and draw known pixel patterns - so a test checks what the *target* saw, not only what vinput replied. No real
game is needed, and no test drives the real keyboard or mouse.

## Run

```bat
build.bat
pwsh -File verify\run_selftest.ps1
cd mcp && npm ci && npm run build && npm test && npm run smoke
```

PowerShell 7 (`pwsh`) is required. `run_selftest.ps1` stops at the first failure and stops only the processes it
started, by PID. Test output goes to `build\test\` (git-ignored).

Only `run_fgguardtest.ps1` touches the real foreground (for a few milliseconds, to simulate a game stealing it). It holds
a lock file meanwhile: set `VINPUT_LOCK` to the lock file that other foreground-taking tools on your machine use, or it
uses `build\foreground.lock`. Close fullscreen games before running the suite.

## What is covered

| script | covers |
|---|---|
| `run_selftest.ps1` | every input channel on x64 / x86, injected DLL and compiled-in mode, D3D11 capture, background isolation; then runs the scripts below (except the three standalone ones) |
| `run_clicktest.ps1`, `run_activatetest.ps1`, `run_keytest.ps1`, `run_tracetest.ps1`, `run_movetest.ps1` | hover-gated clicks, focus shims, key names / chords / mouse buttons, per-API key tracing, relative and raw mouse motion |
| `run_overlaycapturetest.ps1` | capture of a DDraw-wrapper style game (game window black or stale, picture in a separate `...PresentationWindow`), x64 and x86 |
| `run_capturetest.ps1`, `run_nativecapture.ps1` | capture regions and frame waits; native D3D12 / OpenGL / Vulkan capture (BGRA, RGBA, R10G10B10A2, RGBA16F; x64 / x86; AppContainer), pixel-exact pattern decode, 50 resizes per API, late-injection refusal for Vulkan |
| `run_xinputtest.ps1` | XInput pad 0: buttons, sticks, triggers, durations, hold, off, Guide via `XInputGetStateEx` |
| `run_actest.ps1` | anti-cheat refusal (below) |
| `run_unittests.ps1`, `run_ocrtest.ps1`, `run_texttest.ps1`, `run_imagetest.ps1`, `run_smalltest.ps1`, `run_scripttest.ps1` | text matching and NCC unit tests (x64 / x86), OCR, click / wait / assert text, image matching, pixel / scroll / drag / unicode type, macro scripts |
| `run_jsontest.ps1`, `run_foregroundtest.ps1`, `run_earlytest.ps1`, `run_multitest.ps1`, `run_fgguardtest.ps1` | JSON output, non-activating launch and foreground hand-back, early (suspended) injection, several clients driving several targets with ownership claims, delayed foreground re-grab |
| standalone: `run_launchtest.ps1`, `run_locktest.ps1`, `run_parsertests.ps1` | launch JSON / lock regressions, JSON lock leases (x64 / x86, `/W4 /WX`), key-name parser |
| `mcp/test/handshake.mjs` (`npm test`) | the built MCP server answers `initialize` and `tools/list`; every tool has a description and an object schema; argument errors come back as `isError` |
| `mcp/test/smoke.mjs` (`npm run smoke`) | end to end through MCP on `vi_uitarget`: launch, capture (image content), wait_text, ocr, find_text, key, click_text, type_text, region capture, status, explicit errors, release, detach, anti-cheat refusal via `attach` - each action checked in the target's own log |

## Anti-cheat refusal test (`verify/run_actest.ps1`)

The "anti-cheat" is `test/vi_acdummy.cpp`: an **empty** DLL that only carries an anti-cheat-like file name
(`EasyAntiCheat_x64.dll`, `BEClient.dll`), plus a copy of the plain target renamed `BEService.exe`. Every refusal is
paired with a positive control showing the same target **is** injectable without the trigger, so a pass cannot come
from a broken injector:

- x64 target with the EasyAntiCheat-named module, x86 target with the BattlEye-named module: `inject` refused (exit 4),
  no vinput module, no control pipe;
- a game folder that ships an `EasyAntiCheat` folder: `launch` and `launch --early` refused before anything starts;
  `inject` into the running copy refused; injectable once the folder is gone;
- a `BEService.exe` process running: `inject` and `launch` refused; injectable once it has exited;
- an anti-cheat module loaded **after** injection: input and capture commands refused from then on, `status` reports
  `anticheat_disabled`, and the target never receives the refused key;
- the DLL loaded directly next to an anti-cheat module: no hooks, no pipe, `REFUSED` in its log.

Run it against an older build with `-CliDir <folder with that vinput.exe>` to see it red.

## Results for this snapshot

Windows 10 22H2 (19045) x64, MSVC 19.51 (Visual Studio 18), PowerShell 7.6, Node 26:

| check | result |
|---|---|
| `build.bat` x64 + x86 | BUILD OK, 0 warnings, 0 errors |
| `verify\run_selftest.ps1` | 441 PASS, 0 FAIL in 4.7 min (includes `run_actest.ps1`) |
| `verify\run_actest.ps1` | 26 PASS, 0 FAIL; against the previous build (no refusal): 10 PASS (fixtures and positive controls), 16 FAIL (red) |
| `verify\run_locktest.ps1` | 48 checks x64 + 48 x86, 0 failures |
| `verify\run_parsertests.ps1` | 2497 checks, 0 failures (x64 and x86, `/W4 /WX`) |
| `verify\run_launchtest.ps1` | 58 checks, 0 failures |
| `mcp`: `npm ci && npm run build && npm test` | build OK; handshake 89 PASS, 0 FAIL (28 tools) |
| `mcp`: `npm run smoke` | 19 PASS, 0 FAIL |

## Real games

Automated tests cannot include commercial games. During development vinput was also exercised by hand on installed
single-player games (background menus, loading saves, walking, map toggles, two games at once while another window
held the focus): Direct3D 11 games on Unity and Unreal Engine 4, a Direct3D 12 UWP game (capture and XInput pad), and a
Unity game in OpenGL mode (capture). **Vulkan capture has been verified only on the synthetic target, not in a real
game.** OCR on stylised game fonts is read inconsistently from run to run; use regions, `--scale 2` or image matching.

Not verified anywhere: `GetRawInputBuffer` injection (no target uses it), 32-bit real games, D3D9 / D3D10 games.
