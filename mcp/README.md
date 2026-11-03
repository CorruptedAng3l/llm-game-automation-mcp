# llm-game-automation-mcp - MCP server

> **Single-player / offline games only.** It injects a DLL into the game. Never point it at online / multiplayer games
> or games with anti-cheat. See [../DISCLAIMER.md](../DISCLAIMER.md).

A [Model Context Protocol](https://modelcontextprotocol.io) server (stdio transport, official
`@modelcontextprotocol/sdk`) that lets any LLM agent play Windows games through vinput: input and screenshots go to
one game process **in the background**, so the game never needs focus and you keep your mouse and keyboard.

Each tool runs `vinput.exe` and returns **vinput's own JSON reply** as text. `ok` is surfaced as the MCP `isError`
flag: `ok:false` (or a crash, timeout or missing binary) always comes back as `isError: true` with the error text,
never silently. `capture` also returns the PNG as MCP image content.

## Build

Requires Windows 10/11 x64, Node.js 20+, and the vinput binaries (`build.bat` in the repository root).

```bat
cd mcp
npm ci
npm run build
npm test          &rem handshake: starts build\index.js, initialize + tools/list, checks every schema
npm run smoke     &rem live end-to-end run against the synthetic test target (needs build.bat output)
```

## Configure your client

The server finds `vinput.exe` at `<repo>\dist\vinput.exe`. Environment variables (all optional):

| variable | meaning |
|---|---|
| `VINPUT_EXE` | path of `vinput.exe` if it is somewhere else |
| `VINPUT_CAPTURE_DIR` | where `capture` saves PNGs when no path is given (default `<repo>\captures`) |
| `VINPUT_OWNER` | owner name for vinput's per-game claims (default `mcp-<server pid>`) |
| `VINPUT_MCP_MAX_IMAGE_BYTES` | larger captures are saved but not attached as images (default 8 MiB) |

**Claude Code**

```bat
claude mcp add game-automation -- node C:\path\to\llm-game-automation-mcp\mcp\build\index.js
```

**Claude Desktop** - `%APPDATA%\Claude\claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "game-automation": {
      "command": "node",
      "args": ["C:\\path\\to\\llm-game-automation-mcp\\mcp\\build\\index.js"],
      "env": { "VINPUT_CAPTURE_DIR": "C:\\path\\to\\captures" }
    }
  }
}
```

**Any other MCP client** - start a stdio server with command `node` and argument
`C:\path\to\llm-game-automation-mcp\mcp\build\index.js`. It speaks MCP over stdin/stdout and logs only to stderr.

## Agent quick start

```
1. launch {exe:"C:\\Games\\X\\X.exe", args:"-windowed"}      -> remember pid   (or attach {pid} / list_targets)
2. capture {pid}                                               -> look at the image (pixels = click coordinates)
3. wait_text {pid, text:"New Game"}                            -> make sure the screen is ready
4. click_text {pid, text:"New Game"}                           -> act by text; several matches = refusal, add nth
5. wait_text {pid, text:"Difficulty"} or wait_change {pid}     -> verify; ok:true on input only means "delivered"
6. click {pid, x, y} / key {pid, key:"enter"} / pad {pid, buttons:"a"}   -> other actions
7. key {pid, key:"w", hold_ms:1500}; move_relative {pid, dx:200, dy:0}   -> walk / look in 3D games
8. capture {pid, after_frames:3}                               -> look again after acting
9. status {pid}                                                -> counters show which input APIs the game reads
10. release {pid}; detach {pid}                                -> stop driving; the game keeps running
```

## Tools

