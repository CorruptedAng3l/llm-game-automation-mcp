# Tips for LLM agents playing games through the MCP server

Short and practical; every point comes from a field test (GTA Vice City DE, Fallout 1, Fallout 2, The Sims). Tool names
are the MCP tools in [`mcp/README.md`](../mcp/README.md).

## Set the game up first

- **Use a fixed windowed or borderless size, set in the game's own settings or INI.** Exclusive fullscreen cannot be
  captured or driven in the background: vinput refuses `SetFullscreenState(TRUE)`, and some engines then retry every
  frame and never present a frame (GTA Trilogy DE deadlocked this way). `launch` and `capture` now say so
  (`fullscreen_refusals`, a warning naming the fix); `log` with `contains:"fullscreen"` shows the refusals.
  A saved fullscreen setting usually overrides command-line switches, so fix the INI, not just `args`.
- **A window that drifts in size** (a game that re-fits itself) moves every coordinate. Pin the size in the game's
  config and re-read `status` `client` after launch.
- **Background launch plus autoactivate.** `launch` keeps the real foreground where it was. A game started in the
  background never receives "you are active" and DirectDraw-era games then ignore input; vinput sends the activation
  messages itself (`autoactivate`, on by default). Check `status` -> `app_active` is `1` before blaming anything else; if
  it stays `0` the status warning says what to try (`config autoactivate=true`, or the game's own background-input
  options, see the table below).
