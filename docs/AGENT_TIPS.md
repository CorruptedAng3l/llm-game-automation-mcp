# Tips for LLM agents playing games through the MCP server

Short and practical; every point comes from a field test (GTA Vice City DE, Fallout 1, Fallout 2, The Sims). Tool names
are the MCP tools in [`mcp/README.md`](../mcp/README.md).

## Set the game up first

- **Use a fixed windowed or borderless size, set in the game's own settings or INI.** Exclusive fullscreen cannot be
  captured or driven in the background. By default vinput answers a DXGI fullscreen request with a virtual (borderless)
  fullscreen so the game keeps presenting (`config fullscreen:"borderless"`); `fullscreen:"refuse"` makes the request
  fail instead, and some engines then retry every frame and never present a frame (GTA Trilogy DE deadlocked this way).
  `launch` and `capture` say so (`fullscreen_refusals`, a warning naming the fix); `log` with `contains:"fullscreen"`
  shows the requests; `status` `fullscreen` shows the policy. A saved fullscreen setting usually overrides
  command-line switches, so fix the INI, not just `args`. `launch` compares the window it got with a size given in `args`
  (`-screen-width/-screen-height`, `-width/-height`, `-ResX/-ResY`, `-w N -h N`, `-rWxH`) and warns with
  `requested_size_ignored` when they differ: set the size in the game's own options, then re-read `status` `client`.
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
- **Save tokens on big frames.** `capture max_width:960` (or `scale:0.5`) downsizes the returned image, and
  `format:"jpeg"` (`quality`, default 85) makes it much smaller still. vinput shrinks the frame inside the game when its
  DLL supports it (`image.scaled_by:"vinput"`), otherwise the MCP server does (`"mcp"`). The reply's
  `image: {scale, offset_x, offset_y}` maps it back either way (`client = offset + image / scale`). Pass `coord_space:"capture"`
  to `click` / `move` / `drag` and they convert the pixels you read off that image for you (they use the last capture
  of that game; give `mapping` to override). The default stays client coordinates.
- **Watch something happen in one image.** `capture_burst frames:8 interval_ms:250` returns a contact sheet of tiles in
  time order (animations, loading, whether a held key moves the character). Do not click coordinates read off a sheet.
- **Act and look in one call.** `capture_after:true` on `click`, `key`, `click_text`, `move`, `drag` and `pad` returns
  the frame after the action (it waits 3 presented frames; pass a number for more). A failed action takes no picture.
- `status` is compact by default. Read `warnings` first; `verbose:true` gives the full 3 KB reply with every counter.
  When a game ignores input, the non-zero counters show which input APIs it actually reads.

## Click reliably

- **Click the button graphic, not the label beside it.** Many old menus (Fallout 1/2, others) only react to the round or
  square button drawn next to the text; clicking the label does nothing and looks like a dead mouse. `click_text` aims at
  the text, so for those menus use `capture grid` and click the graphic, or `click_image` on the button.
- **Hover first, and hold the press.** Games that poll the mouse once per frame or tick miss short clicks. `click`
  hovers 80 ms over the target (`hover_ms`) and holds the button 140 ms (`hold_ms`) by default. For games that read the
  mouse through DirectInput vinput also waits until the game has read the press and the release; the reply's
  `evidence` (`di_poll_confirmed`, `di_release_confirmed`) says what was observed, and `di_poll_timeout_ms` /
  `di_release_timeout_ms` make those reads required. Measured: under about
  200 ms is often ignored in The Sims; Fallout 1 menus needed about 300 ms (`hold_ms:300`); placing furniture in The Sims
  needed about 900 ms (`hold_ms:900`). Raise `hover_ms` too if hover-gated UI (highlight, tooltip) must appear first.
- **Drag-and-drop** (inventory, furniture, map items): `drag` presses, holds `hold_start_ms` (150), moves in `steps`
  over `ms`, dwells `hold_end_ms` (250) at the destination and then releases. A plain press-move-release is dropped by
  many games; raise `hold_end_ms` to 500-900 for placement. `drag placement:true` runs the whole drag inside the game
  and waits for the game to read each step (games that read `GetCursorPos` and DirectInput buttons); with an older DLL,
  or a game that reads buttons through window messages (Unity: `button_down_not_observed`), it falls back to the normal
  sequence and says so (`placement_fallback`). In Unity games plain `drag` is the right tool. Never use `key_down`/`key_up` for mouse drags unless you
  need to look mid-drag (`key_down lbutton`, `move`, `capture`, `key_up lbutton` also works).
