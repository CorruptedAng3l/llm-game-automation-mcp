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
| `run_cursorguardtest.ps1`, `run_physicalreaderstest.ps1`, `run_chartranslatetest.ps1`, `run_ditypelifetimetest.ps1` | cursor guard (every hooked cursor API against lower spies that absorb the write; old-product red controls), physical keyboard / mouse / raw / DirectInput reader masking and the `isolate=false --for` deadline, legacy character translation from the posted-key snapshot, DirectInput device-type lifetime. `run_cursorguardtest.ps1` can end `not_measured` (physical activity contaminated its native control), which the suite plan reports as such |
| `run_capturescopetest.ps1`, `run_capturescopecompattest.ps1`, `run_capturediagnosticssurrogatetest.ps1` | screenshot-coordinate binding to the adopted window lifetime and client geometry, the refusal of an older DLL, and the `capture_diagnostics` fields against known-source surrogate fixtures |
| `run_wndprocretargettest.ps1`, `run_wndprocawtest.ps1`, `run_retargetquiescencetest.ps1`, `run_dialogtokentest.ps1` | per-window WndProc binding across retargets, A/W window-procedure forwarding, refusal of retargets while owned input has not drained, dialog / window-class tokens |
| `run_selfrawtest.ps1`, `run_hostselfrawtest.ps1` | raw messages an in-process payload posts to its own window (private envelope, backpressure, expiry) and a host-side consumer. They show what the synthetic consumers received, not that a real game accepts such packets |
| `run_padphasetest.ps1`, `run_padtriggerstest.ps1`, `run_keysequencetest.ps1`, `run_readinesstest.ps1` | pad-timeline phases and `--stagger-ms`, `lt` / `rt` aliases, async key sequences (`seq`), the `readiness` handshake and `inject --hooks-only` |
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
| `verify\run_selftest.ps1` (background plan, 68 suites) | run 1: 68 passed, 0 failed; runs 2 and 3: 67 passed, 0 failed, 1 NOT MEASURED (`legacy_focus`: its quiet-machine control saw physical input; the case passes alone, 272 PASS / 0 FAIL, and on the previous build); run 4: 68 passed, 0 failed |
| `mcp`: `npm run build && npm test && npm run smoke` | 19 test files PASS; smoke PASS, glue 12 PASS |
| Cities: Skylines, live (windowed 1600x900, background input) | `key ctrl+a` selects all text in a Paradox login field; `drag placement:true` falls back with `placement_fallback`; dropdown list items take clicks in windowed mode |
| NOT MEASURED | the true OS foreground check for owner input (the test fakes the predicate; needs a real click by the user); Unity text-field chords beyond the one field above |

The foreground suites (`run_foreground_suite.ps1`) were not run for this snapshot.

## Status of the sync with the reviewed core (not a result)

