# vinput CLI reference

`vinput.exe` (x64) injects into and drives one game process; it hands 32-bit targets to `vinput32.exe` automatically.
Every command prints **one JSON line** on stdout and exits 0 when the reply has `"ok":true`. Pass `--json` anywhere for
strictly JSON output (launch progress and script step reports then go to stderr; `--json help` returns the help text in
JSON). Refusals by the anti-cheat check exit with code 4 and carry `"anticheat_refused":true`.

## Reference

```
vinput inject --pid N [--dll PATH] [--hooks-only[=true|false]]
vinput launch (--exe PATH | --steam APPID --wait-exe NAME.exe | --aumid PFN!AppId) [--args "..."] [--wait-exe NAME.exe]
              [--lock FILE] [--lock-retry 60] [--lock-timeout S] [--timeout 240] [--settle 5] [--fg-guard 5] [--early] [--cwd DIR]
              [--client-size WxH] [--cpu-affinity 0xMASK] [--show-hidden] [--dll PATH] [--no-inject] [--owner NAME] [--claim-ttl 300]
vinput launch --follow-pid N [--follow-created T]     continue an earlier launch from its launcher receipt; starts nothing
vinput list                                     pids with a live vinput pipe: exe, owner, uptime, really_foreground
vinput keys                                     every accepted key name and its VK code
vinput [--owner NAME] [--force-claim] --pid N <command>     (owner also from env VINPUT_OWNER)
vinput --owner NAME --pid N claim [--ttl 300] | unclaim
vinput --pid N status | ping | log | activate | release | restore
vinput doctor --pid N [--key KEY]               read-only diagnostics; one JSON object
vinput --pid N waitexit [--timeout MS]          block until the process exits; reply has exit_code (any pid, injected or not)
vinput --pid N readiness [--wait-ms 1000] [--hooks-only[=true|false]]    read-only: hook_ready / window_ready / queue_ready
vinput --pid N key <combo> [ms] [--count N] [--gap ms]     combo: a, enter, esc, space, up, f1, ctrl+s, 0x2D, vk45 ...
vinput --pid N down <combo> | up <combo> | hold <combo> <ms>
              key / hold: [--expect-text TEXT [--expect-timeout-ms 3000] [--expect-region x,y,w,h]]
              timed key / hold / pad / pulse / pad-ramp / pad-timeline accept [--async] [--replace]
vinput --pid N jobs | job-status ID | job-cancel ID
vinput --pid N seq <file> [--replace=true|false]          keyboard sequence (alias: sequence) as one background job
vinput --pid N pulse <combo> <ms> [--duty 50] [--period-ms 100]
vinput --pid N hold-while <combo> --until-text TEXT [--timeout-ms 5000] [--region R] [--exact] [--fuzzy N]
vinput --pid N pad-hold-while <buttons> [--lx N ...] --until-text TEXT [--timeout-ms 5000]
vinput --pid N pad-ramp <lx|ly|rx|ry|lt|rt> <from> <to> <ms> [--lx N ...] [--buttons B] [--pacing frames|time] [--end-hold-ms N]
vinput --pid N pad-timeline <states.json> [--ms N] [--pacing frames|time] [--strict-frames] [--end-hold-ms N] [--stagger-ms 0..1000]
vinput --pid N type "text" [--delay ms] [--keys]           WM_CHAR text; --keys = real key presses per character
vinput --pid N mouse <x> <y> [--norm] [--capture-space] [--steps N]
vinput --pid N move <dx> <dy> [--steps N] [--raw]          relative (raw / DirectInput deltas, mouse-look)
vinput --pid N click [left|right|middle|x1|x2] [<x> <y>] [--norm] [--capture-space] [--count N] [--ms 60]
              [--di-poll-timeout-ms N] [--di-release-timeout-ms N]
vinput --pid N wheel <delta>
vinput --pid N pad <a+b+x+y+lb+rb+lt+rt+start+back+up+down+left+right+ls+rs+guide|none> [ms] [--lx N --ly N --rx N --ry N --lt N --rt N] [--hold]
vinput --pid N pad off                          stop overriding pad 0
vinput --pid N capture <out.png> [--method auto|native|swapchain|printwindow] [--after-frames N] [--region x,y,w,h]
              [--scale 0.05..4 | --max-width N] [--format png|jpeg] [--quality 1..100]
vinput --pid N capture-burst <sheet.png> [--frames 8] [--interval-ms 250] [--columns 4] [--max-width 320] [--format png|jpeg]
              [--quiet[=true|false]]
vinput --pid N wait-change [--region x,y,w,h] [--timeout S]
vinput --pid N trace key <key>
vinput --pid N config isolate=false --for 1000                  physical input passes for 1..120000 ms, then isolation is restored
vinput --pid N config fakefocus=true autoactivate=true isolate=true blockfocus=true nofullscreen=true keepvisible=true vk=on|off
                      cursor=auto|virtual|real raw=auto|post|buffer|off msgs=auto|on|off hwnd=0x...
                      edge_margin=8 fullscreen=borderless|refuse
vinput --pid N json '{"cmd":"key","key":"enter","ms":80}'
vinput --pid N ocr [--region x,y,w,h] [--lang L] [--scale S] [--preprocess MODE] [--retry-empty=true|false] [--tiles]
              [--text-only] [--lines-only] [--out f.json]
vinput --pid N click-text "text" [--nth N] [--exact] [--whole-word=true|false] [--exact-line=true|false] [--fuzzy N]
              [--timeout ms] [--button B] [--region R] [--dry-run] [--optional]
vinput --pid N wait-text "text" [--timeout ms] [--gone] [--region R] [--optional]
vinput --pid N wait-text --any "a|b" [--timeout ms] [--gone]
vinput --pid N assert-text "text" [--absent]
vinput --pid N find-image|click-image|wait-image T.png [--threshold 0.8] [--scales a,b,c|--no-scale] [--region R] [--nth N] [--timeout ms] [--gone] [--dry-run] [--optional]
              find-image [--preserve-map]   keep the saved single-image cursor map and report the source capture scope
vinput --pid N pixel X Y
vinput --pid N scroll N [X Y] [--horizontal]
vinput --pid N drag x1 y1 x2 y2 [--ms 300] [--button B]
vinput --pid N placement-drag x1 y1 x2 y2 [--steps 15] [--ms 300] [--hold-ms 60] [--dwell-ms 200] [--poll-timeout-ms 1000] [--button B] [--async]
vinput --pid N script <file> [--continue]
vinput --pid N escape-host [--timeout-ms N]     opt-in Ctrl+Alt+Shift+End hotkey host; streams receipts; never starts by itself
vinput --pid N foreground [--lease-ms 30000]    hand-off only: really bring the game to the front and grant a foreground lease (1..300000 ms)
vinput sysinput <vk>                            test only: one OS-level SendInput key (the shared keyboard)
```

