# llm-game-automation-mcp - MCP server

> **Single-player / offline games only.** It injects a DLL into the game. Never point it at online / multiplayer games
> or games with anti-cheat. See [../DISCLAIMER.md](../DISCLAIMER.md).

A [Model Context Protocol](https://modelcontextprotocol.io) server (stdio transport, official
`@modelcontextprotocol/sdk`) that lets any LLM agent play Windows games through vinput: input and screenshots go to
one game process **in the background**, so the game never needs focus and you keep your mouse and keyboard.

The server has **62** tools (counted from `tools/list` of the current build). Apart from the host-timer `wait` tool, tools run
`vinput.exe` (once or several times) and return **vinput's own JSON reply** as text. `ok` is surfaced as the MCP `isError`
flag: `ok:false` (or a crash, timeout or missing binary) always comes back as `isError: true` with the error text,
never silently. `capture` also returns the PNG as MCP image content.

## Build

Requires Windows 10/11 x64, Node.js 20+, and the vinput binaries (`build.bat` in the repository root).

```bat
cd mcp
npm ci
npm run build
npm test          &rem handshake (initialize + tools/list, checks every schema), then every test\t_*.mjs against a fake vinput
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
| `VINPUT_MCP_COMPACT` | `1` / `true` / `yes` / `on` makes every tool return compact replies unless a call passes `compact:false` (see [Compact replies](#compact-replies)) |
| `VINPUT_CAMERA_PROFILE_DIR` | where `look_calibrate mode:"counts"` saves angular calibrations (default `<dir of vinput.exe>\run\camera_profiles`) |

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
| `launch` | start a game (`exe` / `aumid` / `steam_appid`+`wait_exe`, `args` (a size in them is compared with the real window: `warnings` + `requested_size_ignored`), `cwd`, `cpu_affinity` (hex mask such as `"0xF"`, direct `exe` only), `early`, `settle_s` (default 5), `timeout_s` (default 240), `lock`, `claim`, `claim_ttl_s`, `arm_pad`, `pad_wait_ms`, `show_hidden`, `verbose`) and inject; a direct `exe` launch refuses when that exact executable is already running and returns `existing_instances` (nothing is attached); `new_instance:true` permits another copy; `follow_pid` (+ `follow_created` from the launcher receipt) continues an existing launch after you clicked Play in a launcher, without starting another process; returns `pid`, plus `warnings` / `nothing_presented` / `fullscreen_refusals` when no frame is being presented |
| `attach` | inject into a running game by `pid`; vinput waits for its hooks, window and message queue (`readiness`; `application_ready` is always `null`) and the tool waits until the window is adopted |
| `claim` | claim or renew the current MCP owner without takeover; `claim_ttl_s` is native inactivity time in seconds (1..86400, default 3600) |
| `list_targets` | games that have vinput loaded (pid, exe, owner, uptime, really_foreground) |
| `doctor` | read-only native CLI process/architecture, connection, channel, capture/foreground/claim diagnostics and suggestions; optional `key` adds a trace. No injection, input or automatic repair; counters do not prove the game accepted an action |
| `status` | compact by default: pid, hwnd, client size, `really_foreground`, `cursor_effective` / `owner_active` (the user is using the window), `app_active`, `present`, cursor / raw / msgs modes, non-zero counters, `capabilities` (what the game's vinput DLL supports), `edge_contact`, `fullscreen` policy, `warnings[]`, and a valid native `virtual_down` VK array (including `[]`); `verbose:true` = the full vinput status |
| `config` | change / read how vinput treats a game: `fakefocus`, `autoactivate`, `isolate`, `blockfocus`, `nofullscreen`, `keepvisible`, `cursor`, `raw`, `msgs`, `edge_margin` (vinput's edge guard), `fullscreen` (`borderless` / `refuse`), `for_ms` (only with `isolate:false`: pass the user's physical input for 1..120000 ms, then vinput restores isolation inside the game); returns the effective config. `hwnd` is CLI-only |
| `log` | tail of vinput's log for a game (`lines`, `contains`, `source` ring/file); repeated lines collapse |
| `trace` | per-key delivery trace (`trace key <key>`): which input channels the game read |
| `capture` | the game's own frame as an image + `{path, w, h, method, capture_path, frame}`; `region`, `after_frames`, `method`, `include_image`, `grid` (labelled client-coordinate grid on the returned image), `max_width` (64..8192) / `scale` (0.05..64) / `zoom` (1..64) resize the returned image (`zoom` may be combined with `max_width` as a cap; `upscale:true` lets `max_width` enlarge a PNG crop; `grid` / `zoom` need png; reply has `image {scale, offset_x, offset_y, scaled_by}`), `format` `png` / `jpeg` + `quality` (default 85); destination `path` / `save_path` (exact file) or `out_dir` (unique name in that folder), give only one; vinput shrinks / encodes inside the game when its DLL supports it, the server does it otherwise |
| `capture_burst` | one contact-sheet image of `frames` tiles (1..16, default 8) taken `interval_ms` apart (default 250, frames x interval at most 30 s); `columns`, `max_width` per tile (default 320) / `scale`, `format`, `region`, `path` / `save_path` / `out_dir`; `frames[]` has each tile's time and rectangle |
| `ocr` | text and lines with boxes; bounded same-capture retry on empty results (`region`, `scale`, `lang`, `words` (word boxes, off by default), `tiles` (default true), `preprocess`, `retry_empty`, `text_only`); `digits_only:true` reads one ASCII digit token (at most six digits) in one explicit `region` up to 512x512 and returns it in `number_read.value` only when two passes agree (otherwise null) |
| `find_text` | locate text without clicking (box, centre, match_count); `timeout_ms` default 0 |
| `click_text` | find complete OCR lines by default and click their centre (`exact_line`, `whole_word`, `nth`, `exact`, `fuzzy`, `timeout_ms` (default 5000), `button`, `region`, `optional`); `stable_ms` (100..3000) waits until the same OCR box has stayed put across captures for that long before one click (for sliding labels); accepts action-image options |
| `wait_text` | wait until text appears (or `gone`); `text` is one string or a list of 1..16 alternatives (any-of; `gone` waits until all are absent; `matched_text` says which matched), `region`, `context_region` (OCR a second rectangle of the same matched frame), `exact_line`, `timeout_ms` (default 15000), `optional`; `save_match:true` saves the exact OCR frame that matched (`save_match_path` for an exact file or `out_dir`); a failed save does not change the wait result |
| `find_image` / `click_image` / `wait_image` | template matching with a PNG (`threshold`, `scales`, `region`, `nth`) |
| `pixel` | colour of one pixel |
| `wait_change` | wait until the picture (or a region) changes; `timeout_s` default 10 |
| `click` | click at `x`,`y` (client pixels, or `norm` 0..1, `capture_space`, or `coord_space:"capture"` for the image you were shown; `capture_space:true` uses vinput's own map, which is bound to one window and client size and is refused after a resize or retarget; `coord_space:"capture"` uses the last capture's mapping (or an explicit `mapping`) and the reply reports it in `mapping_used`), `button`, `count`, `hover_ms` (default 80), `hold_ms` (default 140; on a Chromium window an omitted hover becomes 350 and an omitted hold 400, see below), `di_poll_timeout_ms` / `di_release_timeout_ms` (require the game's DirectInput read), `verify_change {region, timeout_ms, poll_ms}`, `modifiers` (`ctrl` / `shift` / `alt`, with `modifier_hold_ms`, default 60000), `edge_margin`, `park_cursor`; reply `evidence` |
| `move` | move the virtual cursor (hover); `steps`, `norm`, `capture_space`, `coord_space` / `mapping` |
| `move_relative` | relative mouse motion for mouse-look (`dx`, `dy`, `steps`, `raw`) |
| `drag` | press, hold (`hold_start_ms`, default 150), stepped move (`ms` default 300, `steps`), dwell at the destination (`hold_end_ms`, default 250), release; always releases. `timing_profile` `auto` (default: a Unity window, `UnityWndClass`, gets 800 / 400 / 500 ms for move / pickup / drop), `default` or `unity`; explicit timings override it. `measure_region` adds a before / after pixel difference. `modifiers`, `coord_space` / `mapping`. `placement:true` = vinput's owned polled placement drag (`poll_timeout_ms`), falls back to the normal sequence on an older DLL or when the game never reads the press, as Unity-style games that read buttons through window messages (`placement_fallback`) |
| `scroll` | wheel notches at the cursor or at `x`,`y`; give exactly one of `notches` / `delta` / `amount` / `clicks` (-100..100, positive = up); `horizontal`, `modifiers`; `settle` (`true` or `{region, timeout_ms, ...}`, default budget 5000 ms) watches pixels settle after the wheel, before `capture_after` |
| `key` | press a key or chord (`hold_ms`, `count`, `gap_ms`); `async:true` (with `hold_ms`) returns a `job_id` at once, `replace`; `measure_region` reports an RGB difference before / after a completed timed key (not with `async`) |
| `key_down` / `key_up` | hold / release a key (or mouse button name) |
| `type_text` / `type` | type Unicode text (`as_keys` for games that read raw keys, `delay_ms`); `type` is an alias |
| `pad` | XInput pad 0: `buttons` (`lt` / `rt` also work as button names), sticks `lx ly rx ry` -32768..32767, triggers `lt rt` 0..255, `ms`, `hold`, `off`; `async:true` (with `ms`) returns a `job_id`; `capture_after` plus `capture_settle_ms` (0..5000) observes after the CLI reply; pad-family images accept capture `max_width` / `scale` / client `region` / `format` / `quality` |
| `pad_move` | finite named left-stick direction, `ms`, `speed` walk20000/run32767, normalized diagonals, explicit `screen_rotation_deg` compensation; opt-in `smooth`, `async`, `replace`; screen intent requires game calibration |
| `pad_look` | predicted degree turn using required caller angular calibration (`rx`, `degrees_per_second`); signed `degrees`, bounded finite duration, other controls neutral; capture defaults0/100ms settle, `async`, `replace`; angle remains unverified |
| `pad_rotate` | one stick circle/arc for prompts: `stick`, `start_deg`, `direction`, `turns`, rotation `ms`, optional `radius`, final `end_dwell_ms`; one bounded timeline, finite neutral cleanup, capture defaults0/100ms settle, `async`, `replace` |
| `pad_ramp` | ramp one pad axis `from` -> `to` over `ms` with the other axes / `buttons` held, `end_hold_ms`, `pacing` frames / time, `async` |
| `pad_timeline` | timed list of pad states (`at_ms`, `buttons` such as `a+lt`, `phase`, sticks, triggers), `strict_frames`, `pacing`, `stagger_ms` (new presses wait for the stick change before them), `async` |
| `pad_drive` | one timed XInput job for throttle (RT), steering (LX) and brake (LT): `ms` plus `throttle` / `steer` / `brake`, or up to 16 `phases`; `brake_after`, `async`, `replace` (see [Driving in one job](#driving-in-one-job)) |
| `key_sequence` | a validated keyboard sequence (`steps` of `hold` / `key` / `wait` / `retap`, or an absolute `schedule`) as one background job; returns `job_id`; `replace`; `wait:true` (+ `wait_timeout_ms`) waits for that exact job; with `wait:true` also `abort_on`, `progress_check`, `capture_every_ms` recordings; `start_on`, `max_lateness_ms` (see [Waiting for one keyboard sequence](#waiting-for-one-keyboard-sequence)) |
| `pulse` | key PWM: `key` down `duty` percent of each `period_ms` (default 100) for `ms`, `async` |
| `hold_while` | hold a key (or pad buttons / sticks) until `until_text` appears or `timeout_ms` (default 5000); always releases |
| `hold_until_change` | one bounded native key/chord job until scoped region pixels change or timeout (`timeout_ms` default 5000, `baseline_ms` default 250, `min_change_pct`, `pixel_tolerance`); exact-job cancellation and explicit uncertain admission/release receipts |
| `pad_move_until_stuck` | the same for one left-stick `direction` (`speed` walk / run, `screen_rotation_deg`) with the same stop rules and defaults |
| `move_until_stuck` | hold one movement key/chord (`timeout_ms` default 3000) and stop its job when a selected `region` stays quiet (`still_ms` default 500, `grace_ms` 250, `max_change_pct` 1, `pixel_tolerance` 8); `stopped:"no_motion"` measures pixels, not position |
| `observe_settle` | read-only scoped pixel-hash stability observation (`stable_samples` default 3, `timeout_ms` default 5000, `poll_ms` default 100, optional `max_change_pct`); camera angles and raw input draining remain unmeasured |
| `look_calibrate` | default `mode:"visual"`: two small scope-fenced raw probes (`counts` default 80) with unique landmark (`template`) reidentification; returns a local signed screen-pixel/count matrix for `look_at`. `mode:"counts"`: save a signed `counts_per_degree` (+ optional `pitch_counts_per_degree`), or derive it from `yaw_counts` + `observed_yaw_deg`, for `turn` / `look` / `unstick`; sends no input; optional `game` label |
| `look_at` | center a reidentified unique landmark using a same-scope calibration, bounded corrections (`max_steps` default 8, `max_counts_per_step` default 150, `tolerance_px` default 6), settle checks and no-progress stops |
| `turn` | one bounded yaw move of `degrees` (-720..720) using the saved `mode:"counts"` calibration; `game`, `timeout_ms` (default 5000) |
| `look` | `yaw` (default 0) and optional `pitch` together from the saved calibration; refuses before moving when pitch was not calibrated |
| `look_turn` | one yaw move from a caller-supplied `counts_per_degree` and `yaw_deg`; needs no saved calibration or landmark |
| `unstick` | one recipe with the default keys: `s` for `backup_ms` (default 250), a saved-calibration yaw of `degrees` (45..90 either sign, default 60), `space` (unless `jump:false`), then `w` for `retry_ms` (default 350); stops on the first uncertain step, never repeats |
| `click_many` | up to 256 `points` (`{x,y}` or `[x,y]`) clicked in one bounded session (`timeout_ms` default 30000, `interval_ms`, `hold_ms`, `hover_ms`, `button`, `coord_space` / `mapping`); optional `verify_change` + `verify_region` with `verify_policy` (`stop` after `unchanged_limit` unchanged clicks, default 1, or `report_only`); `verify:"tooltip"` + `reject_texts` hovers and OCRs before each click and skips matching or empty points (`tooltip_hover_ms` default 400, `probe_region`); every receipt returned, nothing replayed; `next_index` is the zero-based next unprocessed point |
| `click_grid` | the same for a row-major grid: `x0`, `y0`, `dx`, `dy`, `cols`, `rows` (at most 256 points) |
| `hover_many` | hover up to 64 `points` once each (`hover_ms` default 500); per-point hash of `probe_region` (default a 400x200 box around each point, clamped to the client) and optional tooltip `ocr`; no clicks |
| `click_delta_cursor` | for games that draw their own cursor: find the cursor sprite (`cursor_template`, `hotspot_dx` / `hotspot_dy`), move it toward `x`,`y` with raw deltas (`counts_per_pixel_x` / `_y` required, `max_steps` default 12, `max_counts_per_step` default 200, `tolerance_px` default 3), then click once |
| `click_to_move` | move to `x`,`y`, one bounded button hold (`hold_ms` default 5000, `button` left / right), then wait for `region` change and stability (`timeout_ms` default 10000); returns `displacement:"UNKNOWN"` |
| `job_status` / `job_cancel` | follow / stop a background input job, including a `key_sequence`; `job_status` takes `id` or its alias `job_id` (neither: list all jobs) |
| `list_keys` | all key names |
| `wait` | nonblocking MCP host timer, integer `ms` 0..30000; no target, CLI, input, claim renewal or frame wait; returns requested and actual elapsed ms |
| `run_script` | run a `.vin` macro file (`continue_on_error`, `timeout_s` 1..3600) |
| `release` | release all held input |
| `detach` | release input and drop this server's claim (vinput stays loaded, inert, until the game exits) |

`wait {ms:1000}` is a host-side pause, not a screen/state postcondition. Its 30-second per-call bound stays below the SDK test client's 60-second tool timeout; scheduling or an outer harness can still delay the reply. It runs no vinput process and does not block other MCP requests. Prefer wait_text/wait_change when waiting for an observable game state; a timer neither proves progress nor renews a claim.

Every tool that targets a game takes `pid`. Coordinates are the game's **client pixels**, which equal the pixels of a
full capture. Refusals by the anti-cheat check come back as `isError` with `anticheat_refused: true`; there is no way to
override them from the MCP server.

For a successful top-level completed job outcome (`job_state:"completed"`, nonempty `job_id`, native `accepted:false`), MCP omits the misleading `accepted` field and uses the existing job state and completion evidence. It adds no boolean about whether the original request was queued; it does not identify the original request's async setting, prove callback delivery, or prove a game reaction. Job identity, state and publication counters remain intact. Running/queued async `accepted:true`, failures, nonjob replies and nested raw job-status records stay unchanged. The native CLI schema is unchanged.

Compact `status.virtual_down` is a snapshot of the DLL's published virtual VK state (numeric Windows VK codes, including mouse-button VKs). An empty array means no virtual down bits at that sample; missing or malformed native data is omitted, not converted to an empty array. This does not report physical keys, buffered legacy messages, an application's private key latch, or proof that the game consumed a release. Use a per-key trace and verify the game before diagnosing a stuck key from movement alone.

## Agents and controllers

Use short, measured move–look steps. `pad_move {pid, direction:"up-left", ms:600, speed:"walk", capture_settle_ms:150}` sends one finite action then returns its image. New pad_move defaults to capture_after:"immediate" plus a 100 ms settle timer, avoiding an unnecessary zero-Present timeout on each PrintWindow move. Set capture_after:false for no image or delay; explicit true/numeric waits keep their usual meaning. `walk` is stick magnitude 20000 (useful in Detroit slow-walk scenes), `run` is 32767; diagonals keep the same magnitude. These names express **screen intent** with up=+LY/right=+LX. Actual screen motion depends on the game's camera/character mapping: Detroit's fixed room camera can invert it. Set screen_rotation_deg:180 for a measured inversion, or 90 to map intended up to stick-right; the clockwise compensation is applied before axis quantization. Recheck/reset this per-call angle after a camera cut; there is no auto-detection or shared calibration state. First test a short safe step, compare the character's screen position, then shorten/correct; do not keep walking into an obstacle or forbidden area. No automatic pathfinding or verified screen-direction calibration is claimed.

For buttons, start with `pad {pid, buttons:"a", ms:150, capture_after:true, capture_settle_ms:150}` and verify the resulting screen. A 150 ms press is a recipe, not proof a game consumed it or that first-use pad glyphs are ready. `capture_settle_ms` is an optional nonblocking timer after the successful CLI reply on pad/pad_move/pad_ramp/pad_timeline; it requires capture_after. Synchronous finite actions normally reply after their native job; async jobs may still run and hold:true remains held. The capture observation records that distinction and measured timer delay. It never retries the action or invents completion. Boolean capture_after retains the swapchain/Vulkan Present wait and reported zero-Present PrintWindow fallback; numeric waits stay strict and 0 means capture without a Present wait, after the optional settle delay. A captured image is not proof of application response.

Pad-family observations (`pad`, `pad_move`, `pad_look`, `pad_rotate`, `pad_ramp`, `pad_timeline`) accept capture's max_width, scale, region, format and quality. Images default to a **960-pixel maximum width** when neither size option is explicit; smaller images are not enlarged. The same image options and 960px default also apply to `key`, `click`, `click_text`, `move`, `move_relative` and `key_sequence` when capture_after is enabled. Standalone capture and the legacy drag/pulse observation paths keep their defaults. Example: `pad_move {pid, direction:"up", ms:400, region:{x:200,y:100,w:800,h:600}, max_width:640}`. The region is in **pre-scale client pixels**, not a previous screenshot's pixels. `capture_after.image` describes the attached image's width/height, scale and offset; the outer capture_after w/h describe the saved native capture and can differ when MCP resized the image. Use coord_space:"capture" for the latest successful image's mapping, or convert client = offset + image / scale; capture again after a camera/window change.

Size options are exclusive: choose max_width 64..8192 or scale 0.05..64 (PNG output stays within 32768 per side and 64 million pixels). PNG enlargement uses the existing MCP path (`upscale:true` lets max_width enlarge a crop); JPEG action images allow scale 0.05..1 and quality1..100 (JPEG only). Lossy JPEG is for observation, not exact colour comparisons. Explicit image options require enabled capture_after (0 is enabled); false disables the image and timer. Invalid combinations are refused **before input**. If a later backend, encoding or filesystem capture fails, capture_after_error is explicit while the action receipt remains intact: the action is never replayed. Async/held images remain observations, not completion proof; the existing observed-attachment epoch checks and broader overlapping-capture mapping limits are unchanged.

`smooth:true` on pad_move samples an ease-in/out curve as **one** bounded native timeline; other pad controls stay neutral and the end is neutral. Existing pad defaults remain rectangular. Smooth moves require at least 80 ms; use longer moves if you need intermediate samples, and inspect skipped-state/lateness receipts under load. The native timeline uses time pacing so it also works without an observed render stream; no presented-frame separation is promised. Small ramp values can be inside the game's deadzone, and smoothing does not measure or remove that deadzone. Calibrate a useful deflection before relying on short moves. `job_cancel` stops an async move; `release` is the emergency neutral path.

For “rotate the stick” prompts, `pad_rotate {pid, stick:"left", start_deg:0, direction:"clockwise", turns:1, ms:1200}` requests 25 points (24 segments) around a normalized 32767-radius circle, then a 50 ms final-point dwell before cleanup to neutral: total 1250 ms. Angles are stick-space, 0=up and 90=right; counterclockwise reverses winding, and fractional turns 1/8..4 make arcs. Choose the stick/start/direction the actual prompt requests. Radius is one vector magnitude, so diagonals do not exceed it. Other controls stay neutral. Default image/settle and async/cancel semantics match pad_move; use capture_after:false if you do not want the observation.

Rotation ms excludes end_dwell_ms (default 50, allowed 1..1000); both are reported separately with total time and requested point count. Times are quantized to strictly increasing integer milliseconds, requiring ms at least the segment count; use adequate time (the 1200 ms/turn recipe gives about 50 ms per segment) rather than relying on extremely short arcs. The native timeline is time paced and may skip states under load. The final-point dwell is requested schedule time, not proof of a completed XInput poll or prompt acceptance; inspect native skipped/lateness receipts and verify the game. This command does not rotate the camera by a calibrated angle. Actual std+Ex trajectory/endpoint readback remains a separate cross-check.

For timed choice wheels, observe near the expected transition with a bounded wait_change or short capture_burst, then take a **fresh single-frame capture** before reading coordinates. A contact sheet is retrospective; its tiles are not a coordinate map and cannot guarantee catching a prompt before expiry.

For camera calibration, keep resolution/FOV/zoom, camera settings, scene, stick deflection and duration fixed. First measure a **known angle** using a reliable compass/landmark or validated FOV-feature geometry, at the intended constant right-stick deflection. If the angle is unknown, captures/find_image support **pixels per second only**; do not turn that into degrees. Repeat in both directions, release between steps and recalibrate after camera/settings changes. No automatic vision or global calibration storage is supplied.

With a real angular calibration, `pad_look {pid, degrees:90, calibration:{rx:20000, degrees_per_second:60}}` predicts a1500ms right-stick turn and returns an image after its reply plus100ms settle by default. The signed calibration.rx must be the value measured to produce positive/rightward rotation (it can be negative for inverted controls); negative requested degrees uses its opposite, assuming symmetric response. Verify that assumption in both directions. Constant-speed calibration can still miss acceleration/deadzone effects; calibrate at comparable durations and inspect the actual result. `angle_verified:false` and the supplied calibration are returned explicitly. Integer-ms rounding changes the predicted angle and is reported. Raw calculated duration outside1..60000ms or an unrepresentable opposite axis is **refused before input**, not clamped. Other controls stay neutral and the finite native job releases RX; async+capture observes an ongoing job without claiming completion. capture_after:false disables image/delay; explicit true/numeric keep normal capture semantics.

The delay guards observed MCP attach/detach/launch changes before requesting the image. Avoid concurrent retarget/capture operations; this is not an atomic native-map or PID-lifetime guarantee, and an external change during capture remains outside that guard. Synthetic XInput readbacks establish delivery/cleanup only; Claude's separate game field tests establish game behavior.

## Notes for agents

- **Verify every action.** `ok:true` from `click`, `key` or `click_text` means the input reached the game, not that the
  game did what you wanted. Use `wait_text`, `wait_change` or a new `capture`.
- **Text matching** is case-insensitive, ignores whitespace and matches substrings of a line. If a label appears more
  than once, `click_text` refuses and lists candidates: pass `nth` or a `region`.
- **OCR misses** small or stylised text: empty raw OCR automatically tries bounded upscale/contrast passes on the same captured image. Nonempty-but-wrong results are not retried. Use `region` + `scale: 2`, or cut an icon from a capture and use
  `click_image`.
- **3D games** often read raw input: walk with `key` + `hold_ms`, look with `move_relative`; check `status` counters
  if nothing happens. UWP games often ignore keyboard messages but read XInput: use `pad`.
- **Big captures** cost tokens: capture a `region`, or `max_width` (for example 960), when you do not need the full frame.
- **Do not guess pixels:** `capture grid:100` labels exact client coordinates on the image; `zoom` + `region` reads small things.
- **Act and look in one call:** `capture_after:true` on `click`, `key`, `click_text`, `move`, `drag`, `scroll`, `pad` returns the next frame.
  A number is a **frame count**, not milliseconds (`capture_after:"immediate"` = capture now; `capture_after:0` or `false` = no image); for a timer use `capture_settle_ms`.
- **Short replies:** `compact:true` on any tool (or `VINPUT_MCP_COMPACT=1`) returns only the fields needed to continue.
- **Every tool answers `target exited (pid N, exit code X)`** once the game has ended, not a pipe error.
- More: [../docs/AGENT_TIPS.md](../docs/AGENT_TIPS.md) (setup, click timing, drag, per-game notes) and [../docs/MCP_TODO.md](../docs/MCP_TODO.md) (waits on core features).

### Neutral controller arming (opt in)

`attach {pid, arm_pad:true, pad_wait_ms:500}` asks a matching vinput DLL to enable a neutral virtual XInput user0 contribution, then waits at most the requested0–2000ms for an actual completed virtual GetState/Ex return. Existing queued/running pad jobs or held virtual buttons/axes are refused without cancellation or release. Plain attach stays unchanged. `pad_arm.poll_observed` reports the API-return witness; `neutral_return_observed` describes that returned state, and may be false when foreground/opt-out physical input is merged. No poll leaves it null. This does not announce a hardware device, virtualize GetCapabilities, switch game glyphs, or prove application readiness. Neutral arming persists until `pad off` or another pad publication, as existing pad state does; it sends no buttons, motion or rumble. Use a fresh capture to verify the next deliberate action.

### Launch arming and claim inactivity TTL

Native launch `--timeout` is a shared post-lock budget for pre-injection, window discovery,
adoption, settle/guard and readiness stages. Readiness/helper transports bound both writes
and reads and reject a pipe served by another PID. Replies expose the requested budget,
elapsed time and expiry. Lock acquisition has its separate `--lock-timeout`; synchronous
OS/filesystem/COM calls and bounded cleanup can add latency, so this is not a hard realtime
wall-clock promise. A timed-out loader reports unknown injection and whether its remote
argument storage was retained; it is never retried or freed while the remote thread may
still consume it. Such retained remote storage can remain until the target exits.

`launch {exe:"C:/Games/Game.exe", claim:true, claim_ttl_s:9000, arm_pad:true, pad_wait_ms:500}` uses the existing neutral-arm protocol after successful launch/claim; it does not inject again, press a button, cancel jobs or retry input. If requested claim/arming is refused or unconfirmed, MCP reports `ok:false`/`isError:true` with `launch_ok:true`, `launched:true` and the original PID/launch fields preserved: inspect `pad_arm_attempted`, `pad_arm_acknowledged`, `pad_arm_error` and the native `pad_arm` receipt. Do not relaunch that target. A missing poll is not application or glyph readiness; all neutral-arm limits above still apply.

`claim_ttl_s` is available on `launch`/`attach` only with `claim:true`, and on `claim {pid, claim_ttl_s:9000}`. Bounds are integer 1..86400 seconds, default 3600. Native claims already slide on accepted matching-owner input commands; status/readiness/log/jobs and the host-only `wait` do not renew them. Longer total sessions do not require periodic re-claim while qualifying owner input continues within the inactivity interval. Use the same explicit `VINPUT_OWNER` for one-shot MCP server instances; their default `mcp-<server pid>` names otherwise differ. An owner-mismatch refusal is not evidence that a TTL expired. This change does not alter native ownership policy or silently take over another owner.

### Controller polling diagnostics

Compact `status` preserves a matching DLL's typed `virtual_pad` telemetry, including zero counts and unobserved nulls. Decimal-string counts remain exact: `virtual_returns`, `standard_returns` and `extended_returns` count completed virtual XInput returns; `native_calls_bypassed` counts those served without calling a lower native reader. Compare samples around a deliberate action, and compare `last_return_publication` with the current `publication`; an older return does not confirm the current state was read. Missing or malformed telemetry is unknown. `counters.xinput` counts hook entries, which include non-virtual/error/other-user calls. Key `trace` observes keyboard/mouse channels and has no pad channel.

These observations do not prove the game accepted a button, announced a device or selected XInput for discovery. DirectInput joystick, HID/Raw Input gamepads, Windows.Gaming.Input and winmm controller reads remain unmeasured by these fields; zero virtual returns cannot identify which alternative the game uses. `capabilities_virtualized:false`, `device_arrival_announced:false` and `application_ready:null` retain the native limits. Use `status {verbose:true}` for the full raw native reply.

### Contact sheet attachment budget

`capture_burst` preserves its saved original, frame timestamps, hashes and tile rectangles. If a PNG sheet exceeds the MCP attachment byte cap (default 8 MiB), MCP tries at most four smaller attachment-only previews and checks their actual encoded byte size. `attachment` reports original bytes/dimensions separately from preview bytes/dimensions and per-axis scales. Native frame rectangles still refer to the saved sheet. A temporal sheet never updates the single-image cursor map: capture a fresh frame before clicking.

Encoded size depends on the picture, so geometry cannot predict its exact compressed length. Native geometry/allocation limits remain in force. Unsupported PNGs, irreducible previews and oversized JPEGs return the saved capture receipt with an explicit attachment omission reason. JPEG shrinking is unsupported; request fewer/smaller tiles when using JPEG. No preview retry repeats the burst or input. PNG preview decode is limited to 8192 per edge and 16M pixels, with bounded decompression output; normal single-capture behavior is unchanged.

Every shared `region` option accepts either `{x,y,w,h}` or `[x,y,w,h]` in client pixels. Both normalize to the same rectangle and CLI argument. Coordinates must be nonnegative integers and dimensions positive integers. A tuple must have exactly four entries; invalid action-observation regions fail before input. This does not change whether a tool's returned image has a usable single-frame pointer map.

### Driving in one job

`pad_drive {pid, throttle:120, steer:0, ms:800}` requests RT throttle and LX steering in one finite time-paced XInput timeline, then neutral and a post-reply observation (default100ms settle, max_width960). LT is `brake`, which may reverse in a particular game. It does not measure distance, heading, collision or stopping. Calibrate mappings and use short legs near landmarks. Native `skipped_states`/lateness and the fresh image remain the evidence; a submitted control is not proof of game movement.

For multi-phase manoeuvres, use `phases:[{throttle:100,ms:500},{brake:200,ms:300},{throttle:80,steer:20000,ms:700}]`. Every phase resets omitted controls to0; other sticks/buttons remain neutral. This is one owned job, so no CLI-start neutral gaps separate the phases. Native completion/cancel owns neutral cleanup. `async:true` returns its job ID; `job_status`/`job_cancel` retain their usual semantics, and a concurrent image does not confirm completion.

Give either top-level controls plus `ms`, or1..16 phases. All phase durations plus any tail must fit60000ms. `brake_after` defaultsfalse; explicittrue requests LT255 for250ms, or give `{amount:160,ms:250}` (amount0..255, ms1..5000). This is a mapped trigger request, not a stop guarantee, and may cause reverse. Steering is signed16-bit -32768..32767, triggers0..255. Brake before tight turns, reduce throttle near objectives, and reconsider/restart a stuck manoeuvre after about10 unsuccessful observations instead of repeating hundreds of inputs. These are agent guidance, not a driving controller or automatic path planner.

### Keyboard and mouse action observations

`key`, `click`, `click_text`, `move`, `move_relative`, `drag`, `scroll` and `key_sequence` (and the helper tools from `hold_until_change` on) accept `capture_after` plus `max_width`, `scale`, `upscale`, `region`, `format`, `quality`, `save_path` / `out_dir` and `capture_settle_ms`. No image is taken unless capture_after is enabled (0 is enabled). Example: `move_relative {pid, dx:120, dy:0, capture_after:"immediate", max_width:640, capture_settle_ms:100}`. Image sizing/validation and the saved-versus-returned coordinate mapping match pad observations above; width defaults to at most960 unless scale is explicit. `click_text.region` remains its OCR search region and also crops an enabled observation; a search-only region still works without capture_after.

`click_text` defaults to case-insensitive `whole_word:true` and `exact_line:true`: `PLAY` matches a standalone OCR line `Play`, while `Player` and a paragraph containing `Play` are ineligible. Whitespace remains ignored and `nth` counts eligible matches in reading order. Multiple matches still refuse and list candidates; successful/dry-run clicks also include eligible alternatives and actual OCR lines. Explicit `exact_line:false` permits matching within paragraphs. Existing `exact:true` or `whole_word:false` calls retain their prior mode unless `exact_line:true` is supplied. An explicit fuzzy budget compares complete labels when exact-line mode is active. These are OCR boundaries, not a guarantee about what the image actually says.

Default clicks require CLI `text_word_boundary_version:1` and `text_line_match_version:1`. One bounded2s read-only `help` preflight checks the requested capabilities before input, without caching or target/claim changes; observed attachment changes during it refuse the click. An older CLI fails with update/opt-out instructions. Read-only `find_text` and `wait_text` keep their prior matching defaults and support opt-in `exact_line`. Exact-line searches skip OCR tiles to avoid treating clipped paragraph fragments as labels; use `region` for oversized frames. Native replies retain matched text/candidates and report the applied line/boundary/case modes.

Raw `ocr` defaults to `retry_empty:true`: initial 1x, 2x, contrast 2x, contrast 3x, stopping on nonempty text. Every pass uses one captured image; no recapture changes the observed state. Explicit `scale` is respected, and explicit `preprocess` is retained during retries. `retry_empty:false` keeps the requested pass only. `text_only:true` returns compact text/count, selected pass, retry-error count and capture/QPC provenance, omitting repeated line/word boxes and confidence prose. `ocr_confidence` remains null. An older CLI's unconfirmed retry support is reported explicitly; MCP can still compact its reply. Example: `ocr {pid, region:{x:100,y:100,w:250,h:80}, text_only:true}`.

Options that are incompatible or out of bounds are rejected before input. A later capture/backend/filesystem failure leaves the original action/job receipt intact with capture_after_error; it never replays the action. key_sequence still returns after scheduling one job: its image can show an ongoing sequence, and does not imply completion. Use job_status or job_cancel as before. The optional settle delay is a bounded nonblocking timer, not a completion wait. Observed attachment changes before capture suppress the image; overlapping captures and unobserved external process/window changes retain the existing mapping/lifetime limits.

### Waiting for one keyboard sequence

`key_sequence {pid, steps:[{action:"key",key:"w",ms:150}], wait:true, capture_after:"immediate", max_width:640}` first schedules ONE native job, then observes its exact `PID:job-epoch:serial` ID, owner and sequence kind. The matching native version/jobs endpoint are checked before sending the sequence. Default behavior remains asynchronous. `wait_timeout_ms` is optional only with wait:true,1..120000ms after the scheduling acknowledgement; its default is the native requested duration+5000ms, capped120000. Configure the MCP client's request timeout high enough for long waits and a following capture. Polling uses bounded CLI calls and host timers, not blocking sleeps.

A successful wait returns the native completed snapshot and `sequence_wait.outcome:"completed"`; `sequence_input` retains the original scheduling receipt. `sequence_wait.game_consumption_measured:false` is explicit: scheduler completion counts virtual transitions/release, not game consumption. When waiting, capture_after runs only after successful completion; without wait it remains an observation of a possibly ongoing job. Cancelled, explicitly replaced, failed, timeout, invalid/missing status, generation/owner mismatch or observed attachment change returns an error with the initial receipt retained. An unsuccessful scheduling response is retained too: `ack_unavailable` preserves delivery_unknown/may_have_started/reply_raw, while explicit input_injected:false is `scheduling_refused`. Confirmed target exit adds exit metadata without replacing these details. Timeouts and observation failures NEVER replay or automatically cancel the job. Use job_status/job_cancel with its ID. Unknown/pruned job records are observation failures, not completion. Native records are bounded (about60s/128 terminal records); rapid job churn can make a completed job unobservable.

Steps can also `retap`: `{action:"retap", key:"w+shift", retap_key:"shift", ms:1500, every_ms:300, gap_ms:10}` keeps the whole chord held and releases / re-presses only `retap_key` every `every_ms` (`gap_ms` 1..1000, default 10, strictly below `every_ms`). `retap_key` must be one key of `key`. `schedule:{at_ms:[0,405,810], key:"space", hold_ms:20}` replaces `steps` with absolute offsets. Limits: 1024 steps, 118000 ms of declared timing.

Only with `wait:true`: `abort_on {kind:"frame_change"|"region_change"|"text"|"image", region, text, template, grace_ms, poll_ms}` stops the exact job when the condition is seen; `progress_check {region, max_unchanged_ms}` stops it when the region stays unchanged; `capture_every_ms` (250..30000) keeps scoped PNGs during the job (`capture_max_frames` default 16, max 32; `capture_max_width` default 640, max 960; `capture_region`; `capture_out_dir`) and returns them with a review sheet in `sequence_captures`. `start_on` (same kinds) delays admission until a condition is seen; `max_lateness_ms` aborts the whole sequence before an overdue key edge exceeds it.

The ID is native protocol identity, not a kernel process-creation witness. Polling is read-only and does not renew the native claim TTL; claim expiry/replacement can terminate the job and is reported. Existing observed attachment epochs and concurrent image-map limitations remain; no new global lifetime or input-consumption promise is made.

### Holding until a region changes

`hold_until_change {pid,key:"space",region:[100,200,300,120],timeout_ms:5000}` schedules one bounded native key/chord job and watches scoped region hashes while that job is holding. On change, timeout or observation failure it cancels only the admitted generation-bound job. It never uses an indefinite key_down or releases unrelated input. The native duration remains the release bound if the MCP disconnects. Invalid/missing cancellation acknowledgement is reported as unconfirmed; it is not changed to release success. An observed attachment change prevents cleanup into the new attachment.

Choose a region without clocks, FPS overlays or unrelated animation. Changed pixels prove only the selected pixels changed; they do not establish a successful interaction or gameplay progress. The tool requires typed native scoped-capture and input-job support and refuses before input when those capabilities are absent. `capture_after` and image options use the same post-action image path as key/pad actions, preserving the original attachment epoch.

The key job admission is tied to that exact captured window lifetime and geometry by native `expected_capture_scope`. A lost or malformed admission ACK reports `hold_may_still_be_active:true`; no retry or guessed cancellation occurs. A strict native `ok:false,input_injected:false` refusal proves no admission. The native timeout remains the release bound when an exact terminal receipt is unavailable.

### Compact replies

`compact:true` on any tool returns a short JSON reply: `ok`, pid, job state and IDs, observed reader channels (`input_read`), image path / mapping, matched text and boxes, warnings, notes and refusals, plus `compact:true`. Image content is unchanged. `VINPUT_MCP_COMPACT=1` (also `true` / `yes` / `on`) makes compact the default; `compact:false` on a call restores the full reply. Errors, `ok:false` replies and the query tools `list_targets`, `list_keys`, `config`, `doctor`, `log` and `trace` are never compacted. A compact reply does not say more about game response than the full one.

### Navigation in 3D games

- `move_until_stuck {pid, key:"w", region:<world area>, timeout_ms:2000, still_ms:500}` holds the key as one finite job and cancels exactly that job when the region has stayed within tolerance for `still_ms` (after `grace_ms`). `stopped:"no_motion"` means quiet pixels: a wall, a pause and ignored input look the same. Avoid regions with an animated HUD.
- `look_calibrate {pid, mode:"counts", yaw_counts:300, observed_yaw_deg:45, game:"my-settings"}` stores a signed counts-per-degree gain from your own measurement (compass, known turn), or pass `counts_per_degree` (and optional `pitch_counts_per_degree`) directly. Counts mode sends no input. The profile is saved per process and window (and `game` label) under `VINPUT_CAMERA_PROFILE_DIR` and survives an MCP restart; it never applies to another process. Recalibrate after sensitivity, FOV or input changes.
- `turn {pid, degrees:60}` and `look {pid, yaw:30, pitch:10}` use that saved gain. `look_turn {pid, yaw_deg:30, counts_per_degree:5.2}` takes the gain in the call instead. All three submit one scope-fenced relative move; the actual camera angle is not measured.
- `unstick {pid, degrees:60, capture_after:"immediate"}` runs one recipe (S back, saved-gain turn, Space unless `jump:false`, W forward). It uses those default keys, so check the game's bindings first, and inspect the image before trying again.

### Several clicks or hovers in one call

`click_many {pid, points:[[100,200],{x:140,y:200}]}` and `click_grid {pid, x0, y0, dx, dy, cols, rows}` place each click through the normal native click admission, inside one bounded session (`timeout_ms` default 30000) with a shared deadline. They stop on an uncertain or failed receipt, user activity, a scope or attachment change, cancellation or the deadline, and never replay a point. `completed_points` counts injected clicks; placement in the game stays unknown. `verify_change:true` with a small `verify_region` measures a coarse RGB change per point (`verify_settle_ms`, `min_change_pct`, `pixel_tolerance`) and stops the rest on unchanged pixels. `capture_after` takes one image after the whole batch.

`hover_many {pid, points:[...], probe_region:<tooltip area>, ocr:true}` hovers each point once (`hover_ms` default 500) and returns a scoped hash and optional tooltip OCR per point, without clicking.

`click_delta_cursor` is for games that draw their own cursor and read only relative motion: it finds the cursor sprite from `cursor_template`, moves it toward `x`,`y` with bounded raw deltas (you supply signed `counts_per_pixel_x` / `counts_per_pixel_y`), checks the hotspot twice, then clicks once. A missing or ambiguous sprite, no progress, user activity or a scope change stops it without clicking.

`click_to_move {pid, x, y, hold_ms:800, region:<world area>}` moves there, runs one bounded button hold and waits for the region to change and then settle. It returns `displacement:"UNKNOWN"`: a hash change does not measure movement.

With `coord_space:"capture"`, these tools, `click`, `move` and `drag` report the transform they used in `mapping_used` (scale, offsets, source window scope and `capture_map_id`, a geometry token, not a frame ID). `capture_after` has its own `mapping`.

### Chromium and Unity windows

On a live `Chrome_WidgetWin_0` / `Chrome_WidgetWin_1` window, vinput uses legacy window-message input for UI clicks and, when the call omits them, a 350 ms hover and a 400 ms hold. On a `UnityWndClass` window an omitted click hover and hold become 250 ms each, and `drag` uses its Unity preset (above); input routing is unchanged. Explicit `hover_ms` / `hold_ms`, drag timings and explicit input modes override these presets. Relative raw mouse motion keeps its usual policy. The click presets need a vinput DLL built from the same release.

### Waiting for pixels to settle before the image

Completed actions that take `capture_after` also take `capture_stable`: `true` watches the whole client, `{region, timeout_ms (100..10000), poll_ms, stable_samples, max_change_pct, pixel_tolerance}` watches one area, until it is stable, then the image is taken. `scroll` has the same object as `settle`. The timeout is advisory: the image is still returned. It needs a completed action (not `async` or a held key); `key_sequence` needs `wait:true`. It never replays input. Exclude animated HUD and background pixels from the region.

### Observing and correcting a camera landmark

Native status `window_adopted` and `window_hidden` distinguish binding from visibility;
unknown visibility is null. A fresh, unique successful Present can identify a hidden owned
top-level window without showing it. `presentation_window` lists bounded evidence and
ambiguity/capacity/lifetime uncertainty. Adoption does not establish input queue or game
readiness, and a hidden origin disables automatic visibility restoration for that adoption.
No default show/activation is performed. A no-window capture reports `no_window_adopted`.

`observe_settle {pid,region:[100,100,500,300],stable_samples:3}` watches exact scoped region hashes and reports their changes and stable-window duration. It sends no input. Quiet pixels can mean a pause, a clamp, input rejection, or a stable camera; animated overlays can prevent stability. `queue_drain_measured:false`, `camera_angle_measured:false` and `raw_queue_remaining_counts:null` keep those unknowns explicit. Inspect the game's camera smoothing settings when views continue to change after raw movement.

`look_calibrate {pid,template:"E:/landmark.png",counts:80}` submits two small raw relative probes and reidentifies one unique static PNG landmark after each hash-stability check. Its signed 2x2 matrix measures local screen pixels per submitted mouse count. It moves the view and does not restore it automatically. `degrees_per_count` remains null: image displacement does not measure camera angle or prove raw input consumption. Keep the landmark visible during both probes and select a region without unrelated animation.

Pass its `calibration` object to `look_at {pid,template:"E:/landmark.png",calibration:...,max_counts_per_step:150,max_steps:8}` to center the same landmark (or give `center_x`/`center_y`). Each bounded raw move is fenced to the calibration's exact native window lifetime/geometry, followed by pixel-stability and unique-template reidentification. Missing/ambiguous landmarks, stale scope, user ownership, no observed progress, unconfirmed motion, deadline or step limit stop the helper. Applied motion with a lost ACK remains explicitly uncertain and is never replayed. Centering pixels does not establish game aim, camera angle or gameplay progress. A calibration is local to the current camera response; settings, FOV, depth, scene movement and large turns can invalidate it even while the native window scope remains the same.

Visual helpers require the matching native relative-motion scope version and CLI `image_scope_version`. Scoped `find-image --preserve-map` observations retain the existing saved click map; their capture scope is checked before motion. All four helpers accept the standard `capture_after`, region and image transforms. Simulated protocol tests establish bounds and refusal behavior; these helpers have not been validated for a particular game's camera smoothing or targeting.

A failed launch that includes a typed, matching native DLL-load receipt reports `launched:true`, `keep_pid:true`, and `liveness:"not_verified"`. It preserves the failure/readiness fields. Retain that PID and inspect readiness before considering another launch; the historical load receipt does not prove the process is still alive. If claiming was requested, `claim_pending` and `claim_ttl_s_requested` preserve the unconfirmed request. A fresh live process with the exact adopted window, required safety hooks and only a pending queue handshake instead returns launch success and takes its claim with `input_ready:false`, `queue_ready:false`, and `readiness_unconfirmed:true`. Input commands retain their own readiness checks.

`launch {show_hidden:true}` explicitly requests the native operation below for a proven adopted hidden presenter. The default preserves hidden visibility. `window_hidden`, `window_adopted`, `show_hidden_reply/status` and `show_hidden_visibility_confirmed` distinguish discovery, adoption, request admission and observed visibility. Unproven hidden windows return within a separate discovery bound; keep the PID and follow it later. Direct launches echo the immutable `final_command_line`, requested and queried applied affinity, and the spawned PID even when a later stage fails.

Capture replies include `mapping` and `capture_mapping_persisted`. Matching DLLs keep the last image-to-client transform in the target, including crop origins and separate axis scales after resizing. Thus a fresh MCP server can use `coord_space:"capture"` without a local cache. Host resizing uses an exact map/scope CAS; failure returns an explicit mapping warning and never replays a capture. An explicit `mapping` preserves the supplied transform and gets a fresh observation-only window scope (or an already verified helper baseline) before coordinate input. Missing scope refuses input. It does not prove that the supplied transform still describes the game image. Click/move enforce that scope in the matching native build; ordinary drag retains its existing ownership behavior. Older DLLs without persisted maps need scoped observation and matching pointer-scope enforcement for these fenced clicks/moves; update the DLL when that support is unavailable.

`click {x:...,y:...,verify_change:{region:{x:...,y:...,w:...,h:...},timeout_ms:1000}}` samples a scoped region before and after one delivery. Unchanged pixels return `no_visual_change` with the original input receipt. Changed pixels may include animation; causality and game acceptance remain unmeasured. `capture_after` is one image; use `observe_settle` for scoped stable-frame observation. Action replies also surface cumulative mouse-message suppression warnings and separate known owned delivery from unrecognized sources.

Explicit native `show-hidden` requires the exact adopted `hwnd` and the current claim owner.
It queues one UI-thread `ShowWindow(SW_SHOWNA)` request with a native five-second expiry.
`ok:true` acknowledges admission; `hidden_show` reports the exact ticket, pending/completed,
call_attempted, visible_after and reason. Paused queues, changed claims, owner activity,
retargets and lost lifetime/geometry witnesses cancel without replay. ShowWindow's return
value is never taken as visibility success. The callback uses a strict thread activation
barrier, and no default show is added. Read-only native `presentation-status` returns only
PID/HWND, hidden proof and show receipts; it avoids the full status title callback. Use
bounded transport for either status command. An expired/unconfirmed receipt never proves
that the application rendered or accepted input.

Default MCP `status` includes typed `window_adopted`/`window_hidden`, bounded presentation
uncertainty and exact `hidden_show` receipts when the native schema supports them. Unknown
visibility stays null; old or malformed metadata is omitted. `verbose:true` retains all raw
native evidence. A queued show ticket never means that the window became visible.

Presentation adoption pins a private window-object cookie before native Present and checks
the same lifetime after S_OK. Its pre-call reservation is not successful-frame evidence.
Destruction or HWND recycling during Present refuses adoption; failed/test Presents never
become presenting candidates, and lost cookies are never repaired by post-call restamping.
