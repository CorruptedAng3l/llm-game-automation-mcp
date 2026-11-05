# Tips for LLM agents playing games through the MCP server

Short and practical. Every point comes from a field test (GTA Vice City DE, Fallout 1 and 2, The Sims, Cities: Skylines,
GTA IV, Hollow Knight, Terraria, Detroit: Become Human and others) or from the tools' own contracts. Tool names are the
MCP tools in [`mcp/README.md`](../mcp/README.md).

The one rule behind all of them: `ok:true` on an input tool means the input was **delivered**, not that the game acted.
Look again (`capture_after`, `wait_text`, `wait_change`, `observe_settle`) before you decide the next step.

## 1. Launching and setting the game up

- **Use a fixed windowed or borderless size, set in the game's own settings or INI.** Exclusive fullscreen cannot be
  captured or driven in the background. By default vinput answers a DXGI fullscreen request with a virtual (borderless)
  fullscreen so the game keeps presenting (`config fullscreen:"borderless"`); `fullscreen:"refuse"` makes the request
  fail instead, and some engines then retry every frame and never present a frame (GTA Trilogy DE deadlocked this way).
  `launch` and `capture` say so (`fullscreen_refusals`, a warning naming the fix); `log` with `contains:"fullscreen"`
  shows the requests; `status` `fullscreen` shows the policy.
- **A saved fullscreen or size setting usually overrides command-line switches**, so fix the INI or registry setting the
  game itself uses, not just `args`. `launch` compares the window it got with a size given in `args`
  (`-screen-width/-screen-height`, `-width/-height`, `-ResX/-ResY`, `-w N -h N`, `-rWxH`) and warns with
  `requested_size_ignored` when they differ: set the size in the game's options, then re-read `status` `client`.
- **A window that drifts in size** (a game that re-fits itself) moves every coordinate. Pin the size in the game's
  config and re-read `status` `client` after launch.