Coordinates are client pixels; `--norm` = 0..1 fractions of the client size; `--capture-space` = pixels of the last
capture (after a `--region` capture, its origin is applied). `status.counters` shows which input APIs the game
actually calls (`gaks`, `di_kbd`, `rid`, `rib`, `xinput`, `wm_key`, ...): read it first when a game ignores input.
The DLL logs to `dist\run\logs\vinput_<pid>.log` (or `logs\` next to the DLL that was loaded); `status.log_path` names the
file and `log` returns the last 200 lines held in the game. `counters.fs_refused` counts refused or failed exclusive-fullscreen
requests (a game stuck retrying one never presents a frame, `counters.present` stays 0).

## Diagnose a target

`vinput doctor --pid N` returns one JSON object: architecture, AppContainer state, the input channels the game has been
seen reading and their counters, the last capture path and frame API, real foreground and guard state, the claim owner,
the exact log path, and suggested next steps. It is read-only: it does not inject, capture, send input, change the
foreground or renew a claim. A missing pipe or a gone PID returns `ok:false` with whatever process information is
available; fields it cannot read are `null`, never guessed. `doctor --pid N --key esc` adds the per-key trace
(injected transitions, dispatched messages, each API's reads). Delivered messages and API reads do not show that the
game acted; check a capture or `wait-change`.

Counters are cumulative since injection: compare snapshots taken around an action.
`sysinput` is a CLI-side, real OS-level `SendInput` into the shared keyboard: it is not redirected into the game and is not
covered by the cursor guard, so use it only while nobody else is using the machine. A program running *inside* the game that calls
`SendInput`, `mouse_event`, `SetCursorPos` or `ClipCursor` is handled by the cursor guard (below), not forwarded blindly.

## Readiness

`inject` waits up to five seconds for the keyboard / focus hooks, an adopted window and a message queue that is making
progress, and returns a `readiness` object: `hook_ready`, `window_ready`, `queue_ready`, `readiness_version` and the reason
for a failure. `inject` on an already injected PID repeats the check. `vinput --pid N readiness [--wait-ms 0..10000]`
(default 1000) is the read-only form. `--hooks-only` (`--hooks-only=true`) checks only the hooks and does not wait for a
window: for suspended or window-less setup (`launch --early` uses it before resuming its child, then checks full
readiness); `--hooks-only=false` keeps the full handshake; the flag takes exactly `true` or `false`, with `=`.
`ping` keeps its meaning (the pipe answers). Neither command shows that the game, or a program inside it, has finished
starting or accepted an action: `application_ready` is always `null`. Verify the first action on screen.

The first key / `type` or mouse-press command arms each adopted window by observing its queue before any input is
injected; a queue that is stopped fails arming without a press. Later commands keep their normal hover, render and polling
deadlines, and changing the adopted window arms again. Gamepad jobs and release / up cleanup skip the arming check. A first
`seq` can spend up to five seconds before admission; an armed one returns its job ID immediately.

## Cursor guard and physical input

**Cursor guard.** Once its hooks are armed, vinput hooks `SetCursorPos`, `SetPhysicalCursorPos`, `ClipCursor`, `GetClipCursor`,
`SendInput`, `mouse_event`, `GetCursorPos` and `GetPhysicalCursorPos` in the game. A position write or injected input from
the game reaches the real desktop only when the game owns the real foreground (two independent foreground queries must agree)
**and** either fresh hardware input arrived in its window within the last 300 ms or a foreground lease is active
(`vinput --pid N foreground`, which really takes the foreground; the lease expires after `--lease-ms`, default 30000). `config cursor=virtual` forces the
virtual cursor even then. Otherwise the call is virtualised (the game sees its own virtual position) or refused, and counted.
A newer accepted cursor command from the agent (`mouse`, `move`, `click`, `mousedown`, `placement-drag`) re-arms the virtual
position after a human takeover; read-only and rejected commands do not.

`ClipCursor` is forwarded to Windows only while the game's adopted window owns the verified real foreground and the
rectangle lies inside its client area and the desktop; in the background it stays virtual. vinput releases a clip it
forwarded when the game loses the foreground or its window or size changes (best-effort: up to 3 attempts, and only if the
clip is still the one it set); `status.cursor_guard.native_clip` shows what was forwarded and released. `GetClipCursor`
reports the game's requested clip. A clip left on the desktop before vinput was injected is never cleared. `status.cursor_guard` reports hook readiness and the live entry coverage, the foreground witnesses, why the last write
was allowed or not, and per-API blocked / virtualised / forwarded counters; `real_cursor_writes_leaked` is `null`
(desktop attribution is not measured) and zero counters do not cover a pre-hook call, a direct syscall or a PID that was not
injected. `keybd_event` is not hooked. Input commands refuse a DLL without the cursor guard or with unarmed hooks.

**Physical input isolation.** While `isolate` is on (default), `GetAsyncKeyState` / `GetKeyState` / `GetKeyboardState`,
`GetRawInputData` / `GetRawInputBuffer` and DirectInput readers return nothing from your physical devices unless this process
owns the verified real foreground; caller buffers are filled from private storage and every out pointer is validated. Virtual
input and packets the game posts to itself stay separate. Window messages from your real keyboard and mouse are dropped in the
game's window procedure while isolating. `config isolate=false` passes physical keyboard and mouse input even in the
background and also stops zeroing the physical gamepad state while the game is in the background (`status.physical_input.warning`
is set for as long as it holds).

```
vinput --pid N config isolate=false --for 1000    # JSON: {"cmd":"config","isolate":false,"for_ms":1000}
```

`--for MS` (alias `--for-ms`, 1..120000, only with `isolate=false`) makes the opt-out bounded: the deadline is kept inside the
game on a monotonic clock, evaluated by every reader, `status` and the monitor thread, and restores `isolate=true` even if
the CLI exits. `status.physical_input` shows `remaining_ms`, `expired_count`, the time spent in pass-through and the age of the
last explicit setting. `isolate=true` cancels a lease; `isolate=false` without `--for` is indefinite. A duplicate or
contradictory request changes nothing. The CLI first asks the target for `isolation_deadline_version`: an older DLL, which would
ignore the deadline and turn it into an indefinite opt-out, is refused and nothing is sent.

**Raw input from inside the game.** `status.self_raw` and `status.posted_input` report target-origin raw-message support and
bounded backpressure. A `PostMessageA/W(WM_INPUT)` the game or an in-process payload posts to itself is admitted through a private
envelope that is unwrapped inside an ordinary `DispatchMessageA/W` while outside input stays isolated; at most 1024 envelopes
stay pending for up to 10 seconds, and `self_raw.expired`, `capacity_rejected` and `post_failed` count what was not delivered.
`SendMessage`, direct raw-queue consumers, filters that exclude the envelope message and decoding a handle after its dispatch
returned are outside this route. Served packets do not show that a game acted on them: a game can read the packet and still
ignore it, and that case is not fixed.

**Legacy messages and characters.** Posted keyboard / mouse messages go through a bounded ring (512 entries) that refuses
admission when full instead of replacing live entries; a refusal never sends an unmarked message and `type` stops at the first
failed publication, returning requested / accepted UTF-16 units and `post_error`. While isolating, the game's `TranslateMessage`
builds characters from the key state captured when each key was published (Windows 10 1607 or newer, ordinary
`TranslateMessage` then `DispatchMessageA/W` pump), so `ctrl+a` and `shift+a` arrive as the chord they are. Dead keys, IME and
Alt+numpad are not guaranteed: use Unicode `type` for exact text.

## Launching

`launch` starts the game, waits for its window, injects and lets it settle. It snapshots the current foreground window
first, starts the game with a non-activating hint, and hands the foreground back if the game takes it; replies report
`foreground_restored`.

- **`--lock FILE`** (optional): a cooperative lock file held only while launching (from start until vinput has adopted
  the window, plus the settle and guard time), released on success and on failure. Use the same file for every tool
  on the machine that may take the real foreground, so they do not fight over it. Locks are atomic JSON leases with
  owner / token / file-identity checks on release; an existing lock owned by someone else is never reclaimed, even a
  stale one (`--lock-retry` seconds between attempts, `--lock-timeout` total seconds to wait, default 0 = unlimited;
  `--lock-max-wait` is an alias).
- **`--client-size WxH`** (1..8192 each, direct injected `--exe` only): one size request after settle, without activating
  or moving the window; read `client_size.applied` / `warning`. A running game stays `ok:true` if the size is refused.
- **`--cpu-affinity 0xMASK`** (direct `--exe` only): nonzero hexadecimal processor mask applied before the game's first
  instruction, for example `0xF` for four cores.
- **`--show-hidden`**: hidden presenting windows are adopted without being shown by default; this queues one
  `SW_SHOWNA`. Read the ticket / visibility receipt, not just `ok`.
- **`--fg-guard S`** (default 5): some games take the real foreground back after the first restore (for example when a
  launcher dialog closes). `launch` keeps watching for S seconds and hands it back each time, reporting every hand-back
  in `foreground_regrabs[]`. If the game is running and injected but the hand-back failed, the reply is
  `ok:true, foreground_restored:false` plus a `warning` (not `ok:false`, which would invite a duplicate launch).
  `--fg-guard 0` turns it off.
- **`--cwd DIR`**: working directory for the game (default: the exe's own folder, which is what most old games need).
  Needs `--exe`; a folder that does not exist is refused before anything starts.
- **`--early`**: the process is created suspended, vinput is injected and its pipe is up before the game's first
  instruction, then the main thread is resumed (`early_injected:true`). vinput also watches DLL loads, so
  `vulkan-1.dll` / `opengl32.dll` are hooked the moment they are mapped. Vulkan capture needs this. Requires `--exe`.
- **Launchers / Steam:** games that restart themselves through a launcher exit immediately when started directly.
  Use `--steam <appid> --wait-exe <Game.exe>` (with no `--args` it uses `steam://rungameid/<appid>`, which shows no
  arguments dialog) or `--wait-exe` with a launcher exe. `--wait-exe` picks the NEW process with that name.