This branch merges the vinput core reviewed up to dev commit bceb8b4 (cursor guard and physical-input isolation, screenshot
coordinate binding, per-window WndProc binding, retarget input drain, readiness, pad stagger and trigger aliases, async key
sequences, capture diagnostics) into the public tree. What was measured on the merged tree: `build.bat` x64 + x86 completed with 0
warnings and 0 errors; every new fixture source compiles with `/W4 /WX` (x64); the MCP server builds and its offline tests pass
(`npm test`). **No native fixture, suite plan, gate or live game has been run on the merged tree**, so none of the suites in the
rows above has a result for it, and `run_ownerinputtest.ps1` and `run_chordtest.ps1` have not been re-run against the merged
foreground logic (the Ctrl / Shift chord case is now served by the core's snapshot-based character translation). Run
`verify\run_selftest.ps1` before relying on any of it.

Open items carried from the dev review, not fixed here: a payload's posted `WM_INPUT` can be served to a game that still ignores
it (not reproduced or fixed in a real game); Fallout-style resize / freeze after a human alt-tab is unmeasured. See the README's
*Cursor guard and physical input*.

### Merge of dev commits 830e2d3, 8827920 and 84eea6a (2026-11-04)

- **830e2d3** launch window selection and injection-result reporting (`launch` replies now carry the `inject`,
  `launch_readiness`, `early_inject` and `dialog_inject` records).
- **8827920** `ClipCursor` is forwarded natively while the game owns the verified real foreground, then released
  conditionally (see the README). Dev evidence: native-clip fixture 42 named semantic FAIL -> 0 (21 per arch), background
  plan 84 PASS / 0 FAIL / 1 NOT MEASURED. OS-level confinement with a real user and real games is **not measured**.
- **84eea6a** a lost window-binding witness no longer drops the game's window procedure to `DefWindowProc`: GUI messages
  still reach it, and input to that window is quarantined. Dev evidence: 12-case matrix 126 FAIL occurrences -> 0;
  retarget quiescence 814 / 0. Only targeted suites ran on dev for this commit.
- Gate on the merged tree (08d4fdf, 2026-11-04): `build.bat` x64 + x86 0 warnings; background plan via
  `verify\run_selftest_parallel.ps1`: all 85 cases passed (70 in the first parallel run, the rest in reruns after the
  fixes in that commit; `run_actest.ps1` 26 PASS / 0 FAIL; `run_capturediagnosticssurrogatetest.ps1` 466 PASS / 0 FAIL
  when run alone). `capture_diagnostics` failed twice in a parallel lane on a complete-window-enumeration check, on a
  different check each time, while other windows were being created and destroyed. Run it alone or in the serial lane.
  `mcp`: `npm test` 19 files PASS. Foreground suites not run.
- Merged by hand into this tree (it carries the anti-cheat refusal and other public-only code); five conflicts resolved
  in `vinput_cli.cpp` / `vinput_core.cpp`. The dev-only fixtures (`vi_nativeclip`, `vi_wndquarantine`, `vi_launch_*`)
  are copied into `test/` but are not yet part of `build.bat` or `vi_suiteplan.ps1`.
- Fixer batch aff3016..4296f19 (2026-11-04): Caps/Num/Scroll toggle bits kept when key state is masked; OCR / text
  search no longer overwrites the saved screenshot map; MCP `launch` passes `args` starting with `--` intact;
  `capture_after` falls back to PrintWindow only when status shows no Present stream; `capture_diagnostics` reports
  not_measured (exit 3) instead of failing when window enumeration cannot be proven complete.
  Gate on the merged tree: `build.bat` x64 + x86 OK; `mcp` `npm test` 21 files PASS; background plan (86 cases,
  new `ocr_map`): 83 passed, 3 not_measured (`capture_diagnostics` by design; `cursor_guard` and one other where the
  passive desktop changed during the run). Run 3 passed 77; `di_type_lifetime` missed a startup token under
  parallel load and passed on rerun together with the 5 lane cases it blocked; `owner_input` failed once when the
  real cursor moved during the run and passed 22 / 0 in the next. `run_actest.ps1` 26 PASS / 0 FAIL.
- Controller batch a2eea77..b60a66d + ce8bb4f (2026-11-04): MCP `pad_move` (named directions, walk/run speeds,
  screen-rotation compensation, optional smoothing, image after a settle delay), `pad_look` (caller-calibrated degree
  turns), `pad_rotate` (stick circles/arcs for rotate prompts), `attach arm_pad` (neutral virtual pad before the first
  press), a cold-XInput bypass while isolated, and completed-job replies without the misleading `accepted:false`.
  Gate on the merged tree: `build.bat` x64 + x86 OK; `mcp` `npm test` 26 files PASS; `run_padreadinessnativetest.ps1`
  20 PASS / 0 FAIL; `run_controllernativetest.ps1` 282 PASS / 0 FAIL; background plan (87 cases, new `pad_readiness`):
  84 passed, 3 not_measured (`capture_diagnostics` by design; `cursor_guard`, `legacy_focus` with the desk in use).
  `owner_input` failed once when the real cursor moved and passed on the serial-lane rerun.
  These checks use synthetic XInput fixtures; behaviour in real games is a separate field test.
- Batch 0e6adda..fe867d2 (2026-11-04): `capture --scale` up to 4 (coordinates stay mapped), quiet `capture-burst`,
  compact virtual key state in MCP `status`, an MCP `wait` tool, duplicate-flag errors that name the flag, and a
  Unity-style mouse/focus fixture. Gate on the merged tree: `build.bat` x64 + x86 OK; `mcp` `npm test` 28 files PASS;
  `run_ocrmaptest.ps1` 10 / 0 and `-UpscaleOnly` 36 / 0; `run_unitymousetest.ps1` 24 / 0; background plan (87 cases):
  84 passed, 3 not_measured (same three as before). `owner_input` failed once with the real cursor moving and passed on
  the serial-lane rerun; it needs an idle desk.
- Batch fe867d2..7ab27ff (2026-11-04, MCP only, native binaries unchanged from 0.2.1): sized controller action
  images, trigger/stick ranges in tool fields, `launch` `arm_pad` + `claim_ttl_s` and a `claim` tool, pad returns in
  compact `status`, bounded previews for oversized bursts, and a read-only `doctor` tool. Gate: `mcp` `npm test`
  34 files PASS; `doctor` against a real, uninjected process returned `status_available:false` and did not inject.
- Batch 7ab27ff..a8bc5e2 (2026-11-04): while a game is claimed, its cursor writes, clips and exclusive DirectInput
  mouse acquisition stay virtual (the GTA IV mouse trap); an opt-in `escape-host` hotkey (Ctrl+Alt+Shift+End);
  `pad_drive`; capture options shared by keyboard and mouse actions; a bounded `wait` for `key_sequence`; launch
  readiness and D3D9 negotiation diagnostics; a controller API census. Gate: `build.bat` x64 + x86 OK; `mcp` `npm test`
  38 files PASS; background plan (89 cases, now including `startup_activation` and `claimed_cursor_writer`): 70 passed
  on the first run, and the 19 failed or not-run cases (`di_type_lifetime` under load, `owner_input` with the desk in
  use, and the cases queued behind them) all passed on a rerun, so 89 / 89. Real-desktop hotkey delivery and a real
  game cursor trap are not measured in the background suites.
- Commit 281961b (2026-11-04): `click_text` / text clicks match whole OCR words by default, so "PLAY" no longer
  clicks "Player". Gate: `build.bat` x64 + x86 OK; `mcp` `npm test` 39 files PASS; `run_textwordmodeltest.ps1`
  22 / 0; background plan (89 cases): 86 passed, 3 not_measured (capture_diagnostics, cursor_guard, legacy_focus).

## Real games

Automated tests cannot include commercial games. During development vinput was also exercised by hand on installed
single-player games (background menus, loading saves, walking, map toggles, two games at once while another window
held the focus): Direct3D 11 games on Unity and Unreal Engine 4, a Direct3D 12 UWP game (capture and XInput pad), and a
Unity game in OpenGL mode (capture). **Vulkan capture has been verified only on the synthetic target, not in a real
game.** OCR on stylised game fonts is read inconsistently from run to run; use regions, `--scale 2` or image matching.

Agent-driven field runs (an LLM playing through vinput, the game in the background the whole time):

| game | what was done | evidence |
|---|---|---|
| Oxygen Not Included (Unity, D3D11, 2560x1440) | non-activating launch, `click-text "RESUME GAME"` via OCR, held `d` / `w` to pan the map, `g` dig tool, `esc`; every capture after an input had a new frame hash; `really_foreground:false` throughout | 2026-11-02 run log + captures |
| Phantom Rose 2 Sapphire (`PrintWindow` capture) | full card-battle runs: Silver adventure cleared, then the Gold adventure including its final boss | 2026-11-03 play log |
| Phantom Rose (`PrintWindow` capture) | card battles won while Notepad held the real foreground (recorded demo) | 2026-11-04 video |
| Halo Wars 2 (UWP, D3D12) | RTS play through captures and clicks | 2026-11-03 video |

Lesson from the card games: hover-gated buttons need a `move` and a short wait before the click, and hands reflow after
each card is played (wait about 1 s and capture again before the next placement).

Not verified anywhere: `GetRawInputBuffer` injection (no target uses it), 32-bit real games, D3D9 / D3D10 games.
