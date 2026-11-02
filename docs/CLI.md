# vinput CLI reference

`vinput.exe` (x64) injects into and drives one game process; it hands 32-bit targets to `vinput32.exe` automatically.
Every command prints **one JSON line** on stdout and exits 0 when the reply has `"ok":true`. Pass `--json` anywhere for
strictly JSON output (launch progress and script step reports then go to stderr; `--json help` returns the help text in
JSON). Refusals by the anti-cheat check exit with code 4 and carry `"anticheat_refused":true`.

## Reference

```
vinput inject --pid N [--dll PATH]
vinput launch (--exe PATH | --steam APPID --wait-exe NAME.exe | --aumid PFN!AppId) [--args "..."] [--wait-exe NAME.exe]
              [--lock FILE] [--lock-retry 60] [--lock-max-wait S] [--timeout 240] [--settle 5] [--fg-guard 5] [--early]
              [--dll PATH] [--no-inject] [--owner NAME] [--claim-ttl 300]
vinput list                                     pids with a live vinput pipe: exe, owner, uptime, really_foreground
vinput keys                                     every accepted key name and its VK code
vinput [--owner NAME] [--force-claim] --pid N <command>     (owner also from env VINPUT_OWNER)
vinput --owner NAME --pid N claim [--ttl 300] | unclaim
vinput --pid N status | ping | log | activate | release | restore
vinput --pid N key <combo> [ms] [--count N] [--gap ms]     combo: a, enter, esc, space, up, f1, ctrl+s, 0x2D, vk45 ...
vinput --pid N down <combo> | up <combo> | hold <combo> <ms>
vinput --pid N type "text" [--delay ms] [--keys]           WM_CHAR text; --keys = real key presses per character
vinput --pid N mouse <x> <y> [--norm] [--capture-space] [--steps N]
vinput --pid N move <dx> <dy> [--steps N] [--raw]          relative (raw / DirectInput deltas, mouse-look)
vinput --pid N click [left|right|middle|x1|x2] [<x> <y>] [--norm] [--capture-space] [--count N] [--ms 60]
vinput --pid N wheel <delta>
vinput --pid N pad <a+b+x+y+lb+rb+start+back+up+down+left+right+ls+rs+guide|none> [ms] [--lx N --ly N --rx N --ry N --lt N --rt N] [--hold]
vinput --pid N pad off                          stop overriding pad 0
vinput --pid N capture <out.png> [--method auto|native|swapchain|printwindow] [--after-frames N] [--region x,y,w,h]
vinput --pid N wait-change [--region x,y,w,h] [--timeout S]
vinput --pid N trace key <key>
vinput --pid N config fakefocus=true autoactivate=true isolate=true blockfocus=true nofullscreen=true keepvisible=true vk=on|off
                      cursor=auto|virtual|real raw=auto|post|buffer|off msgs=auto|on|off hwnd=0x...
vinput --pid N json '{"cmd":"key","key":"enter","ms":80}'
vinput --pid N ocr [--region x,y,w,h] [--lang L] [--scale S] [--tiles] [--lines-only] [--out f.json]
vinput --pid N click-text "text" [--nth N] [--exact] [--fuzzy N] [--timeout ms] [--button B] [--region R] [--dry-run] [--optional]
vinput --pid N wait-text "text" [--timeout ms] [--gone] [--region R] [--optional]
vinput --pid N assert-text "text" [--absent]
vinput --pid N find-image|click-image|wait-image T.png [--threshold 0.8] [--scales a,b,c|--no-scale] [--region R] [--nth N] [--timeout ms] [--gone] [--dry-run] [--optional]
vinput --pid N pixel X Y
vinput --pid N scroll N [X Y] [--horizontal]
vinput --pid N drag x1 y1 x2 y2 [--ms 300] [--button B]
vinput --pid N script <file> [--continue]
vinput --pid N foreground                       hand-off only: really bring the game to the front
vinput sysinput <vk>                            test only: one OS-level SendInput key (the shared keyboard)
```

