# MCP follow-ups that depend on core (vinput) work

The MCP server only wraps what the core already does. These items are being built in the core separately; when they land,
expose them in the MCP layer as described. Nothing here is implemented in `mcp/` yet.

| core feature | what the MCP should expose when it lands | until then |
|---|---|---|
| Capture scaling / JPEG in the core (smaller frames straight from the game) | `capture` options `format: "jpeg"`, `quality`, and core-side `max_width` passed to vinput's `capture`; drop the TypeScript resize for the fast path. | `capture max_width/scale/zoom` resizes the PNG in TypeScript (full-size PNG still crosses the pipe and is written to disk first). |
| Raw-input mouse look (`WM_INPUT` / `GetRawInputData` delivery for engines that register a raw mouse, UE4) | Nothing new needed if `move_relative` starts working; consider defaulting `config raw=post` when `status` shows `raw_mouse` true and `wm_input` stays 0 after a move. | Use `config raw=post|buffer` and watch the `rid` / `wm_input` counters; steer with keys or `pad`. |
| Time-scale (slow the game's clock so a slow agent loop can play real-time games) | A `time_scale` tool or `config` field, plus a note in `status`. | Prefer turn-based games, short key taps, and `capture_after`. |
| Non-blocking holds (`key hold_ms` and `pad ms` currently block the game's command queue) | `key` / `pad` return immediately with a hold id; `capture` and `status` keep working while it runs; a `hold_while` helper. | `key_down` / `key_up` (or `pad hold:true`, `pad off`) around a `capture`. |
| Polled clicks (a click that waits until the game has actually polled the button state) | Make `click` use it by default and report `polled:true/false`; then `hold_ms` / `hover_ms` become fallbacks. | `click` holds the button 140 ms by default (`hold_ms`), 300 for old menus, ~900 for placement. |
| Capture from the wrapper's presentation window for DirectDraw-wrapper games (DDrawCompat and similar), on another branch | None expected; `status` should report which window was adopted. | Black captures on a DirectDraw game usually mean vinput is capturing the original window rather than the wrapper's. |
| Persistent pipe client (avoid spawning `vinput.exe` per call, saves about 30-80 ms) | Internal to `mcp/src/vinput.ts`. | Each tool spawns the CLI. |