- **UWP / Xbox app games:** `--aumid PFN!AppId` returns the exact PID from the activation manager. The DLL copy and the
  log folder are made readable by the AppContainer; captures are staged in the log folder and moved to the requested
  path. These games often read CoreWindow keyboard events (which vinput does not reach): use `pad`.
- Always attach other tools by **PID**, never by process name.
- `inject` loads a per-build copy of the DLL from `dist\run\`, so `build.bat` can replace `dist\vinput64.dll` while games
  run. `dist\run\` accumulates one copy per build that was injected; delete it when no game uses them.

**Run the game windowed or borderless.** Exclusive fullscreen loses its swapchain when another window is in front;
vinput refuses DXGI `SetFullscreenState(TRUE)` (`DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`, which games must already
handle). Typical switches: Unreal `-windowed -ResX=960 -ResY=540`; Unity `-screen-fullscreen 0 -screen-width 960
-screen-height 540`; otherwise the game's own settings. Do not minimise a game: minimised games stop rendering.

## Several agents, several games

Every game has its own pipe and its own command mutex inside the game process, so agents driving different games never
wait on each other. Commands to one game run one at a time, in order; the read-only commands `ping`, `status`, `log`,
`trace` (and `claim` / `unclaim`) skip that mutex, so `status` answers during a long `--after-frames` capture or wait.

```
vinput --owner agentA --pid N claim --ttl 300     # or env VINPUT_OWNER=agentA; launch --owner agentA claims the new pid
vinput --owner agentA --pid N key enter           # accepted, and extends the claim (sliding TTL)
vinput --owner agentB --pid N key enter           # refused: {"ok":false,"claim_refused":true,"claim_owner":"agentA",...}
vinput --owner agentA --pid N unclaim             # or let it expire; --force-claim overrides (logged in the game)
vinput list                                       # pid, exe, owner, claim_expires_in_ms, uptime_ms, really_foreground
```

A command with no owner is refused by a claimed game too. `status` and `list` are always readable.

## Input details

- `click X Y`, `mousedown X Y`, `mouseup X Y` move the virtual cursor there first, dispatch a hover, and wait three
  presented frames (D3D) or 100 ms before the button goes down, so hover-gated UI accepts the click
  (`--hover-frames N`, `--hover-timeout-ms`, `--settle-ms`). Replies report `cursor`, coordinates and `hover_frames`.
- `move DX DY` sends full raw / DirectInput deltas and clamps the virtual cursor to the client area; `--raw` does not
  clamp (games that compute look from `GetCursorPos` and re-centre with `SetCursorPos`). Replies distinguish
  `applied_dx/dy` (virtual cursor travel) from `raw_dx/dy` (requested deltas).
- Key names are case-insensitive: letters, digits, `enter`, `esc`, `space`, `tab`, arrows, `f1`..`f24`,
  `numpad0`..`numpad9`, `num+`, `num-`, `num*`, `num/`, `oem1`, `oem102`, `VK_OEM_PLUS`, media / browser / IME keys,
  `vk1`..`vk255`, `0x2D`. Chords with `+`: `ctrl+s`, `hold w+space 1500`, `key ctrl+num+`. Mouse buttons work in chords
  too: `lbutton`, `rbutton`, `mbutton`, `xbutton1`, `xbutton2` (`down rbutton`, capture, `up rbutton`). `vinput keys`
  lists everything.
- `type` posts exact `WM_CHAR` per UTF-16 unit (surrogate pairs and non-Latin text work); `--keys` presses real keys.
- `trace key esc` reports per-key injected transitions, dispatched messages and API reads (`GetAsyncKeyState`,
  `GetKeyState`, `GetKeyboardState`, DirectInput state / buffer, raw data / buffer). A message or raw delivery with no
  polling increase shows the game's polling code did not read the key. Counters prove API delivery, not that the game
  performed an action.
- `activate` enables the virtual focus shims only; it does not request the real foreground.
- **Window binding.** Each adopted window keeps its own original window procedure and A/W encoding. `config hwnd=...` refuses to
  re-adopt a window whose old hook chain it cannot verify (`adoption_refused:true` and a reason), keeping the current target and
  settings. A target change is also refused while held virtual input, queued legacy messages or owned translation still need the
  old window; `status.retarget_quiescence` says which. For the same PID run `release`, then `readiness --wait-ms 2000`, then repeat
  `config hwnd=...`. `status` reports `wndproc_binding_ready`, adoption refusals and unbound callbacks.
- `config fakefocus=false` stops the focus API overrides and deactivation-message suppression; `isolate` separately
  controls hiding real background input; `keepvisible=false` stops vinput restoring a minimised background game.
- A game whose window procedure has never seen `WM_ACTIVATEAPP(TRUE)` (started in the background) is sent the activation
  messages a real switch would send (`WM_NCACTIVATE`, `WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_SETFOCUS`), posted to its own
  window, while `fakefocus` and `blockfocus` are on and it is not really foreground. Games that keep their own "active"
  flag (Fallout 1/2 and other DirectDraw-era titles) otherwise ignore all input. The real foreground does not change;
  `status` shows `app_active` and the `act_synth` counter. `config autoactivate=false` turns it off.
- **Polled clicks.** When the game reads the mouse through DirectInput, a timed `click` waits (after the requested hold)
  until the game has read the button down, then until it has read the release: at most 1000 ms extra each by default,
  with a warning if a read was not seen. `--di-poll-timeout-ms N` makes both reads required (error after N extra ms,
  `0` = do not wait); `--di-release-timeout-ms N` sets the release cap separately. Replies report `di_poll_confirmed`,
  `di_release_confirmed` and the observed hold. A read proves the game called the API, not that it acted.
- **Edge guard.** `config edge_margin=8` (default; `0` disables) keeps virtual pointer paths and their resting point
  that many client pixels inside the window edge, so games with edge scrolling do not pan; a destination inside an
  edge strip is honoured exactly. Replies and `status` add `edge_contact`, `edge_sides` (left 1, right 2, top 4,
  bottom 8), `edge_guard_applied` and `edge_guard_bypassed`. `move --raw` bypasses it.
- **Placement drag.** `placement-drag x1 y1 x2 y2` (or `drag ... --placement`) runs one owned job inside the game: press,
  wait until the game has read the press and the start position, publish and wait for each step, dwell, release and
  wait for the release read. It needs a game that reads `GetCursorPos` and DirectInput mouse buttons; the CLI refuses
  before sending input when the DLL lacks it (`status.input_precision.placement_drag`).

## Timed input jobs and steering

Timed `key`, `hold`, `pad`, `pulse`, `pad-ramp` and `pad-timeline` run on a worker inside the game, so a hold no longer
blocks other commands: another client can capture or press an unrelated key during a two-second hold.

- `--async` returns at once with a `job_id` (`input_injected:null`: accepted means scheduled, not delivered). Follow it
  with `job-status ID` or `jobs`, stop it with `job-cancel ID` (only that job's input is released). Every job has its
  own deadline inside the game, so it ends even if the client goes away. At most 32 live jobs; finished jobs are kept
  for about 60 seconds.
- Overlapping timed commands on the same key or pad queue in order; an `--async` job that overlaps another job or
  manually held input is refused before any input unless `--replace` is given. `release`, `up`, `pad off` and claim
  changes cancel overlapping jobs.
- `pulse w 2000 --duty 25 --period-ms 100` is key PWM for proportional steering with digital keys. Late edges are
  coalesced and reported as skipped, never replayed as a burst; a game can still miss very short pulses.
- `pad-ramp lx 0 16000 1000 --ly 20000 --end-hold-ms 100` moves one XInput axis smoothly with the others held. Default
  pacing follows the game's presented frames (and refuses a game that presents none); `--pacing time` uses the wall
  clock.
- `pad-timeline states.json` plays a JSON array such as
  `[{"at_ms":0,"ly":20000},{"at_ms":200,"lx":16000,"ly":20000},{"at_ms":500}]` (axes not given are 0; at most 256 states,
  `at_ms` from 0, strictly increasing). Overdue states are coalesced and reported (`skipped_states`,
  `max_lateness_ms`); `--strict-frames` fails instead. `--ms N` runs past the last state.
- `seq flight.vin` (alias `sequence`) validates the whole file, then returns a background job ID at once. Lines are `hold CHORD MS`,
  `key CHORD MS`, `wait MS`, `sleep MS` or `retap CHORD KEY MS EVERY_MS GAP_MS` (CHORD stays held for MS while only KEY,
  one member of CHORD, is released and pressed again every EVERY_MS; GAP_MS 1..1000 and below EVERY_MS); `#` starts a comment, and a file with `hold w+space 1500`, `wait 100`, `key d 300` holds
  W and Space together, releases them, waits and presses D. `job-status` reports the current zero-based step and its source line;
  `job-cancel` releases the active chord in reverse press order. The keys of all later steps are reserved too, so pressing one by
  hand cancels the rest. A conflict with other work fails before any input; `--replace=true` replaces it, `--replace=false`
  (default) keeps it. Limits: 32768 source bytes, 1024 steps, 4096 transitions, 118000 ms of declared timing; the 120-second
  accepted lifetime cap still applies. After a lost reply the result is uncertain (`delivery_unknown`): inspect `jobs` instead of
  resubmitting.