- **Edge scrolling.** Many games pan the view while the cursor rests at the window edge (The Sims, strategy games), so
  items land in the wrong place. `click`, `move`, `drag` and `scroll` warn when a position is within `edge_margin` px
  (default 8) of the edge. Aim further in, or add `park_cursor:true` so the cursor ends in the middle. A current
  vinput DLL also keeps injected cursor paths that many px inside the edge itself (`config edge_margin:N`, 0 = off);
  replies then carry `edge_contact` / `edge_guard_applied`, and `status` warns while the cursor rests on the edge.
- **Verify every action.** `ok:true` means the input was delivered, not that the game acted. Use `capture_after`,
  `wait_text` or `wait_change`.

## Holding input while you look

- **Hold in the background, keep looking.** `key hold_ms:2000 async:true` (and `pad ms:... async:true`) returns at once
  with a `job_id`; the hold runs inside the game with its own deadline while `capture` and `status` keep working.
  `job_status` follows it, `job_cancel` stops it early and releases only what that job held. Accepted means scheduled,
  not delivered: check `job_status` or a capture.
- **Hold until something shows up.** `hold_while key:"w" until_text:"Village" timeout_ms:5000` holds and watches the
  screen in one call, and always releases when it returns.
- Keyboard steering is all-or-nothing: a 400 ms tap of `d` at speed turned a car about 90 degrees. Use `pulse`
  (key PWM: `duty` percent of each `period_ms`) for gentler digital steering, or `pad_ramp` / `pad_timeline` for
  proportional stick input on games that read XInput. Without those, use taps of 100-150 ms and look in between.
- Each decision costs a round trip (a capture-view-decide loop took 5 s median on a real run, about 75 m of travel in a
  car). Prefer slow, safe moves in real-time games, and use `capture_after` to cut the loop.

## Sequences, staggered pads and clicking from a screenshot

- **A fixed key routine is one job, not many calls.** `key_sequence steps:[{action:"hold",key:"w+space",ms:1500},
  {action:"wait",ms:100},{action:"key",key:"d",ms:300}]` returns a `job_id` at once; follow it with `job_status` (it shows the
  current step) and stop it with `job_cancel`, which releases the active chord. Pressing a key a later step uses cancels the rest.
- **Menus that need the stick first, then the button.** `pad_timeline` states are complete snapshots and take `buttons` and a
  `phase` label; `stagger_ms: 60` makes a new button / trigger press wait until the movement stick has been published (and, with
  frame pacing, a later frame). `lt` / `rt` also work inside `buttons` (`"a+lt"`). Check the result on screen: the receipt
  says what vinput published, not what the game did.
- **After `attach`, wait for the first action to show.** `ok:true` means the hooks, window and queue were ready, not that the
  game or its menu is. The first key can still land before the game polls.
- **Do not leave `isolate` off.** `config isolate:false` lets the user's real keyboard and mouse into a background game. If you
  need it, bound it: `for_ms: 1000` restores isolation inside the game by itself.
- **Capture-space coordinates go stale on a resize or retarget.** vinput refuses `capture_space:true` clicks then (the map belongs
  to one window and one client size); `coord_space:"capture"` uses this server's remembered mapping and cannot notice. Either way,
  take a fresh single-frame `capture` and read the coordinates again. A black capture next to a second window of the same
  process (see `capture_diagnostics`) may mean the picture is drawn in that other window; the CLI's `config hwnd=0x...`
  selects it (the MCP `config` tool does not expose `hwnd`).
- **A retarget can be refused while input is held.** `release`, wait for the game's queue to progress (`attach`/`status`), retry.

## When the user is at the keyboard

vinput keeps the user's real mouse and keyboard out of a background game. When the user activates the game window
themselves, their real input passes through and the cursor the game reads is the real one until your next input
command; `status` shows `owner_active` (with a warning) and `cursor_effective`. Treat `owner_active:true` as "the user
is playing right now": look before you act, and do not fight them for the mouse.