- Old games that open files relative to their working directory work with the default (the exe's own folder). Use
  `launch cwd` only when a game wants a different one.

## Look precisely, then act

- **Do not guess pixels from a big screenshot.** `capture grid:100` draws a labelled coordinate grid (every 100 client
  px) on the image you are shown; read the coordinates off it. Inside a region use `grid:20` or `grid:50`. The PNG saved
  at `path` stays clean, so `find_image` templates cut from it are exact.
- **Zoom in on small things.** `capture region:{x,y,w,h} zoom:3 grid:20` shows a crop at 3x with the grid still in
  client coordinates.
- **Save tokens on big frames.** `capture max_width:960` (or `scale:0.5`) downsizes the returned image. The reply's
  `image: {scale, offset_x, offset_y}` maps it back (`client = offset + image / scale`). Pass `coord_space:"capture"`
  to `click` / `move` / `drag` and they convert the pixels you read off that image for you (they use the last capture
  of that game; give `mapping` to override). The default stays client coordinates.
- **Act and look in one call.** `capture_after:true` on `click`, `key`, `click_text`, `move`, `drag` and `pad` returns
  the frame after the action (it waits 3 presented frames; pass a number for more). A failed action takes no picture.
- `status` is compact by default. Read `warnings` first; `verbose:true` gives the full 3 KB reply with every counter.
  When a game ignores input, the non-zero counters show which input APIs it actually reads.

## Click reliably

- **Click the button graphic, not the label beside it.** Many old menus (Fallout 1/2, others) only react to the round or
  square button drawn next to the text; clicking the label does nothing and looks like a dead mouse. `click_text` aims at
  the text, so for those menus use `capture grid` and click the graphic, or `click_image` on the button.
- **Hover first, and hold the press.** Games that poll the mouse once per frame or tick miss short clicks. `click`
  hovers 80 ms over the target (`hover_ms`) and holds the button 140 ms (`hold_ms`) by default. Measured: under about
  200 ms is often ignored in The Sims; Fallout 1 menus needed about 300 ms (`hold_ms:300`); placing furniture in The Sims
  needed about 900 ms (`hold_ms:900`). Raise `hover_ms` too if hover-gated UI (highlight, tooltip) must appear first.
- **Drag-and-drop** (inventory, furniture, map items): `drag` presses, holds `hold_start_ms` (150), moves in `steps`
  over `ms`, dwells `hold_end_ms` (250) at the destination and then releases. A plain press-move-release is dropped by
  many games; raise `hold_end_ms` to 500-900 for placement. Never use `key_down`/`key_up` for mouse drags unless you
  need to look mid-drag (`key_down lbutton`, `move`, `capture`, `key_up lbutton` also works).
- **Edge scrolling.** Many games pan the view while the cursor rests at the window edge (The Sims, strategy games), so
  items land in the wrong place. `click`, `move`, `drag` and `scroll` warn when a position is within `edge_margin` px
  (default 8) of the edge. Aim further in, or add `park_cursor:true` so the cursor ends in the middle.
- **Verify every action.** `ok:true` means the input was delivered, not that the game acted. Use `capture_after`,
  `wait_text` or `wait_change`.

## Holding input while you look

- **`key` with `hold_ms` blocks the game's command queue** for the whole hold: a capture sent meanwhile waited its full
  length (1.7 s measured). To look while holding (driving, walking), use `key_down`, then `capture`, then `key_up`
  (or `pad` with `hold:true`, then `pad off`). Always `release` at the end.
- Keyboard steering is all-or-nothing: a 400 ms tap of `d` at speed turned a car about 90 degrees. Use taps of
  100-150 ms and look in between.
- Each decision costs a round trip (a capture-view-decide loop took 5 s median on a real run, about 75 m of travel in a
  car). Prefer slow, safe moves in real-time games, and use `capture_after` to cut the loop.

## When something does not work

1. `status`: `warnings`, `app_active`, `present` (0 = no Direct3D/OpenGL/Vulkan frame yet).
2. `log` (add `contains` to filter): hooks that failed, refused fullscreen, errors from the injected DLL.
3. `trace key <key>` after pressing the key: shows through which channels (messages, `GetAsyncKeyState`, DirectInput,
   raw input) the key was delivered and which the game actually read.
4. `config`: change how vinput treats the game (`raw=post|buffer` for mouse-look that never arrives, `cursor=real`,
   `msgs=on`, `nofullscreen=false`); it returns the config now in effect.
5. If the game exited, every tool answers `target exited (pid N, exit code X)`.

## Game notes

Generic and factual; check each against your build of the game.

| game / engine | note |
|---|---|
| Fallout 1 / 2 (with sfall `ddraw.ini`) | Set `BackgroundMouse=1` and `BackgroundKeyboard=1`, or a game started in the background dies with "Failure initializing input devices". Menu buttons are the round graphics left of the labels. Menu clicks need hover first and a ~300 ms press. A fixed window size (for example 640x480 with 2x scaling = 1280x960) keeps coordinates stable. Captures read through PrintWindow. |
| GTA Trilogy Definitive Edition (Unreal Engine 4) | Saved exclusive fullscreen deadlocks the game under vinput (the fullscreen request is refused every frame and nothing is ever presented). Set windowed in its INI first. Mouse look through `move_relative` needs raw-input work (see [MCP_TODO.md](MCP_TODO.md)); steer with keys or `pad`. |
| The Sims (2000) | Launch it windowed with `-w -r800x600` (or the resolution you want). Under the DDrawCompat wrapper, `FullscreenMode=borderless` still covers the whole monitor with the wrapper's presentation window and the user cannot Alt-Tab out; `windowed` is not a valid `FullscreenMode` value in that build. Clicks shorter than about 200 ms are often ignored and furniture placement needed a press held about 900 ms (`hold_ms:900`). The view scrolls when the cursor is at the window edge. |
| DirectDraw / Direct3D-wrapper games (DDrawCompat, dgVoodoo, DXVK and similar) | Capture comes from the wrapper's own window, not the game's original one. A black capture on a DirectDraw game usually means vinput is looking at the wrong window (fix in progress, see [MCP_TODO.md](MCP_TODO.md)). `status` `client` and `hwnd` show which window vinput adopted. |
| Games with a saved fullscreen setting | Command-line switches are often overridden by the saved setting; edit the INI/registry setting the game itself uses. |