- `pad a+lt 300` presses A and fully presses LT (255) for 300 ms. `lt` and `rt` are case-insensitive button-list aliases for the
  trigger axes (also in `pad-ramp --buttons` and a timeline `"buttons"` field) and add no XInput button bits; a partial trigger is
  `pad a 300 --lt 128`. An alias together with an explicit value for the same trigger is rejected, in either field order.
- `pad-timeline` states are complete snapshots (omitted axes and buttons are off) and may carry `buttons` and a `phase` label
  (1..32 letters, digits, `_ . -`). `--stagger-ms N` (0..1000, `pad-timeline` only) publishes a movement-stick change first, waits
  at least N ms and (when native Present checkpoints are observed) a later checkpoint, and only then publishes a newly pressed
  button or trigger; releases happen immediately. Guard waits shift later deadlines to keep the following dwell; the 120-second cap
  remains. `--pacing time` on a target without frames staggers by time only. The job receipt reports `phase_publications` and
  `last_published_pad`: what was published, not that the game used it. An older DLL is refused before any input.
- `hold-while w --until-text "VILLAGE"` (or `pad-hold-while`) holds, watches the screen with OCR, and cancels the hold on
  success, timeout or error. The text appearing is an observation, not proof the hold caused it.

## Capture

`capture shot.png` saves the game's frame and reports `method`, `capture_path` (`d3d11`, `d3d12`, `gl`, `vk`,
`printwindow`), `w`, `h` and `frame`. `--after-frames N` waits for N more presented frames first (e.g. after an action);
`--region x,y,w,h` crops client pixels (later `--capture-space` coordinates keep that crop's origin).
`--method native` (= `swapchain`) never falls back; `auto` falls back to `PrintWindow` and puts the native error in
`note`. `wait-change --region ... --timeout 5` polls fresh frame hashes and returns on a visual change; a change proves
pixels changed, not that a particular action succeeded.

`--scale 0.05..4` (above 1 enlarges, coordinates stay mapped) or `--max-width N` resize the saved image inside the game, and `--format jpeg` (`--quality`, default 85)
writes a JPEG; the format flag decides the container, not the file extension. Replies add `source_w` / `source_h`,
`origin_x` / `origin_y` and `region_client_w` / `region_client_h` (the client rectangle the image covers), and later
`--capture-space` coordinates use that saved image's mapping. `hash` stays the hash of the unscaled source pixels. A game
injected with an older DLL is refused these options (`status.capture_output_version` < 1) instead of silently ignoring
them.

**Screenshot coordinates.** `--capture-space` coordinates are bound to the adopted window lifetime and client geometry that
produced the saved capture. After a resize or retarget they are refused (`capture_scope_reason`, `capture_space_valid:false`):
save a fresh single-frame capture. Hashes and `capture-burst` do not refresh the map. Coordinate input needs
`capture_scope_version:1` in the loaded DLL (older ones are refused; a positioned `mouseup` still attempts coordinate-free cleanup,
so check its cleanup receipt). `--norm` wins when both coordinate modes are given. The guard compares the current client size and
observed resize / lifecycle events; a resize away and back that an outer window hook hid is not detected.

**Capture diagnostics.** A `PrintWindow` reply includes `capture_diagnostics`: the input and source HWND, class and default caption,
client size and a bounded list of other same-process windows, plus whether the full client image (before any crop or scaling) is
black or one flat colour. Native renderer captures report the source window as unavailable. A black frame beside another
proxy-class window is an observation; candidate metadata alone does not show a usable renderer source, and vinput does not change
its capture or input target on its own (`config hwnd=0x...`).

`capture-burst sheet.png --frames 8 --interval-ms 250 --columns 4 --max-width 320` saves up to 16 captures taken
`interval-ms` apart as one contact sheet, in time order. Late slots are skipped, not bunched; `tiles[]` reports each
tile's actual time, lateness and rectangle. A sheet is not a single game image and never updates `--capture-space`.

| path | how the frame is found |
|---|---|
| d3d9 | `Present` hook, readback on the presenting thread (synthetic targets only so far) |
| d3d11 | `Present` hook, staging copy |
| d3d12 | `Present` / `Present1`; the game's DIRECT queue from the `ExecuteCommandLists` hook; own allocator / list / fence copy the back buffer to a readback buffer; the game never waits |
| gl | `wglSwapBuffers` / `SwapBuffers`, `glReadPixels` of the back buffer on the game's GL thread, touched GL state restored |
| vk | `vkQueuePresentKHR` + device / queue / swapchain hooks; copy to a host-visible buffer chained by semaphores; needs injection before the game creates its `VkDevice` (`launch --early`), otherwise a clear refusal and the `PrintWindow` fallback |
| ddraw | DirectDraw games whose exclusive mode vinput turned into a windowed virtual primary surface (below) |
| printwindow | `PrintWindow(PW_RENDERFULLCONTENT)`, fallback only |

GL / Vulkan drivers may present through their own internal DXGI swapchain; those presents are recognised as
`foreign_dxgi` and never counted as frames of their own. `config vk=off` makes every Vulkan hook a pass-through.

**Exclusive fullscreen.** With `nofullscreen=true` (default) a DXGI fullscreen request is answered with a virtual
fullscreen: the swapchain stays windowed, the window becomes borderless at its size, and the game is told it is in
fullscreen (`config fullscreen=refuse` makes the request fail instead; `nofullscreen=false` passes it through).
DirectDraw exclusive mode becomes a normal cooperative level with a virtual primary / back buffer chain presented into
the game's window (2D games; captured as `ddraw`), and D3D9 `Windowed=FALSE` requests become windowed ones. `status`
reports `fullscreen`, `ddraw` and `d3d9` counters; the log names the first request and every 256th.

## UI navigation: by text and by picture

All of this runs in the CLI process (OCR = the built-in `Windows.Media.Ocr` API; image matching = our own code);
nothing extra is loaded into the game. Boxes are `[x,y,w,h]` in client pixels (a capture's region origin and any scale
are applied for you, so a box can be fed straight to `click`).

| command | what it does |
|---|---|
| `ocr [--region ..] [--lang en-US] [--scale 1] [--tiles] [--lines-only] [--out f.json]` | OCR the frame: `text`, `lines[]` and `words[]` with boxes. `--scale 2` upscales first (small text). `--tiles` adds the tiled pass. |
| `click-text "text" [--nth N] [--exact] [--fuzzy N] [--timeout ms] [--button left] [--region ..] [--dry-run] [--optional]` | find the text and click the centre of its box; reply has the matched box, `match_count`, `match_score`. **Several matches and no `--nth` = refuse, click nothing, list the candidates.** `--timeout` (default 5000) keeps polling. `--dry-run` reports without clicking. `--optional` succeeds with `skipped:true` if the text never shows. |
| `wait-text "text" [--timeout ms] [--gone] [--region ..] [--optional]` | block until the text is on screen (default 15000 ms) or, with `--gone`, until it is not. Times out with `timed_out:true` and `ocr_saw`. |
| `assert-text "text" [--absent]` | one look, no waiting. |
| `find-image t.png [--threshold 0.8] [--scales a,b,c \| --no-scale] [--region ..]` | normalised cross-correlation template match; `matches[]` with box, `score`, `scale`. |
| `click-image t.png [--nth N] [--timeout ms] [--dry-run] [--optional]` / `wait-image t.png [--gone]` | same refusal / timeout rules as the text commands. |
| `pixel X Y` | RGB (+ `#RRGGBB`) of one client pixel. |
| `scroll N [X Y]` | N wheel notches (positive = up / away) at the virtual cursor, after moving it to X Y if given. |
| `drag x1 y1 x2 y2 [--ms 300] [--button left]` | press at 1, move with the button held, release at 2 (always releases). |
| `script file [--continue]` | run a macro (below). |

**Matching rules.** Default: case-insensitive, all whitespace ignored on both sides (`LoadGame` finds `Load   Game`),
substring of a line (`PLAY` also hits `PLAYERS`). `--exact`: case-sensitive and the match must start and end on OCR word
boundaries. `--fuzzy N` (0..3) also accepts whole-word runs within N edits (OCR slips); those hits have
`match_score` < 1. `--whole-word=true` requires complete OCR words (case is still ignored); `--exact-line=true` requires the query
to cover a whole OCR line (tiled passes are then skipped; use `--region` on big frames). Both default to `false` in the CLI;
the MCP `click_text` tool turns both on by default. `--nth N` is 1-based in reading order. `ocr_confidence` is always `null`: the Windows engine reports
none, and none is invented.

**OCR is not stable, so the search is layered.** Windows OCR's layout analysis can drop words from a full frame that it
reads fine in a crop. Text commands therefore try: full frame at 1x, then a 2x3 overlapping tiled pass, then the same at
2x, and stop at the first pass that matches (~0.1 s, ~0.6 s with fallback). Stylised or very small text: use `--region`,
`--scale 2`, or `click-image`. If a label also appears elsewhere, confine the search with `--region`.

**Image matching** is zero-mean NCC on greyscale, so brightness / contrast changes do not matter. It tries scales
0.85 0.92 1.0 1.08 1.18; copies more than about +-20 % different in size are not found unless you widen `--scales`. No
rotation. A flat (single-colour) template is refused. Self-similar templates (stripes, nested squares) can correlate
with a wrong-scale copy at ~0.84: raise `--threshold` to 0.9+.

### Cookbook

```
vinput --pid N click-text "Load Game"
vinput --pid N wait-text "Select a save" --timeout 10000

vinput --pid N click-text BATTLE --nth 2                  # two buttons say the same thing: pick by position
vinput --pid N click-text BATTLE --region 400,570,480,110 # or confine the search

vinput --pid N wait-text "Loading" --gone --timeout 120000
vinput --pid N capture icon.png --region 1205,290,70,140  # icon-only button: capture it once ...
vinput --pid N click-image icon.png --threshold 0.9       # ... then click by picture
```

### Scripts

A `.vin` script has one command per line (no `--pid`), `#` comments, `sleep ms`, `echo text`. It stops at the first
failing line and prints its number; relative paths resolve against the script's folder; `--continue` runs on and still
exits 1. Example (`macros/synthetic_menu_to_game.vin`, for the synthetic target `test/vi_uitarget.cpp`):

```
assert-text "MAIN MENU" --timeout 10000
click-text PLAY
wait-text "Select a game type" --timeout 8000
click-text SOLO
wait-text "Game started" --timeout 8000
```

`vinput --pid N script macros\synthetic_menu_to_game.vin` prints one JSON line per command and a final summary
(`{"ok":false,"error":"script failed at line 5",...,"first_failure":{...}}` on failure).