Coordinates are client pixels; `--norm` = 0..1 fractions of the client size; `--capture-space` = pixels of the last
capture (after a `--region` capture, its origin is applied). `status.counters` shows which input APIs the game
actually calls (`gaks`, `di_kbd`, `rid`, `rib`, `xinput`, `wm_key`, ...): read it first when a game ignores input.
The DLL logs to `dist\run\logs\vinput_<pid>.log` (or `logs\` next to the DLL that was loaded).

## Launching

`launch` starts the game, waits for its window, injects and lets it settle. It snapshots the current foreground window
first, starts the game with a non-activating hint, and hands the foreground back if the game takes it; replies report
`foreground_restored`.

- **`--lock FILE`** (optional): a cooperative lock file held only while launching (from start until vinput has adopted
  the window, plus the settle and guard time), released on success and on failure. Use the same file for every tool
  on the machine that may take the real foreground, so they do not fight over it. Locks are atomic JSON leases with
  owner / token / file-identity checks on release; an existing lock owned by someone else is never reclaimed, even a
  stale one (`--lock-retry` seconds between attempts, `--lock-max-wait` to give up).
- **`--fg-guard S`** (default 5): some games take the real foreground back after the first restore (for example when a
  launcher dialog closes). `launch` keeps watching for S seconds and hands it back each time, reporting every hand-back
  in `foreground_regrabs[]`. If the game is running and injected but the hand-back failed, the reply is
  `ok:true, foreground_restored:false` plus a `warning` (not `ok:false`, which would invite a duplicate launch).
  `--fg-guard 0` turns it off.
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
- `config fakefocus=false` stops the focus API overrides and deactivation-message suppression; `isolate` separately
  controls hiding real background input; `keepvisible=false` stops vinput restoring a minimised background game.
- A game whose window procedure has never seen `WM_ACTIVATEAPP(TRUE)` (started in the background) is sent the activation
  messages a real switch would send (`WM_NCACTIVATE`, `WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_SETFOCUS`), posted to its own
  window, while `fakefocus` and `blockfocus` are on and it is not really foreground. Games that keep their own "active"
  flag (Fallout 1/2 and other DirectDraw-era titles) otherwise ignore all input. The real foreground does not change;
  `status` shows `app_active` and the `act_synth` counter. `config autoactivate=false` turns it off.

## Capture

`capture shot.png` saves the game's frame and reports `method`, `capture_path` (`d3d11`, `d3d12`, `gl`, `vk`,
`printwindow`), `w`, `h` and `frame`. `--after-frames N` waits for N more presented frames first (e.g. after an action);
`--region x,y,w,h` crops client pixels (later `--capture-space` coordinates keep that crop's origin).
`--method native` (= `swapchain`) never falls back; `auto` falls back to `PrintWindow` and puts the native error in
`note`. `wait-change --region ... --timeout 5` polls fresh frame hashes and returns on a visual change; a change proves
pixels changed, not that a particular action succeeded.

| path | how the frame is found |
|---|---|
| d3d11 | `Present` hook, staging copy |
| d3d12 | `Present` / `Present1`; the game's DIRECT queue from the `ExecuteCommandLists` hook; own allocator / list / fence copy the back buffer to a readback buffer; the game never waits |
| gl | `wglSwapBuffers` / `SwapBuffers`, `glReadPixels` of the back buffer on the game's GL thread, touched GL state restored |
| vk | `vkQueuePresentKHR` + device / queue / swapchain hooks; copy to a host-visible buffer chained by semaphores; needs injection before the game creates its `VkDevice` (`launch --early`), otherwise a clear refusal and the `PrintWindow` fallback |
| printwindow | `PrintWindow(PW_RENDERFULLCONTENT)`, fallback only |

GL / Vulkan drivers may present through their own internal DXGI swapchain; those presents are recognised as
`foreign_dxgi` and never counted as frames of their own. `config vk=off` makes every Vulkan hook a pass-through.

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
`match_score` < 1. `--nth N` is 1-based in reading order. `ocr_confidence` is always `null`: the Windows engine reports
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
