# llm-game-automation-mcp - MCP server

> **Single-player / offline games only.** It injects a DLL into the game. Never point it at online / multiplayer games
> or games with anti-cheat. See [../DISCLAIMER.md](../DISCLAIMER.md).

A [Model Context Protocol](https://modelcontextprotocol.io) server (stdio transport, official
`@modelcontextprotocol/sdk`) that lets any LLM agent play Windows games through vinput: input and screenshots go to
one game process **in the background**, so the game never needs focus and you keep your mouse and keyboard.

Apart from the host-timer `wait` tool, tools run `vinput.exe` and return **vinput's own JSON reply** as text. `ok` is surfaced as the MCP `isError`
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
| `launch` | start a game (`exe` / `aumid` / `steam_appid`+`wait_exe`, `args` (a size in them is compared with the real window: `warnings` + `requested_size_ignored`), `cwd`, `early`, `settle_s`, `timeout_s`, `lock`, `claim`, `claim_ttl_s`, `arm_pad`, `pad_wait_ms`) and inject; returns `pid`, plus `warnings` / `nothing_presented` / `fullscreen_refusals` when no frame is being presented |
| `attach` | inject into a running game by `pid`; vinput waits for its hooks, window and message queue (`readiness`; `application_ready` is always `null`) and the tool waits until the window is adopted |
| `claim` | claim or renew the current MCP owner without takeover; `claim_ttl_s` is native inactivity time in seconds (1..86400, default 3600) |
| `list_targets` | games that have vinput loaded (pid, exe, owner, uptime, really_foreground) |
| `doctor` | read-only native CLI process/architecture, connection, channel, capture/foreground/claim diagnostics and suggestions; optional `key` adds a trace. No injection, input or automatic repair; counters do not prove the game accepted an action |
| `status` | compact by default: pid, hwnd, client size, `really_foreground`, `cursor_effective` / `owner_active` (the user is using the window), `app_active`, `present`, cursor / raw / msgs modes, non-zero counters, `capabilities` (what the game's vinput DLL supports), `edge_contact`, `fullscreen` policy, `warnings[]`, and a valid native `virtual_down` VK array (including `[]`); `verbose:true` = the full vinput status |
| `config` | change / read how vinput treats a game: `fakefocus`, `autoactivate`, `isolate`, `blockfocus`, `nofullscreen`, `keepvisible`, `cursor`, `raw`, `msgs`, `edge_margin` (vinput's edge guard), `fullscreen` (`borderless` / `refuse`), `for_ms` (only with `isolate:false`: pass the user's physical input for 1..120000 ms, then vinput restores isolation inside the game); returns the effective config. `hwnd` is CLI-only |
| `log` | tail of vinput's log for a game (`lines`, `contains`, `source` ring/file); repeated lines collapse |
| `trace` | per-key delivery trace (`trace key <key>`): which input channels the game read |
| `capture` | the game's own frame as an image + `{path, w, h, method, capture_path, frame}`; `region`, `after_frames`, `method`, `include_image`, `grid` (labelled client-coordinate grid on the returned image), `max_width` / `scale` / `zoom` (resize the returned image; `zoom` may be combined with `max_width` as a cap; `grid` / `zoom` need png; reply has `image {scale, offset_x, offset_y, scaled_by}`), `format` `png` / `jpeg` + `quality`; vinput shrinks / encodes inside the game when its DLL supports it, the server does it otherwise |
| `capture_burst` | one contact-sheet image of `frames` tiles taken `interval_ms` apart (`columns`, `max_width` / `scale`, `format`, `region`); `frames[]` has each tile's time and rectangle |
| `ocr` | text and lines with boxes (`region`, `scale`, `lang`, `words`, `tiles`) |
| `find_text` | locate text without clicking (box, centre, match_count) |
| `click_text` | find text and click its centre (`nth`, `exact`, `fuzzy`, `timeout_ms`, `button`, `region`, `optional`) |
| `wait_text` | wait until text appears (or `gone`) |
| `find_image` / `click_image` / `wait_image` | template matching with a PNG (`threshold`, `scales`, `region`, `nth`) |
| `pixel` | colour of one pixel |
| `wait_change` | wait until the picture (or a region) changes |
| `click` | click at `x`,`y` (client pixels, or `norm` 0..1, `capture_space`, or `coord_space:"capture"` for the image you were shown; `capture_space:true` uses vinput's own map, which is bound to one window and client size and is refused after a resize or retarget; `coord_space:"capture"` uses this server's remembered mapping of the last image and cannot see a resize: capture again after one), `button`, `count`, `hover_ms` (default 80), `hold_ms` (default 140), `di_poll_timeout_ms` / `di_release_timeout_ms` (require the game's DirectInput read), `edge_margin`, `park_cursor`; reply `evidence` |
| `move` | move the virtual cursor (hover) |
| `move_relative` | relative mouse motion for mouse-look (`dx`, `dy`, `raw`) |
| `drag` | press, hold (`hold_start_ms`), stepped move (`ms`, `steps`), dwell at the destination (`hold_end_ms`), release; always releases. `placement:true` = vinput's owned polled placement drag (`poll_timeout_ms`), falls back to the normal sequence on an older DLL or when the game never reads the press, as Unity-style games that read buttons through window messages (`placement_fallback`) |
| `scroll` | wheel notches at the cursor or at `x`,`y` |
| `key` | press a key or chord (`hold_ms`, `count`, `gap_ms`); `async:true` (with `hold_ms`) returns a `job_id` at once, `replace` |
| `key_down` / `key_up` | hold / release a key (or mouse button name) |
| `type_text` | type Unicode text (`as_keys` for games that read raw keys) |
| `pad` | XInput pad 0: `buttons` (`lt` / `rt` also work as button names), sticks `lx ly rx ry` -32768..32767, triggers `lt rt` 0..255, `ms`, `hold`, `off`; `async:true` (with `ms`) returns a `job_id`; `capture_after` plus `capture_settle_ms` (0..5000) observes after the CLI reply; pad-family images accept capture `max_width` / `scale` / client `region` / `format` / `quality` |
| `pad_move` | finite named left-stick direction, `ms`, `speed` walk20000/run32767, normalized diagonals, explicit `screen_rotation_deg` compensation; opt-in `smooth`, `async`, `replace`; screen intent requires game calibration |
| `pad_look` | predicted degree turn using required caller angular calibration (`rx`, `degrees_per_second`); signed `degrees`, bounded finite duration, other controls neutral; capture defaults0/100ms settle, `async`, `replace`; angle remains unverified |
| `pad_rotate` | one stick circle/arc for prompts: `stick`, `start_deg`, `direction`, `turns`, rotation `ms`, optional `radius`, final `end_dwell_ms`; one bounded timeline, finite neutral cleanup, capture defaults0/100ms settle, `async`, `replace` |
| `pad_ramp` | ramp one pad axis `from` -> `to` over `ms` with the other axes / `buttons` held, `end_hold_ms`, `pacing` frames / time, `async` |
| `pad_timeline` | timed list of pad states (`at_ms`, `buttons` such as `a+lt`, `phase`, sticks, triggers), `strict_frames`, `pacing`, `stagger_ms` (new presses wait for the stick change before them), `async` |
| `key_sequence` | a validated keyboard sequence (`steps` of `hold` / `key` / `wait` with `ms`) as one background job; returns `job_id`; `replace` |
| `pulse` | key PWM: `key` down `duty` percent of each `period_ms` for `ms`, `async` |
| `hold_while` | hold a key (or pad buttons / sticks) until `until_text` appears or `timeout_ms`; always releases |
| `job_status` / `job_cancel` | follow / stop a background input job, including a `key_sequence` (no `id`: list all jobs) |
| `list_keys` | all key names |
| `wait` | nonblocking MCP host timer, integer `ms` 0..30000; no target, CLI, input, claim renewal or frame wait; returns requested and actual elapsed ms |
| `run_script` | run a `.vin` macro file |
| `release` | release all held input |
| `detach` | release input and drop this server's claim (vinput stays loaded, inert, until the game exits) |

`wait {ms:1000}` is a host-side pause, not a screen/state postcondition. Its 30-second per-call bound stays below the SDK test client's 60-second tool timeout; scheduling or an outer harness can still delay the reply. It runs no vinput process and does not block other MCP requests. Prefer wait_text/wait_change when waiting for an observable game state; a timer neither proves progress nor renews a claim.

Every tool that targets a game takes `pid`. Coordinates are the game's **client pixels**, which equal the pixels of a
full capture. Refusals by the anti-cheat check come back as `isError` with `anticheat_refused: true`; there is no way to
override them from the MCP server.

For a successful top-level completed job outcome (`job_state:"completed"`, nonempty `job_id`, native `accepted:false`), MCP omits the misleading `accepted` field and uses the existing job state and completion evidence. It adds no boolean about whether the original request was queued; it does not identify the original request's async setting, prove callback delivery, or prove a game reaction. Job identity, state and publication counters remain intact. Running/queued async `accepted:true`, failures, nonjob replies and nested raw job-status records stay unchanged. The native CLI schema is unchanged.

Compact `status.virtual_down` is a snapshot of the DLL's published virtual VK state (numeric Windows VK codes, including mouse-button VKs). An empty array means no virtual down bits at that sample; missing or malformed native data is omitted, not converted to an empty array. This does not report physical keys, buffered legacy messages, an application's private key latch, or proof that the game consumed a release. Use a per-key trace and verify the game before diagnosing a stuck key from movement alone.

## Agents and controllers

Use short, measured move–look steps. `pad_move {pid, direction:"up-left", ms:600, speed:"walk", capture_settle_ms:150}` sends one finite action then returns its image. New pad_move defaults to capture_after:0 plus a 100 ms settle timer, avoiding an unnecessary zero-Present timeout on each PrintWindow move. Set capture_after:false for no image or delay; explicit true/numeric waits keep their usual meaning. `walk` is stick magnitude 20000 (useful in Detroit slow-walk scenes), `run` is 32767; diagonals keep the same magnitude. These names express **screen intent** with up=+LY/right=+LX. Actual screen motion depends on the game's camera/character mapping: Detroit's fixed room camera can invert it. Set screen_rotation_deg:180 for a measured inversion, or 90 to map intended up to stick-right; the clockwise compensation is applied before axis quantization. Recheck/reset this per-call angle after a camera cut; there is no auto-detection or shared calibration state. First test a short safe step, compare the character's screen position, then shorten/correct; do not keep walking into an obstacle or forbidden area. No automatic pathfinding or verified screen-direction calibration is claimed.

For buttons, start with `pad {pid, buttons:"a", ms:150, capture_after:true, capture_settle_ms:150}` and verify the resulting screen. A 150 ms press is a recipe, not proof a game consumed it or that first-use pad glyphs are ready. `capture_settle_ms` is an optional nonblocking timer after the successful CLI reply on pad/pad_move/pad_ramp/pad_timeline; it requires capture_after. Synchronous finite actions normally reply after their native job; async jobs may still run and hold:true remains held. The capture observation records that distinction and measured timer delay. It never retries the action or invents completion. Boolean capture_after retains the swapchain/Vulkan Present wait and reported zero-Present PrintWindow fallback; numeric waits stay strict and 0 means capture without a Present wait, after the optional settle delay. A captured image is not proof of application response.

Pad-family observations (`pad`, `pad_move`, `pad_look`, `pad_rotate`, `pad_ramp`, `pad_timeline`) accept capture's max_width, scale, region, format and quality. Images default to a **960-pixel maximum width** when neither size option is explicit; smaller images are not enlarged. Other action tools and standalone capture keep their defaults. Example: `pad_move {pid, direction:"up", ms:400, region:{x:200,y:100,w:800,h:600}, max_width:640}`. The region is in **pre-scale client pixels**, not a previous screenshot's pixels. `capture_after.image` describes the attached image's width/height, scale and offset; the outer capture_after w/h describe the saved native capture and can differ when MCP resized the image. Use coord_space:"capture" for the latest successful image's mapping, or convert client = offset + image / scale; capture again after a camera/window change.

Size options are exclusive: choose max_width64..8192 or scale0.05..8. PNG enlargement uses the existing MCP path; JPEG action images allow scale0.05..1 and quality1..100 (JPEG only). Lossy JPEG is for observation, not exact colour comparisons. Explicit image options require enabled capture_after (0 is enabled); false disables the image and timer. Invalid combinations are refused **before input**. If a later backend, encoding or filesystem capture fails, capture_after_error is explicit while the action receipt remains intact: the action is never replayed. Async/held images remain observations, not completion proof; the existing observed-attachment epoch checks and broader overlapping-capture mapping limits are unchanged.

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
- **OCR misses** small or stylised text: use `region` + `scale: 2`, or cut an icon from a capture and use
  `click_image`.
- **3D games** often read raw input: walk with `key` + `hold_ms`, look with `move_relative`; check `status` counters
  if nothing happens. UWP games often ignore keyboard messages but read XInput: use `pad`.
- **Big captures** cost tokens: capture a `region`, or `max_width` (for example 960), when you do not need the full frame.
- **Do not guess pixels:** `capture grid:100` labels exact client coordinates on the image; `zoom` + `region` reads small things.
- **Act and look in one call:** `capture_after:true` on `click`, `key`, `click_text`, `move`, `drag`, `pad` returns the next frame.
- **Every tool answers `target exited (pid N, exit code X)`** once the game has ended, not a pipe error.
- More: [../docs/AGENT_TIPS.md](../docs/AGENT_TIPS.md) (setup, click timing, drag, per-game notes) and [../docs/MCP_TODO.md](../docs/MCP_TODO.md) (waits on core features).

### Neutral controller arming (opt in)

`attach {pid, arm_pad:true, pad_wait_ms:500}` asks a matching vinput DLL to enable a neutral virtual XInput user0 contribution, then waits at most the requested0–2000ms for an actual completed virtual GetState/Ex return. Existing queued/running pad jobs or held virtual buttons/axes are refused without cancellation or release. Plain attach stays unchanged. `pad_arm.poll_observed` reports the API-return witness; `neutral_return_observed` describes that returned state, and may be false when foreground/opt-out physical input is merged. No poll leaves it null. This does not announce a hardware device, virtualize GetCapabilities, switch game glyphs, or prove application readiness. Neutral arming persists until `pad off` or another pad publication, as existing pad state does; it sends no buttons, motion or rumble. Use a fresh capture to verify the next deliberate action.

### Launch arming and claim inactivity TTL

`launch {exe:"C:/Games/Game.exe", claim:true, claim_ttl_s:9000, arm_pad:true, pad_wait_ms:500}` uses the existing neutral-arm protocol after successful launch/claim; it does not inject again, press a button, cancel jobs or retry input. If requested claim/arming is refused or unconfirmed, MCP reports `ok:false`/`isError:true` with `launch_ok:true`, `launched:true` and the original PID/launch fields preserved: inspect `pad_arm_attempted`, `pad_arm_acknowledged`, `pad_arm_error` and the native `pad_arm` receipt. Do not relaunch that target. A missing poll is not application or glyph readiness; all neutral-arm limits above still apply.

`claim_ttl_s` is available on `launch`/`attach` only with `claim:true`, and on `claim {pid, claim_ttl_s:9000}`. Bounds are integer 1..86400 seconds, default 3600. Native claims already slide on accepted matching-owner input commands; status/readiness/log/jobs and the host-only `wait` do not renew them. Longer total sessions do not require periodic re-claim while qualifying owner input continues within the inactivity interval. Use the same explicit `VINPUT_OWNER` for one-shot MCP server instances; their default `mcp-<server pid>` names otherwise differ. An owner-mismatch refusal is not evidence that a TTL expired. This change does not alter native ownership policy or silently take over another owner.

### Controller polling diagnostics

Compact `status` preserves a matching DLL's typed `virtual_pad` telemetry, including zero counts and unobserved nulls. Decimal-string counts remain exact: `virtual_returns`, `standard_returns` and `extended_returns` count completed virtual XInput returns; `native_calls_bypassed` counts those served without calling a lower native reader. Compare samples around a deliberate action, and compare `last_return_publication` with the current `publication`; an older return does not confirm the current state was read. Missing or malformed telemetry is unknown. `counters.xinput` counts hook entries, which include non-virtual/error/other-user calls. Key `trace` observes keyboard/mouse channels and has no pad channel.

These observations do not prove the game accepted a button, announced a device or selected XInput for discovery. DirectInput joystick, HID/Raw Input gamepads, Windows.Gaming.Input and winmm controller reads remain unmeasured by these fields; zero virtual returns cannot identify which alternative the game uses. `capabilities_virtualized:false`, `device_arrival_announced:false` and `application_ready:null` retain the native limits. Use `status {verbose:true}` for the full raw native reply.

### Contact sheet attachment budget

`capture_burst` preserves its saved original, frame timestamps, hashes and tile rectangles. If a PNG sheet exceeds the MCP attachment byte cap (default 8 MiB), MCP tries at most four smaller attachment-only previews and checks their actual encoded byte size. `attachment` reports original bytes/dimensions separately from preview bytes/dimensions and per-axis scales. Native frame rectangles still refer to the saved sheet. A temporal sheet never updates the single-image cursor map: capture a fresh frame before clicking.

Encoded size depends on the picture, so geometry cannot predict its exact compressed length. Native geometry/allocation limits remain in force. Unsupported PNGs, irreducible previews and oversized JPEGs return the saved capture receipt with an explicit attachment omission reason. JPEG shrinking is unsupported; request fewer/smaller tiles when using JPEG. No preview retry repeats the burst or input. PNG preview decode is limited to 8192 per edge and 16M pixels, with bounded decompression output; normal single-capture behavior is unchanged.