## Text fields and chords

`key ctrl+a`, `ctrl+z`, `shift+a` and other chords reach the game with their modifier (the character message is built
from vinput's own key state, not the thread's real one). To replace a text field's content: click it, `key ctrl+a`,
then `type_text`. Check the field with a zoomed `capture` before you confirm.

## City-builder opening checklist

A human player reached about 90 residents within a few in-game days, even with a negative budget, because they used the
information the game provides. An agent that guessed from the 3D view played badly. Do it in this order:

1. Connect the town to the highway with an interchange or on-ramp: one road in and one road out.
2. Run power lines around the zoned area. A power plant only serves the buildings it is connected to.
3. Power the water pump and the sewage drain as well.
4. Lay water pipes under every zoned street.
5. Zone along the roads.

Then read the game, and read it again after every build step:

- Open the Info views panel and read the numbers: electricity consumption against production, water and sewage
  capacity against use.
- Open the Economy > Budget panel too.
- Fix whatever is red before you build more.

The general lesson for any game: read the game's own info and overlay panels before acting, and again after each step,
instead of guessing from the picture. `capture` plus `wait_text` on those panels is cheaper than a wrong build.

## Cities: Skylines (Unity) notes

- Run it windowed (Options > Graphics > Display Mode > Windowed, then Apply): it rewrites its saved display mode on
  start and ignores `-screen-fullscreen 0 -screen-width/-screen-height`. `launch` warns when the window size differs.
- Roads are click-click, not a drag: click the start, click each further point, then click the last point again (or press
  Escape) to finish. Plain `drag` works for zoning brushes and middle-button camera tilt; `drag placement:true` falls back
  to it by itself.
- Connect the road network to the highway first, then power, water and sewage (see the checklist above). Snap a pipe or
  power-line end onto the building or the existing network and check the Info view afterwards: an unconnected end looks
  fine in the 3D view and does nothing.
- Read the Info views and the Economy > Budget panel after every build step (red numbers first).
- Dropdown lists: click the dropdown, then click the item. In the field test the item click once closed the list without
  selecting (Display Mode, fullscreen session, not reproduced since in windowed mode). If an item will not take a click,
  use the fallback: `scroll` one notch over the CLOSED dropdown to step its value, then press the dialog's Apply.

## When something does not work

1. `status`: `warnings`, `app_active`, `present` (0 = no Direct3D/OpenGL/Vulkan frame yet).
2. `log` (add `contains` to filter): hooks that failed, refused fullscreen, errors from the injected DLL.
3. `trace key <key>` after pressing the key: shows through which channels (messages, `GetAsyncKeyState`, DirectInput,
   raw input) the key was delivered and which the game actually read.
4. `config`: change how vinput treats the game (`raw=post|buffer` for mouse-look that never arrives, `cursor=real`,
   `msgs=on`, `nofullscreen=false`); it returns the config now in effect.
5. If the game exited, every tool answers `target exited (pid N, exit code X)`.
6. `ok:true` with no change on screen after 10-15 varied attempts means the game is not reading that input channel. Switch channel (pad <-> keyboard) or stop; don't keep retrying.
7. Black captures with `present` 0 and audio playing: the window may be invisible or exclusive-fullscreen. Relaunch windowed (launch `args`) or set windowed in the game's own settings.

## Game notes

Generic and factual; check each against your build of the game.

