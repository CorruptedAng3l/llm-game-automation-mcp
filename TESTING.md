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

PowerShell 7 (`pwsh`) is required. `run_selftest.ps1` runs the **background** suite plan (`verify\vi_suiteplan.ps1`):
none of its cases touches the real foreground, keyboard or mouse, so it can run while the machine is in use. It stops at
the first failure, stops only the processes it started (by PID), and writes `suite_result.json` plus one folder per
case under `build\test\selftest\` (git-ignored). `-PlanOnly` prints the plan.

The **foreground** cases (`run_foreground_suite.ps1`: `run_fgguardtest.ps1`, `run_foregroundtest.ps1`,
`run_windowfocustest.ps1`, `run_userintenttest.ps1` and similar) take the real foreground or simulate a user's
Alt+Tab. Run them only on an idle machine. They hold a lock file meanwhile: set `VINPUT_LOCK` to the lock file that
other foreground-taking tools on your machine use, or they use `build\foreground.lock`.

## What is covered

| script | covers |
|---|---|
| `run_selftest.ps1` | every input channel on x64 / x86, injected DLL and compiled-in mode, D3D11 capture, background isolation; then every background case of `vi_suiteplan.ps1` (the scripts below and the ones in the next rows) |
| `run_inputjobstest.ps1`, `run_steeringsemanticstest.ps1` | owned input jobs (async key / hold / pad, job status / cancel / replace, deadlines), key PWM duty, XInput ramps and timelines with frame pacing |
| `run_dipollclicktest.ps1`, `run_direleasepolltest.ps1`, `run_placementtest.ps1`, `run_edgeguardtest.ps1` | polled click press / release evidence through DirectInput, owned placement drag, edge guard paths |
| `run_captureoutputtest.ps1`, `run_imageencodetest.ps1`, `run_outputreusecontracttest.ps1`, `run_capturetimingtest.ps1` | in-game scaling / JPEG output and its capture-space map, capture bursts, encoder, capture timing |
| `run_fullscreentest.ps1`, `run_ddrawwindowedtest.ps1`, `run_dinput1test.ps1` | DXGI virtual fullscreen and refusal, DirectDraw exclusive mode turned windowed (pixels, flips, no display-mode change), legacy DirectInput 1-7 |
| `run_doctortest.ps1`, `run_pipesecuritytest.ps1`, `run_hwndownershiptest.ps1`, `run_hostselecttest.ps1`, `run_sentinel*test.ps1`, `run_profiletest.ps1` and the other `vi_suiteplan.ps1` cases | read-only diagnostics, pipe ACLs, window ownership and host selection, focus sentinels, per-game profiles |
| `run_clicktest.ps1`, `run_activatetest.ps1`, `run_keytest.ps1`, `run_tracetest.ps1`, `run_movetest.ps1` | hover-gated clicks, focus shims, key names / chords / mouse buttons, per-API key tracing, relative and raw mouse motion |
| `run_ownerinputtest.ps1`, `run_chordtest.ps1` | the user's real mouse / keyboard in a game vinput drives: pass-through while the game is the real foreground (the foreground predicate is faked with `VINPUT_TEST_HOOKS=1` + `config test_realfg`; real input is a window message posted from outside vinput), the virtual cursor standing down until the next agent command, the foreground guard not handing focus away, background isolation unchanged; Ctrl / Shift chords arriving as `WM_CHAR` control / capital characters. **The true OS foreground check is not covered here** (needs a live check with a real click) |
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
| `verify\run_selftest.ps1` (background plan, run twice) | both runs: 66 suites passed, 0 failed, 9 foreground suites skipped; 3168 PASS lines, 0 FAIL; about 22 min each (includes `run_actest.ps1`, `run_launchtest.ps1`, `run_dinput1test.ps1`, `run_overlaycapturetest.ps1`, `run_autoactivatetest.ps1`) |
| `verify\run_actest.ps1` | 26 PASS, 0 FAIL |
| `mcp`: `npm run build && npm test` | build OK; 18 test files, 403 PASS, 0 FAIL (handshake 112 PASS, 38 tools) |
| `mcp`: `npm run smoke` | smoke 30 PASS + glue 12 PASS, 0 FAIL |

Fix pass after the Cities: Skylines field test (owner input, chords, placement fallback, launch size warning, capture options):

| check | result |
|---|---|
| `build.bat` x64 + x86 | BUILD OK, 0 warnings |
| `run_ownerinputtest.ps1` (x64 + x86) | 22 PASS, 0 FAIL each; against the previous behaviour 17 PASS, 5 FAIL (x64) |
| `run_chordtest.ps1` (x64 + x86) | 8 PASS, 0 FAIL each; against the previous behaviour 4 PASS, 4 FAIL (x64): `ctrl+a` arrived as `a`, `shift+a` as lowercase `a` |
| `verifyun_selftest.ps1` (background plan, 68 suites) | run 1: 68 passed, 0 failed; runs 2 and 3: 67 passed, 0 failed, 1 NOT MEASURED (`legacy_focus`: its quiet-machine control saw physical input; the case passes alone, 272 PASS / 0 FAIL, and on the previous build); run 4: 68 passed, 0 failed |
| `mcp`: `npm run build && npm test && npm run smoke` | 19 test files PASS; smoke PASS, glue 12 PASS |
| Cities: Skylines, live (windowed 1600x900, background input) | `key ctrl+a` selects all text in a Paradox login field; `drag placement:true` falls back with `placement_fallback`; dropdown list items take clicks in windowed mode |
| NOT MEASURED | the true OS foreground check for owner input (the test fakes the predicate; needs a real click by the user); Unity text-field chords beyond the one field above |

The foreground suites (`run_foreground_suite.ps1`) were not run for this snapshot.

## Real games

Automated tests cannot include commercial games. During development vinput was also exercised by hand on installed
single-player games (background menus, loading saves, walking, map toggles, two games at once while another window
held the focus): Direct3D 11 games on Unity and Unreal Engine 4, a Direct3D 12 UWP game (capture and XInput pad), and a
Unity game in OpenGL mode (capture). **Vulkan capture has been verified only on the synthetic target, not in a real
game.** OCR on stylised game fonts is read inconsistently from run to run; use regions, `--scale 2` or image matching.

Not verified anywhere: `GetRawInputBuffer` injection (no target uses it), 32-bit real games, D3D9 / D3D10 games.