- **Background launch plus autoactivate.** `launch` keeps the real foreground where it was. A game started in the
  background never receives "you are active", and DirectDraw-era games then ignore input; vinput sends the activation
  messages itself (`autoactivate`, on by default). Check that `status` `app_active` is `1` before blaming anything else;
  if it stays `0` the status warning says what to try (`config autoactivate:true`, or the game's own background-input
  options, see [Game notes](#9-game-notes)).
- Old games that open files relative to their working directory work with the default (the exe's own folder). Use
  `launch cwd` only when a game wants a different one.
- **One copy per game.** A direct `exe` launch refuses when that executable is already running and returns
  `existing_instances` (PID and start-time receipts) without attaching. Use `attach` on that PID, or pass
  `new_instance:true` when you really want a second copy. Steam / AUMID relays and child processes are not covered by
  this check.
- **After `launch` or `attach`, wait for the first action to show.** `ok:true` means the hooks, window and queue were
  ready, not that the game or its menu is. The first key can still land before the game polls.
- **Controllers:** `attach {arm_pad:true}` (or `launch arm_pad:true`) makes the first pad press count in games that
  need a pad to be seen first. Arming can make a game show a device prompt or switch its button glyphs, so leave it out
  until you want pad input.
- **Do not leave `isolate` off.** `config isolate:false` lets the user's real keyboard and mouse into a background game.
  If you need it, bound it: `for_ms:1000` restores isolation inside the game by itself.

## 2. Looking precisely

- **Do not guess pixels from a big screenshot.** `capture grid:100` draws a labelled coordinate grid (every 100 client
  px) on the image you are shown; read the coordinates off it. Inside a region use `grid:20` or `grid:50`. The PNG saved
  at `path` stays clean, so `find_image` templates cut from it are exact.
- **Zoom in on small things.** `capture region:{x,y,w,h} zoom:3 grid:20` shows a crop at 3x with the grid still in
  client coordinates. `upscale:true` with `max_width` also enlarges a small PNG crop. `scale` > 1, `zoom` and `upscale`
  only enlarge the returned preview; `path` is the clean native capture. `include_image:false` refuses enlargement and
  `grid`, because they would change nothing on disk.
- **Save tokens on big frames.** `capture max_width:960` (or `scale:0.5`) downsizes the returned image, and
  `format:"jpeg"` (`quality`, default 85) makes it much smaller still. vinput shrinks the frame inside the game when its
  DLL supports it (`image.scaled_by:"vinput"`), otherwise the MCP server does (`"mcp"`).
- **Clicking on what you saw.** A capture `region` is in client pixels before scaling. The reply's
  `image: {scale, scale_y, offset_x, offset_y}` maps the preview back (`client = offset + image / scale`). Pass
  `coord_space:"capture"` to `click` / `move` / `drag` / `click_many` and they convert the pixels you read off that image
  for you, using the last capture of that game (give `mapping` to override). Their replies include `mapping_used`: the
  scale, offsets, source window scope and `capture_map_id` used. That ID is a geometry token, not a GPU frame ID, and
  `capture_after` has its own mapping. To reuse an older screenshot's geometry on purpose, pass its `mapping` and check
  that the UI still matches first.
- **Capture coordinates go stale on a resize or retarget.** vinput refuses `capture_space:true` clicks then (the map
  belongs to one window and one client size). Either way, take a fresh single-frame `capture` and read the coordinates
  again.
- **Choose where files go.** `save_path` (exact file) or `out_dir` (unique name in a folder) on `capture` and the action
  tools; give only one destination.
- `status` is compact by default. Read `warnings` first; `verbose:true` gives the full reply with every counter. When a
  game ignores input, the non-zero counters show which input APIs it actually reads.

## 3. Capture after actions, and waiting for the screen to settle

- **Act and look in one call.** `capture_after:true` on `click`, `key`, `click_text`, `move`, `move_relative`, `drag`,
  `scroll`, `pad` and the helper tools returns the frame after the action (it waits 3 presented frames). A failed action
  takes no picture. `capture_after:false` disables the image.
- **`capture_after` numbers are FRAME COUNTS.** `capture_after:0` captures immediately; `capture_after:300` asks for 300
  future presented frames. For a timer delay use `capture_settle_ms:300` with `capture_after:0`.
- **Wait for pixels to settle before the image.** `capture_after:0` plus
  `capture_stable:{region:<area>, timeout_ms:4000, max_change_pct:1, pixel_tolerance:8}` watches that area until it is
  stable, then captures. On `scroll` the same object is `settle`. The timeout is advisory, and the final image must
  come from the same window scope as the observation. Exclude animated HUD and background pixels. Async or indefinite
  holds need their own job observation; `key_sequence` needs `wait:true`.
- **After a click that opens something**, `observe_settle {region:<popup>, poll_ms:300, stable_samples:5,
  timeout_ms:8000, capture_after:0, max_width:640}` observes a longer quiet window and returns one image. Use
  `wait_text` for the expected next state when you can. A stale or cached image can also be quiet: do not re-click an
  item that may already be claimed just because the first capture looks old.
- **Repeated identical PrintWindow images** may mean static UI, cached output or a stalled renderer.
  `capture_observation` reports identical captures and the unchanged time, with `rendered_frame_age_ms:null`.
  `method:"auto"` already tries an observed native renderer first. A Chromium GPU child window is not automatically an
  input target or a supported native capture route.
- **Watch something happen in one image.** `capture_burst frames:8 interval_ms:250` returns a contact sheet of tiles in
  time order (animations, loading, whether a held key moves the character). Keep sheets readable:
  `capture_burst {frames:4, columns:2, max_width:640, region:<panel>, interval_ms:500}`; `max_width` is per tile, and a
  few cropped tiles beat many full-client frames. The saved original keeps its native tile geometry; the attached
  preview can be smaller. Do not click coordinates read off a sheet: take a fresh single-frame capture.
- **Timed choice wheels / prompts:** observe near the expected moment with a bounded `wait_change` or a short
  `capture_burst`, then take a fresh single-frame capture before reading coordinates.

## 4. Menus and UI clicks

- **Click the button graphic, not the label beside it.** Many old menus (Fallout 1/2, others) only react to the round or
  square button drawn next to the text; clicking the label does nothing and looks like a dead mouse. `click_text` aims at
  the text, so for those menus use `capture grid` and click the graphic, or `click_image` on the button.
- **Hover first, and hold the press.** Games that poll the mouse once per frame or tick miss short clicks. `click`
  hovers 80 ms over the target (`hover_ms`) and holds the button 140 ms (`hold_ms`) by default. Measured: under about
  200 ms is often ignored in The Sims; Fallout 1 menus needed about 300 ms (`hold_ms:300`); placing furniture in The Sims
  needed about 900 ms (`hold_ms:900`). Raise `hover_ms` too if hover-gated UI (highlight, tooltip) must appear first.
- **DirectInput mouse games.** vinput also waits until the game has read the press and the release; the reply's
  `evidence` (`di_poll_confirmed`, `di_release_confirmed`) says what was observed, and `di_poll_timeout_ms` /
  `di_release_timeout_ms` make those reads required.
- **Engine presets.** On a Chromium window (`Chrome_WidgetWin_0/1`, Electron games) vinput uses legacy window-message
  input for UI and, for timings you omit, a 350 ms hover and a 400 ms hold. On a Unity window (`UnityWndClass`) omitted
  click hover and hold become 250 ms, and `drag` uses 800 ms move / 400 ms pickup / 500 ms drop
  (`timing_profile:"unity"` asks for that preset, `"default"` turns class selection off). Explicit timings and input
  modes override all presets. A hold-to-skip control can need a longer explicit `hold_ms`, for example 2000.
- **Text clicks.** `click_text` matches a complete OCR line by default (`PLAY` does not hit `PLAYER` or a paragraph).
  Several matches = refusal with candidates: pass `nth` or a `region`. For a label that slides in, use
  `click_text {text:"PLAY", stable_ms:300, timeout_ms:3000}`: it waits until the same OCR box stayed put across captures,
  then clicks once. For a short label OCR keeps misreading, look at `ocr` output first, then use `fuzzy:1` on purpose.
- **UI transitions can swallow clicks.** Use `observe_settle` on the relevant region before a deliberate click, then
  `click verify_change` or `wait_text` to check the result. Never resend a delivered click blindly: automatic repeated
  clicking can double-toggle a control.
- **Dropdown lists:** click the dropdown, then the item. If an item will not take a click, `scroll` one notch over the
  CLOSED dropdown to step its value, then press the dialog's Apply.
- **Text fields and chords.** `key ctrl+a`, `ctrl+z`, `shift+a` and other chords reach the game with their modifier (the
  character message is built from vinput's own key state). To replace a field's content: click it, `key ctrl+a`, then
  `type_text`. Wait for a text prompt to appear before typing, and check the field with a zoomed `capture` before you
  confirm.

## 5. Many clicks, drags and placement

- **Several clicks in one call.** `click_many` takes `{x,y}` objects or `[x,y]` pairs (`click_grid` for a regular grid).
  `completed_points` counts delivered clicks; placement in the game stays unknown and `game_accepted_points` stays
  null. Never resend a delivered point just because the following capture failed. `capture_after` takes one image after
  the whole batch.
- **Stop on no effect.** `verify_change:true` with a small `verify_region` stops after the first unchanged point
  (`unchanged_limit:3` allows more; `verify_policy:"report_only"` records and continues). Changed pixels alone do not
  prove placement. Compact replies keep each point's `changed` / `reason`, `stop_reason` and the zero-based
  `next_index`. A point at `uncertain_index` was attempted: do not replay it.
- **Read tooltips before placing.** `click_many verify:"tooltip"` with explicit `reject_texts` such as
  `["Cannot build","Out of city limits"]` hovers and OCRs before each click: matching or empty OCR skips that point,
  failed or unknown OCR stops. `hover_many` (with `ocr:true`) inspects several tooltips without clicking; both default
  to a 400x200 probe box around each point, clamped to the client, and `probe_region` overrides it.
- **Drag-and-drop** (inventory, furniture, map items): `drag` presses, holds `hold_start_ms` (150), moves in `steps`
  over `ms`, dwells `hold_end_ms` (250) at the destination and then releases. A plain press-move-release is dropped by
  many games; raise `hold_end_ms` to 500-900 for placement. `measure_region:<area>` adds a before / after pixel
  difference. `drag placement:true` runs the whole drag inside the game and waits for the game to read each step (games
  that read `GetCursorPos` and DirectInput buttons); with an older DLL, or a game that reads buttons through window
  messages (Unity: `button_down_not_observed`), it falls back to the normal sequence and says so
  (`placement_fallback`). In Unity games plain `drag` is the right tool. A failed placement drag is not a reason to send
  a second one: check the screen first.
- Use `key_down lbutton`, `move`, `capture`, `key_up lbutton` only when you need to look mid-drag.
- **Edge scrolling.** Many games pan the view while the cursor rests at the window edge (The Sims, strategy games), so
  items land in the wrong place. `click`, `move`, `drag` and `scroll` warn when a position is within `edge_margin` px
  (default 8) of the edge. Aim further in, or add `park_cursor:true` so the cursor ends in the middle. A current DLL also
  keeps injected cursor paths that many px inside the edge itself (`config edge_margin:N`, 0 = off); replies then carry
  `edge_contact` / `edge_guard_applied`, and `status` warns while the cursor rests on the edge.
- **Zoom with the wheel** may centre on the virtual cursor, so choose `scroll` `x`/`y` deliberately.

## 6. Keyboard input, holds and sequences

- **Hold in the background, keep looking.** `key hold_ms:2000 async:true` (and `pad ms:... async:true`) returns at once
  with a `job_id`; the hold runs inside the game with its own deadline while `capture` and `status` keep working.
  `job_status` (pass `id` or `job_id`; neither lists all jobs) follows it, `job_cancel` stops it early and releases only
  what that job held. Accepted means scheduled, not delivered.
- **Hold until something shows up.** `hold_while key:"w" until_text:"Village" timeout_ms:5000` holds and watches the
  screen in one call, and always releases when it returns. `hold_until_change` does the same for a pixel change in a
  region.
- **A fixed key routine is one job, not many calls.** `key_sequence steps:[{action:"hold",key:"w+space",ms:1500},
  {action:"wait",ms:100},{action:"key",key:"d",ms:300}]` returns a `job_id`; `job_status` shows the current step and
  `job_cancel` releases the active chord. Pressing a key a later step uses cancels the rest.
- **Menu steps in one call:** `key_sequence {steps:[{action:"key",key:"down",ms:80},{action:"wait",ms:100},
  {action:"key",key:"enter",ms:80}], wait:true, capture_settle_ms:150, capture_after:0,
  region:{x:100,y:40,w:320,h:180}, max_width:320}`. Adjust keys and region to the menu you see. `wait:true` waits for
  this job's completion before the settle delay and the one image; it does not prove the menu consumed the keys.
  Without `wait:true` the image can come before the keys.
- **`wait:true` belongs on the original call.** An async reply's `next_action` follows that exact job with
  `job_status`. Never resubmit the sequence to wait for it; cancel only its `job_id` to stop it.
- **Refresh a modifier while moving:** `steps:[{action:"retap", key:"w+shift", retap_key:"shift", ms:1500,
  every_ms:300, gap_ms:10}], wait:true`. W stays held and Shift gets fresh edges. `retap_key` must be one of the held
  keys (for Space sprint edges: `key:"w+space", retap_key:"space"`); do not add an unheld key through it. Choose a gap the
  game can poll and check the result.
- **Avoid Ctrl or other modifier chords for movement** unless the game binds them: Ctrl can toggle crouch instead of
  holding it.
- **Timed chases:** `key_sequence` with `wait:true`, `abort_on:{kind:"text", text:"MISSION FAILED",
  region:{x:0,y:0,w:800,h:180}, poll_ms:100}`, `capture_every_ms:500`, `capture_max_frames:16`, `capture_max_width:640`
  and a `capture_out_dir`. Review `sequence_captures.frames` and its row-major sheet; each frame has job / QPC / scope
  receipts. Use an image template or a region-change condition when OCR is unreliable. Observer latency is bounded but
  can miss a short-lived screen. Frames are observations, not actions.
- **Saving proof of a result screen:** `wait_text {text:["VICTORY","DEFEAT"], save_match:true, out_dir:<folder>}` saves
  the exact OCR frame that matched, with its original QPC (`save_match_path` for an exact file). A failed save keeps the
  wait result and does not recapture.

## 7. Mouse look, movement and navigation in 3D games

- **3D games often read raw input:** walk with `key` + `hold_ms`, look with `move_relative`; check `status` counters if
  nothing happens. In `move_relative` replies `applied_dx/dy` describe cursor coordinates and `raw_dx/dy` the requested
  legacy values; `device_dx/dy` and `relative_delivery` show what was submitted on each channel. Consumption by the game
  is still unconfirmed.
- **Save an angular reference once.** `look_calibrate {mode:"counts", yaw_counts:300, observed_yaw_deg:45,
  game:"my-current-settings"}`, using your own compass or known-turn measurement, or supply a signed
  `counts_per_degree` and an optional `pitch_counts_per_degree`. Then `turn {degrees:60}` and `look {yaw:30, pitch:10}`
  reuse it, across MCP restarts, for that exact process / window / label. Recalibrate after sensitivity or FOV changes.
  Collision and smoothing can make the actual angle differ; it is never measured. `look_at` closes the loop on a unique
  image landmark using the separate visual calibration (`look_calibrate` default mode).
- **Approach a visible target:** capture, turn toward a world landmark, then make short 300-800 ms moves and capture
  again. `move_until_stuck {key:"w", region:<small world area>, timeout_ms:2000, still_ms:500}` stops the exact hold
  when those pixels stay quiet. `stopped:"no_motion"` does not tell a wall from a pause or ignored input. Avoid animated
  HUD or hand regions and look at the final image.
- **On a pad:** `pad_move_until_stuck {direction:"up", region:<world area>, timeout_ms:3000, still_ms:500,
  capture_after:0}` does the same for one finite stick hold. It measures neither yaw nor progress toward a waypoint.
- **Stuck on geometry:** after checking that S / Space / W are your bindings, `unstick {degrees:60, capture_after:0}`
  tries one bounded back / turn / jump / forward recipe using the saved counts. Look at the image before another try.
- **Pad movement:** `pad_move {direction:"up-left", ms:600, speed:"walk", capture_settle_ms:150}` sends one finite
  move. Directions are screen intent; a fixed camera can invert them (`screen_rotation_deg:180`). Recheck after every
  camera cut. `pad_look` needs a caller-measured calibration (`rx`, `degrees_per_second`); calibrate it separately for
  normal, aim and other camera modes.
- Each decision costs a round trip (a capture-view-decide loop took 5 s median on a real run, about 75 m of travel in a
  car). Prefer slow, safe moves in real-time games, and use one-call tools to cut the loop.

## 8. Real-time games, steering and driving

- Keyboard steering is all-or-nothing: a 400 ms tap of `d` at speed turned a car about 90 degrees. Use `pulse` (key PWM:
  `duty` percent of each `period_ms`) for gentler digital steering, or `pad_ramp` / `pad_timeline` / `pad_drive` for
  proportional stick input on games that read XInput. Without those, use taps of 100-150 ms and look in between.
- **Put multi-phase manoeuvres in one job** (`pad_drive phases`, `pad_timeline`): separate calls leave a neutral gap
  between them.
- **Menus that need the stick first, then the button:** `pad_timeline` states are complete snapshots and take `buttons`
  and a `phase` label; `stagger_ms:60` makes a new button / trigger press wait until the stick change has been published
  (and, with frame pacing, a later frame). `lt` / `rt` also work inside `buttons` (`"a+lt"`). The receipt says what
  vinput published, not what the game did.
- `ok:true` with no change on screen after 10-15 varied attempts means the game is not reading that input channel.
  Switch channel (pad / keyboard) or stop; do not keep retrying. In a vehicle that stays stuck for about 10 calls, get
  out or restart instead of grinding.

## 9. Game notes

Generic and factual; check each against your build of the game.

| game / engine | note |
|---|---|
| Fallout 1 / 2 (with sfall `ddraw.ini`) | Set `BackgroundMouse=1` and `BackgroundKeyboard=1`, or a game started in the background dies with "Failure initializing input devices". Menu buttons are the round graphics left of the labels. Menu clicks need hover first and a ~300 ms press. A fixed window size (for example 640x480 with 2x scaling = 1280x960) keeps coordinates stable. Captures read through PrintWindow. |
| GTA Trilogy Definitive Edition (Unreal Engine 4) | Saved exclusive fullscreen deadlocks the game under vinput (the fullscreen request is refused every frame and nothing is ever presented). Set windowed in its INI first. Mouse look through `move_relative` did not work in the field test (see [MCP_TODO.md](MCP_TODO.md)); steer with keys or `pad`. |
| The Sims (2000) | Launch it windowed with `-w -r800x600` (or the resolution you want). Under the DDrawCompat wrapper, `FullscreenMode=borderless` still covers the whole monitor with the wrapper's presentation window and the user cannot Alt-Tab out; `windowed` is not a valid `FullscreenMode` value in that build. Clicks shorter than about 200 ms are often ignored and furniture placement needed a press held about 900 ms (`hold_ms:900`). The view scrolls when the cursor is at the window edge. |
| DirectDraw / Direct3D-wrapper games (DDrawCompat, dgVoodoo, DXVK and similar) | The picture can be in the wrapper's own presentation window, not the game's original one. vinput's PrintWindow path also reads a visible same-process `...PresentationWindow`. If a capture is still black, look at `capture_diagnostics` (other windows of the same process); the CLI's `config hwnd=0x...` selects another window (the MCP `config` tool does not expose `hwnd`). |
| Games with a saved fullscreen setting | Command-line switches are often overridden by the saved setting; edit the INI / registry setting the game itself uses. |
| Grand Theft Auto IV: The Complete Edition (RAGE, D3D9) | Without launch args the game window can stay invisible (audio plays, `present` 0, black captures). Launch with `args:"-windowed -width 1600 -height 900"` and `early:true`. Capture comes through PrintWindow. Driving on `pad`: `rt` 170-200 for 3000 ms covers about 25 m; `lx` +-32000 at `rt` 150-170 for about 2 s turns 90-120 degrees; small `lx` values barely steer; `lt` 255 reverses. Brake before turns and use short low-throttle steps near the marker. If the car is stuck for more than about 10 calls, get out (`y`) or restart the mission. Clicking into the window lets the game capture the real mouse. |
| Hollow Knight (Unity) | Keyboard works in the background; the virtual controller was ignored in the field test. Arrows move, Z jump, X attack, A focus. Menu keys need `hold_ms` about 250 (80-100 ms presses are often dropped). Walk-jump: one async `right` hold, then `z` about 250 ms later; the chord `right+z` does neither. Use `capture_burst` (6 frames, 120-200 ms, jpeg) to see jumps and hits; a single capture right after the action can miss them. |
| Terraria (XNA, D3D9) | Mouse, long click-holds, keys and `type` work in the background. Capture comes through PrintWindow and can be one frame stale. Wait for a text prompt to appear before `type`. Hold left-click (`ms` 15000+) on a tree trunk to chop; the reach is about 5 tiles and a miss still returns `ok:true`. |
| Detroit: Become Human (Vulkan) | Needs `launch early:true` for capture. Use `attach {arm_pad:true}` so the first pad press counts. `pad_move` with `screen_rotation_deg:180` when the fixed camera faces the character; re-check after every camera cut. `pad_rotate` handles "rotate the stick" prompts (read the start angle and direction from the glyph). Hold A about 1500 ms for "hold" prompts. |
| Fallout Shelter (Unity) | Capture and OCR work, but the game ignored all background input in the field test (cause unknown). |
| Cities: Skylines (Unity) | Run it windowed (Options > Graphics > Display Mode > Windowed, then Apply): it rewrites its saved display mode on start and ignores `-screen-fullscreen 0 -screen-width/-screen-height`. Roads are click-click, not a drag: click the start, each further point, then the last point again (or press Escape) to finish. Plain `drag` works for zoning brushes and middle-button camera tilt. Right-click cancels the active build tool (Escape did not in one run). Snap a pipe or power-line end onto the building or network and check the Info view: an unconnected end looks fine in 3D and does nothing. In one fullscreen run a dropdown item click closed the list without selecting; use the scroll fallback above. |
| Electron / Chromium games | Launch with Chromium's anti-throttling switches (see the root README), or rendering slows down in the background. The Chromium click preset applies (section 4). |

### City-builder opening checklist

A human player reached about 90 residents within a few in-game days, even with a negative budget, because they used the
information the game provides. An agent that guessed from the 3D view played badly. Do it in this order:

1. Connect the town to the highway with an interchange or on-ramp: one road in and one road out.
2. Run power lines around the zoned area. A power plant only serves the buildings it is connected to.
3. Power the water pump and the sewage drain as well.
4. Lay water pipes under every zoned street.
5. Zone along the roads.

Then open the Info views panel (electricity consumption against production, water and sewage capacity against use) and
the Economy > Budget panel after every build step, and fix whatever is red before you build more. The general lesson for
any game: read its own info and overlay panels before acting and after each step, instead of guessing from the picture.
`capture` plus `wait_text` on those panels is cheaper than a wrong build.

## 10. OCR

- **OCR misses** small or stylised text. Empty raw OCR automatically retries bounded upscale / contrast passes on the
  same captured image; nonempty-but-wrong results are not retried. Use `region` + `scale:2`, or cut an icon from a
  capture and use `click_image`.
- `ocr text_only:true` returns just the text when you do not need boxes. Word boxes are off unless `words:true`.
- **Numbers:** `ocr {digits_only:true, region:<at most 512x512>}` returns `number_read.value` as a literal string only
  after two agreeing bounded passes with no conflict or incomplete analysis; otherwise it is null. Small single digits
  (a pinned Risk counter) still came back unknown in testing. Look at the image instead of guessing values or
  substituting letters.
- `wait_text` accepts a list of alternatives (`text:["VICTORY","DEFEAT"]`) and `context_region` to read a second area of
  the same matched frame.

## 11. Reply size

- `compact:true` on input and capture tools, or `VINPUT_MCP_COMPACT=1` on the server, returns job state, reader summary,
  warnings and image paths only. `compact:false` restores the full receipt. Errors are never compacted.
- Big captures cost tokens: capture a `region`, or set `max_width` (for example 640 or 960), when you do not need the
  full frame. Action images default to at most 960 px wide.
- If a wrapper or harness around the MCP truncates replies, read the full output or use `compact:true`: important
  fields such as the saved image path can be cut off.

## 12. When something does not work

1. `status`: `warnings`, `app_active`, `present` (0 = no Direct3D / OpenGL / Vulkan frame yet).
2. `log` (add `contains` to filter): hooks that failed, refused fullscreen, errors from the injected DLL.
3. `trace key:<key>` after pressing the key: shows through which channels (messages, `GetAsyncKeyState`, DirectInput,
   raw input) the key was delivered and which the game actually read.
4. `config`: change how vinput treats the game (`raw:"post"` / `"buffer"` for mouse-look that never arrives,
   `cursor:"real"`, `msgs:"on"`, `nofullscreen:false`); it returns the config now in effect.
5. If the game exited, every tool answers `target exited (pid N, exit code X)`.
6. Black captures with `present` 0 and audio playing: the window may be invisible or exclusive-fullscreen. Relaunch
   windowed (launch `args`) or set windowed in the game's own settings.
7. A retarget can be refused while input is held: `release`, wait for the game's queue to progress (`status`), retry.
8. `wm_mouse_unrecognized_dropped` counts isolated mouse messages without owned provenance, including startup and window
   traffic before any agent action; its source is unknown. Judge an agent action from `owned_delivered` /
   `owned_dropped` and the image, not from that aggregate.

## 13. When the user is at the keyboard

vinput keeps the user's real mouse and keyboard out of a background game. When the user activates the game window
themselves, their real input passes through and the cursor the game reads is the real one until your next input
command; `status` shows `owner_active` (with a warning) and `cursor_effective`. Treat `owner_active:true` as "the user
is playing right now": look before you act, and do not fight them for the mouse.
