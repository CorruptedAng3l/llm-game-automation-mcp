# MCP follow-ups that depend on core (vinput) work

The MCP server only wraps what the core already does. These items still need core work; when they land, expose them in
the MCP layer as described.

| core feature | what the MCP should expose when it lands | until then |
|---|---|---|
| Raw-input mouse look (`WM_INPUT` / `GetRawInputData` delivery for engines that register a raw mouse, UE4) | Nothing new needed if `move_relative` starts working; consider defaulting `config raw=post` when `status` shows `raw_mouse` true and `wm_input` stays 0 after a move. | Use `config raw=post|buffer` and watch the `rid` / `wm_input` counters; steer with keys, `pad`, `pad_ramp` or `pulse`. |
| Time-scale (slow the game's clock so a slow agent loop can play real-time games) | A `time_scale` tool or `config` field, plus a note in `status`. | Prefer turn-based games, short key taps, `capture_after`, and async holds with `job_status`. |
| Capture from the wrapper's presentation window for DirectDraw-wrapper games (DDrawCompat and similar) | None expected; `status` should report which window was adopted. | Black captures on a DirectDraw game usually mean vinput is capturing the original window rather than the wrapper's. |
| Persistent pipe client (avoid spawning `vinput.exe` per call, saves about 30-80 ms) | Internal to `mcp/src/vinput.ts`. | Each tool spawns the CLI. |

## Done (kept here briefly so older notes still resolve)

- Capture scaling / JPEG in the core: `capture` passes `max_width`, `scale <= 1`, `format: "jpeg"` and `quality` to
  vinput (`image.scaled_by: "vinput"`); the TypeScript resize remains the fallback for an older DLL and does `zoom`.
- Non-blocking holds: `key` / `pad` `async:true` return a `job_id`; `job_status`, `job_cancel`, `hold_while`, `pulse`,
  `pad_ramp`, `pad_timeline`.
- Polled clicks: vinput's `click` waits for the game's DirectInput read by default; `click` returns `evidence`
  (`di_poll_confirmed`, `di_release_confirmed`), tunable with `di_poll_timeout_ms` / `di_release_timeout_ms`.
  `drag placement:true` uses the owned polled placement drag.
- Contact sheets: `capture_burst`.