| tool | what it does |
|---|---|
| `launch` | start a game (`exe` / `aumid` / `steam_appid`+`wait_exe`, `args`, `cwd`, `early`, `settle_s`, `timeout_s`, `lock`, `claim`) and inject; returns `pid`, plus `warnings` / `nothing_presented` / `fullscreen_refusals` when no frame is being presented |
| `attach` | inject into a running game by `pid` and wait until its window is adopted |
| `list_targets` | games that have vinput loaded (pid, exe, owner, uptime, really_foreground) |
| `status` | compact by default: pid, hwnd, client size, `really_foreground`, `app_active`, `present`, cursor / raw / msgs modes, non-zero counters, `warnings[]`; `verbose:true` = the full vinput status |
| `config` | change / read how vinput treats a game: `fakefocus`, `autoactivate`, `isolate`, `blockfocus`, `nofullscreen`, `keepvisible`, `cursor`, `raw`, `msgs`; returns the effective config |
| `log` | tail of vinput's log for a game (`lines`, `contains`, `source` ring/file); repeated lines collapse |
| `trace` | per-key delivery trace (`trace key <key>`): which input channels the game read |
| `capture` | the game's own frame as an image + `{path, w, h, method, capture_path, frame}`; `region`, `after_frames`, `method`, `include_image`, `grid` (labelled client-coordinate grid on the returned image), `max_width` / `scale` / `zoom` (resize the returned image; reply has `image {scale, offset_x, offset_y}`) |
| `ocr` | text and lines with boxes (`region`, `scale`, `lang`, `words`, `tiles`) |
| `find_text` | locate text without clicking (box, centre, match_count) |
| `click_text` | find text and click its centre (`nth`, `exact`, `fuzzy`, `timeout_ms`, `button`, `region`, `optional`) |
| `wait_text` | wait until text appears (or `gone`) |
| `find_image` / `click_image` / `wait_image` | template matching with a PNG (`threshold`, `scales`, `region`, `nth`) |
| `pixel` | colour of one pixel |
| `wait_change` | wait until the picture (or a region) changes |
| `click` | click at `x`,`y` (client pixels, or `norm` 0..1, `capture_space`, or `coord_space:"capture"` for the image you were shown), `button`, `count`, `hover_ms` (default 80), `hold_ms` (default 140), `edge_margin`, `park_cursor` |
| `move` | move the virtual cursor (hover) |
| `move_relative` | relative mouse motion for mouse-look (`dx`, `dy`, `raw`) |
| `drag` | press, hold (`hold_start_ms`), stepped move (`ms`, `steps`), dwell at the destination (`hold_end_ms`), release; always releases |
| `scroll` | wheel notches at the cursor or at `x`,`y` |
| `key` | press a key or chord (`hold_ms`, `count`, `gap_ms`) |
| `key_down` / `key_up` | hold / release a key (or mouse button name) |
| `type_text` | type Unicode text (`as_keys` for games that read raw keys) |
| `pad` | XInput pad 0: `buttons`, sticks `lx ly rx ry`, triggers `lt rt`, `ms`, `hold`, `off` |
| `list_keys` | all key names |
| `run_script` | run a `.vin` macro file |
| `release` | release all held input |
| `detach` | release input and drop this server's claim (vinput stays loaded, inert, until the game exits) |

Every tool that targets a game takes `pid`. Coordinates are the game's **client pixels**, which equal the pixels of a
full capture. Refusals by the anti-cheat check come back as `isError` with `anticheat_refused: true`; there is no way to
override them from the MCP server.

## Notes for agents

- **Verify every action.** `ok:true` from `click`, `key` or `click_text` means the input reached the game, not that the
  game did what you wanted. Use `wait_text`, `wait_change` or a new `capture`.
- **Text matching** is case-insensitive, ignores whitespace and matches substrings of a line. If a label appears more
  than once, `click_text` refuses and lists candidates: pass `nth` or a `region`.
- **OCR misses** small or stylised text: use `region` + `scale: 2`, or cut an icon from a capture and use
  `click_image`.
- **3D games** often read raw input: walk with `key` + `hold_ms`, look with `move_relative`; check `status` counters
  if nothing happens. UWP games often ignore keyboard messages but read XInput: use `pad`.
- **Big captures** cost tokens: capture a `region`, or `max_width` (for example 960), when you do not need the full frame.
- **Do not guess pixels:** `capture grid:100` labels exact client coordinates on the image; `zoom` + `region` reads small things.
- **Act and look in one call:** `capture_after:true` on `click`, `key`, `click_text`, `move`, `drag`, `pad` returns the next frame.
- **Every tool answers `target exited (pid N, exit code X)`** once the game has ended, not a pipe error.
- More: [../docs/AGENT_TIPS.md](../docs/AGENT_TIPS.md) (setup, click timing, drag, per-game notes) and [../docs/MCP_TODO.md](../docs/MCP_TODO.md) (waits on core features).
