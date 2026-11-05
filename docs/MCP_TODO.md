# MCP follow-ups that depend on core (vinput) work

The MCP server only wraps what the core already does. These items still need core work; when they land, expose them in
the MCP layer as described.

| core feature | what the MCP should expose when it lands | until then |
|---|---|---|
| Raw-input mouse look that every engine accepts (UE4 and others that register a raw mouse). vinput delivers `WM_INPUT` / `GetRawInputData` / `GetRawInputBuffer`, but a game can read a served packet and still ignore it; that case is not fixed. | Nothing new if `move_relative` starts working; consider defaulting `config raw=post` when `status` shows `raw_mouse` true and `wm_input` stays 0 after a move. | Try `config raw=post` or `raw=buffer` and watch the `rid` / `wm_input` counters in `status`. Measure a gain with `look_calibrate mode:"counts"` and turn with `turn` / `look` / `look_turn`; if the view still does not move, steer with keys, `pad`, `pad_look`, `pad_ramp` or `pulse`. |
| Time-scale (slow the game's clock so a slow agent loop can play real-time games) | A `time_scale` tool or `config` field, plus a note in `status`. | Prefer turn-based games and short moves. Use `capture_after`, async holds with `job_status`, one-call jobs (`key_sequence wait:true`, `pad_timeline`, `pad_drive`, `hold_until_change`, `move_until_stuck`) and `compact:true` to cut round trips. |
| Old DirectDraw games behind a `ddraw.dll` wrapper such as the Fallout 1 High Resolution Patch 4.1.8: the window is never adopted (`window_not_adopted_or_subclassed`, log `hooks FAILED: DirectDrawCreateEx(missing)`), and `early:true` breaks the game's own DirectInput/video init. | Nothing new; `launch` should adopt the window and say which ddraw path it hooked. | Not playable through vinput yet. |
| Persistent pipe client (avoid spawning `vinput.exe` per call, saves about 30-80 ms) | Internal to `mcp/src/vinput.ts`. | Each tool spawns the CLI. |

## Done (kept here briefly so older notes still resolve)

- Capture from a DirectDraw wrapper's presentation window (DDrawCompat and similar): the `PrintWindow` path also reads a
  visible same-process `...PresentationWindow` (`src/vi_overlaycapture.h`, `verify/run_overlaycapturetest.ps1`). A
  `PrintWindow` reply's `capture_diagnostics` names the source window.
- Capture scaling / JPEG in the core: `capture` passes `max_width`, `scale <= 1`, `format: "jpeg"` and `quality` to
  vinput (`image.scaled_by: "vinput"`); the TypeScript resize remains the fallback for an older DLL and does `zoom`.
- Non-blocking holds: `key` / `pad` `async:true` return a `job_id`; `job_status`, `job_cancel`, `hold_while`, `pulse`,
  `pad_ramp`, `pad_timeline`.
- Polled clicks: vinput's `click` waits for the game's DirectInput read by default; `click` returns `evidence`
  (`di_poll_confirmed`, `di_release_confirmed`), tunable with `di_poll_timeout_ms` / `di_release_timeout_ms`.
  `drag placement:true` uses the owned polled placement drag.
- Contact sheets: `capture_burst`.