| game / engine | note |
|---|---|
| Fallout 1 / 2 (with sfall `ddraw.ini`) | Set `BackgroundMouse=1` and `BackgroundKeyboard=1`, or a game started in the background dies with "Failure initializing input devices". Menu buttons are the round graphics left of the labels. Menu clicks need hover first and a ~300 ms press. A fixed window size (for example 640x480 with 2x scaling = 1280x960) keeps coordinates stable. Captures read through PrintWindow. |
| GTA Trilogy Definitive Edition (Unreal Engine 4) | Saved exclusive fullscreen deadlocks the game under vinput (the fullscreen request is refused every frame and nothing is ever presented). Set windowed in its INI first. Mouse look through `move_relative` needs raw-input work (see [MCP_TODO.md](MCP_TODO.md)); steer with keys or `pad`. |
| The Sims (2000) | Launch it windowed with `-w -r800x600` (or the resolution you want). Under the DDrawCompat wrapper, `FullscreenMode=borderless` still covers the whole monitor with the wrapper's presentation window and the user cannot Alt-Tab out; `windowed` is not a valid `FullscreenMode` value in that build. Clicks shorter than about 200 ms are often ignored and furniture placement needed a press held about 900 ms (`hold_ms:900`). The view scrolls when the cursor is at the window edge. |
| DirectDraw / Direct3D-wrapper games (DDrawCompat, dgVoodoo, DXVK and similar) | Capture comes from the wrapper's own window, not the game's original one. A black capture on a DirectDraw game usually means vinput is looking at the wrong window (fix in progress, see [MCP_TODO.md](MCP_TODO.md)). `status` `client` and `hwnd` show which window vinput adopted. |
| Games with a saved fullscreen setting | Command-line switches are often overridden by the saved setting; edit the INI/registry setting the game itself uses. |
| Grand Theft Auto IV: The Complete Edition (RAGE, D3D9) | Without launch args the game window can stay invisible (audio plays, `present` 0, black captures). Launch with `args:"-windowed -width 1600 -height 900"` and `early:true`. Capture comes through PrintWindow. Driving on `pad`: `rt` 170-200 for 3000 ms covers about 25 m; `lx` ±32000 at `rt` 150-170 for about 2 s turns 90-120°; small `lx` values barely steer; `lt` 255 reverses. Brake before turns and use short low-throttle steps near the marker. If the car is stuck for more than about 10 calls, get out (`y`) or restart the mission instead of grinding. Put multi-phase manoeuvres (reverse, then accelerate) in one `pad_timeline`, because separate calls leave a neutral gap. Clicking into the window lets the game capture the real mouse. |
| Hollow Knight (Unity) | Keyboard works in the background; the virtual controller is currently ignored (see [MCP_TODO.md](MCP_TODO.md)). Arrows move, Z jump, X attack, A focus. Menu keys need `hold_ms` about 250 (80-100 ms presses are often dropped). Walk-jump: one async `right` hold, then `z` about 250 ms later; the chord `right+z` does neither. Use `capture_burst` (6 frames, 120-200 ms, jpeg) to see jumps and hits; a single capture right after the action can miss them. |
| Terraria (XNA, D3D9) | Mouse, long click-holds, keys and `type` work in the background. Capture comes through PrintWindow and can be one frame stale. Wait for a text prompt to appear before `type`. Hold left-click (`ms` 15000+) on a tree trunk to chop; the reach is about 5 tiles and a miss still returns `ok:true`. |
| Detroit: Become Human (Vulkan) | Needs `launch early:true` for capture. Use `attach {arm_pad:true}` so the first pad press counts. `pad_move` with `screen_rotation_deg:180` when the fixed camera faces the character; re-check after every camera cut. `pad_rotate` handles "rotate the stick" prompts (read the start angle and direction from the glyph). Hold A about 1500 ms for "hold" prompts. |
| Fallout Shelter (Unity) | Capture and OCR work, but the game currently ignores all background input (cause unknown). |

## Playtest notes (session 9)
- Cities: Skylines: right-click cancels the active build tool. Escape did not
  cancel it in the Cities run 2 playtest. This is an inherited game-specific
  observation; check the current UI after cancelling.
- Successful native input and completed_injected_points describe delivered
  virtual input. Placement/game acceptance remains unknown. Never resend a
  delivered point just because its following capture failed.
- click_many accepts {x,y} objects or [x,y] pairs. Optional verify_change with a
  small verify_region stops remaining points on unchanged scoped pixels;
  changed pixels alone do not prove placement. hover_many can inspect several
  connection/placement tooltips with per-point hash and optional OCR.
- UI transitions may ignore delivered clicks. Use observe_settle on the relevant
  region before a deliberate click, then click verify_change or wait_text to
  inspect its result. Automatic repeated clicking can double-toggle controls.
- Capture region uses client pixels before scaling. Read image scale/scale_y and
  offsets to map the preview, or use coord_space:"capture". upscale:true plus
  max_width explicitly enlarges small PNG crops; save_path/out_dir select files.
