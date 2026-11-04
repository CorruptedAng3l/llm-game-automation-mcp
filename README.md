# LLM Game Automation MCP

> [!WARNING]
> **Single-player / offline games only.** Do **not** use this in online or multiplayer games, or with any game
> protected by anti-cheat: you will very likely be banned. **It injects a DLL into the target game process.**
> vinput refuses to inject when it detects a known anti-cheat, but that check is a courtesy, not a guarantee.
> **Use at your own risk**: the authors are not responsible for bans, lost saves, data loss, damage or anything else
> that results from using it. Read [DISCLAIMER.md](DISCLAIMER.md) first.

Let an LLM agent (or a script) play Windows games **in the background**. Each game process gets its own private
keyboard, mouse, gamepad and eyes:

- **vinput** - a DLL injected into one game process plus a CLI (`vinput.exe`) that talks to it over a per-process
  named pipe. Input is delivered *inside* that process on every channel games read; frames are captured from the
  game's own swapchain.
- **MCP server** (`mcp/`) - wraps the CLI as [Model Context Protocol](https://modelcontextprotocol.io) tools
  (`launch`, `capture`, `click_text`, `key`, `pad`, ...), so Claude or any MCP client can look at a game and act on it.

## Why it is different

Most "AI plays games" setups drive the real desktop: `SendInput` plus a screenshot of the whole screen. That needs the
game in front, steals your mouse and keyboard, and allows exactly one game at a time. vinput works per process:

- **The game never needs focus.** It keeps running and rendering in the background; vinput fakes focus inside the game
  and swallows its deactivation messages.
- **You keep your PC.** vinput never generates OS-level input (no `SendInput`); your real keyboard and mouse are hidden
  from a game that is not really in front, the game cannot steal the foreground from you, and its own cursor moves
  and clips go to a virtual cursor instead of your real one (see [Cursor guard and physical input](#cursor-guard-and-physical-input)).
- **Several games at once.** Every game has its own pipe and command queue, so several agents can drive several
  games in parallel, each with an optional ownership claim.
- **Pixels from the game itself.** Captures come from the frame the game presents (not a desktop screenshot), so they
  work while the window is covered by other windows.

## Supported APIs

| | supported | notes |
|---|---|---|
| **Capture** | Direct3D 11, Direct3D 12, OpenGL, Vulkan, DirectDraw (virtual primary surface), `PrintWindow` fallback | HDR formats (R10G10B10A2, RGBA16F) converted to 8-bit; PNG or JPEG, optionally scaled inside the game; contact sheets (`capture-burst`); Vulkan needs early injection (`launch --early`); D3D9/D3D10 fall back to `PrintWindow` |
| **Keyboard** | window messages (`WM_KEYDOWN`/`WM_CHAR`, with the game's own `TranslateMessage`), Raw Input (`WM_INPUT`, `GetRawInputBuffer` on x64), DirectInput 8 and legacy `dinput.dll` (DirectInput 1-7), `GetAsyncKeyState`/`GetKeyState`/`GetKeyboardState` | mirrors the game's own `RegisterRawInputDevices` choices (e.g. `RIDEV_NOLEGACY`) |
| **Mouse** | messages, Raw Input, DirectInput 8, virtual cursor behind `GetCursorPos` / `WindowFromPoint` | absolute (`click`, `move`) and relative (`move_relative`, mouse-look) |
| **Gamepad** | XInput pad 0 (`XInputGetState`, `XInputGetStateEx`) | buttons, both sticks, triggers; timed ramps and state timelines for proportional steering |
| **Vision (CLI side)** | Windows OCR (`Windows.Media.Ocr`), normalised cross-correlation template matching | runs in the CLI, nothing extra is loaded into the game |
| **Targets** | 64-bit and 32-bit games, UWP / Xbox-app (AppContainer) games | `vinput.exe` hands 32-bit targets to `vinput32.exe` |

## Requirements

- Windows 10 or 11, x64.
- To build: Visual Studio 2022 or newer (or the Build Tools) with the **Desktop development with C++** workload (x64
  and x86 compilers).
- For the MCP server: Node.js 20 or newer.
- The game should run **windowed or borderless**. Exclusive fullscreen loses its swapchain as soon as another window is
  in front.

## Build

```bat
build.bat
```

`build.bat` finds MSVC through `vswhere`; set `VINPUT_VCVARS_DIR` to a `...\VC\Auxiliary\Build` folder to pick a
specific install. It builds x64 and x86 into `dist\` (git-ignored): `vinput.exe`, `vinput32.exe`, `vinput64.dll`,
`vinput32.dll`, `vinput_x64.lib`, `vinput_x86.lib` and the synthetic test targets. MinHook and the Vulkan headers are
vendored in `third_party\`, so nothing else is downloaded.

MCP server:

```bat
cd mcp
npm ci
npm run build
npm test
```

`npm test` is the MCP handshake test (`tools/list` against the built server); it needs no game. Self-tests: see
[TESTING.md](TESTING.md).

## CLI quick start

```bat
rem start a game and inject (prints {"ok":true,"pid":1234,...})
dist\vinput.exe launch --exe "C:\Games\MyGame\MyGame.exe" --args "-windowed"

rem or inject into a game that is already running
dist\vinput.exe inject --pid 1234

dist\vinput.exe --pid 1234 capture shot.png
dist\vinput.exe --pid 1234 click-text "New Game"
dist\vinput.exe --pid 1234 wait-text "Choose difficulty"
dist\vinput.exe --pid 1234 key enter
dist\vinput.exe --pid 1234 hold w 1500
dist\vinput.exe --pid 1234 click left 480 300
dist\vinput.exe --pid 1234 pad a
dist\vinput.exe --pid 1234 status
```

`capture` saves the game's own frame; `click-text` / `wait-text` act on and verify on-screen text (OCR); `hold w 1500`
holds W for 1.5 s; coordinates are client pixels, which equal capture pixels; `pad` drives XInput pad 0; `status`
shows, among other things, which input APIs the game really reads.

Every command prints one JSON line; exit code 0 means `"ok":true`. `ok:true` on an input command means the input was
delivered, **not** that the game reacted: follow it with `wait-text`, `wait-change` or a capture. Full reference:
[docs/CLI.md](docs/CLI.md). Practical advice for agents that drive games (windowed sizes, click timing, drag-and-drop,
per-game notes): [docs/AGENT_TIPS.md](docs/AGENT_TIPS.md).

## Readiness, timed input and safety controls

```bat
rem read-only: are the hooks, the window and its message queue ready? (inject runs the same check)
dist\vinput.exe --pid 1234 readiness --wait-ms 5000

rem a keyboard sequence file (hold w+space 1500 / wait 100 / key d 300) runs as ONE background job
dist\vinput.exe --pid 1234 seq flight.vin
dist\vinput.exe --pid 1234 job-status JOB_ID

rem gamepad: A plus a fully pressed left trigger for 300 ms; a timeline with staggered presses
dist\vinput.exe --pid 1234 pad a+lt 300
dist\vinput.exe --pid 1234 pad-timeline states.json --stagger-ms 60 --strict-frames

rem let your physical keyboard / mouse reach the game for one second, then vinput restores isolation by itself
dist\vinput.exe --pid 1234 config isolate=false --for 1000

rem click on coordinates read off a screenshot (the map is bound to that window and client size)
dist\vinput.exe --pid 1234 capture shot.png --max-width 960
dist\vinput.exe --pid 1234 click left 120 80 --capture-space
```

- **Input readiness.** `inject` waits (up to five seconds) for the keyboard / focus hooks, an adopted window and a
  message queue that is making progress, and reports a `readiness` object (`hook_ready`, `window_ready`, `queue_ready`,
  `readiness_version`, and the reason for a failure). `readiness [--wait-ms 0..10000]` repeats the check without
  injecting anything; `inject --hooks-only` checks only the hooks (suspended or window-less setup, which `launch --early`
  uses). `application_ready` is always `null`: vinput cannot know that a game or a program inside it has finished
  starting, so verify the first action on screen. `ping` keeps its meaning (the pipe is alive).
- **Async key sequences.** `seq FILE` (alias `sequence`) validates the whole file first, then returns a background job
  ID at once. Lines are `hold CHORD MS`, `key CHORD MS`, `wait MS` or `sleep MS`; `#` starts a comment. Cancelling
  (`job-cancel`) releases the active chord; the keys of later steps are reserved, so pressing one by hand cancels the
  rest. At most 32768 bytes, 1024 steps and 118000 ms of declared timing; the 120-second job lifetime cap still applies.
- **Gamepad.** `lt` and `rt` are button-list aliases for the triggers (`pad a+lt 300` presses LT fully; a partial
  trigger is `pad a 300 --lt 128`); an alias plus an explicit value for the same trigger is rejected. In
  `pad-timeline` every state is a complete snapshot (omitted axes are 0) and may carry `buttons` and a `phase` label.
  `--stagger-ms 0..1000` holds back a newly pressed button or trigger until the movement stick has been published first
  (and, with native frame pacing, a later presented frame); releases are never delayed. Receipts report what vinput
  published, not that the game consumed it.
- **Capture diagnostics.** A `PrintWindow` capture reply carries `capture_diagnostics`: the window the frame came from,
  its class, caption and client size, a bounded list of other windows in the same process, and whether the full client
  image is black or one flat colour. A black image next to a second same-process window is an observation, not proof
  that the other window is the renderer: vinput does not switch its capture or input target by itself (try
  `config hwnd=0x...` yourself).
- **Screenshot coordinates.** `--capture-space` coordinates belong to one adopted window and one client geometry. After
  a resize or a retarget the map is refused: take a fresh single-frame `capture` (hashes and `capture-burst` do not
  refresh it). A DLL without `capture_scope_version:1` is refused for these coordinates, and a positioned mouse-up
  still attempts a coordinate-free release. A resize that happens and reverts without vinput seeing an event cannot be
  detected.
- **Window binding and retargets.** Each adopted window keeps its own original window procedure and its A/W character
  encoding, so retargeting between windows does not cross their hook chains. If another payload has left an opaque
  hook chain on an old window, `config hwnd=...` refuses to re-adopt it (`adoption_refused:true` with a reason) and
  keeps the current target. A target change is also refused while held virtual input, queued legacy messages or an
  owned character translation still need the old window (`status.retarget_quiescence`): run `release`,
  `readiness --wait-ms 2000`, and retry. A refusal changes no setting and no job.

### Cursor guard and physical input

While vinput is injected and its hooks are armed, `SetCursorPos`, `SetPhysicalCursorPos`, `ClipCursor`, `SendInput`,
`mouse_event`, `GetCursorPos`, `GetPhysicalCursorPos` and `GetClipCursor` inside the game go to a virtual cursor,
not your real one:

- A cursor-position write or injected input from the game reaches the real desktop only when the game owns the real
  foreground (checked through two independent queries) **and** either fresh physical input arrived in its window
  within the last 300 ms or a foreground lease is active and `cursor=virtual` is not forced (`vinput --pid N foreground [--lease-ms 30000]`, a hand-off
  and verification command that really takes the foreground). Otherwise the call is virtualised or refused and counted.
- **`ClipCursor` is real only while you play the game:** a clip the game sets is forwarded to Windows only when the
  game's adopted window owns the verified real foreground and the rectangle lies inside its client area and the
  desktop. In the background it stays virtual. vinput releases a clip it forwarded when the game loses the
  foreground, its window changes or its size changes (best-effort: up to 3 attempts, and only if the clip is still the
  one it set). `GetClipCursor` reports the game's requested clip, and `status.cursor_guard.native_clip` shows what was
  forwarded and released.
- `keybd_event` is not hooked, and a program that talks to the kernel directly bypasses every user-mode hook. The
  counters in `status.cursor_guard` are scoped to the hooked entry points; `real_cursor_writes_leaked` is `null`
  because it is not measured. Input commands refuse a DLL that lacks the cursor guard or whose hooks are not armed.

While `isolate` is on (the default), the keyboard, mouse, raw-input and DirectInput readers return nothing from your
physical devices unless this process owns the verified real foreground. Virtual input and raw packets the game
itself posts are separate. `config isolate=false` passes physical keyboard and mouse input through even in the
background **and** stops hiding the physical gamepad while the game is in the background; status shows a
`physical_input.warning` for as long as it is set. Prefer the bounded form, `config isolate=false --for 1000`
(1..120000 ms): the deadline lives inside the game, is checked by every reader, `status` and the monitor thread,
and restores `isolate=true` even if the CLI or this MCP server goes away. A later `config isolate=true` cancels it, an
`isolate=false` without `--for` is indefinite. Cursor-write safety does not depend on `isolate`.

Known limits of this contract:

- A game that sets its own cursor position while really in front of you is virtualised unless you just moved
  the mouse over it or took a foreground lease; test such a game while it is in front with
  `vinput --pid N foreground --lease-ms 30000` first. (`ClipCursor` follows the foreground rule above.)
- A program running inside the game (a trainer, for example) that calls target-side `SendInput` or `mouse_event`
  produces global OS input when it is forwarded; fake focus does not redirect it into the game. The guard forwards it
  only under the rule above (verified real foreground plus fresh human activity or a lease), and a forward reports an
  attempt, not that the application accepted it.
- If the game (or another hook) removes vinput's marker from its window, the window keeps working (its own window
  procedure still gets every message), but vinput refuses input to that window from then on and
  `status.wndproc_binding` reports it as quarantined. Restart the game or select a fresh window; recovery on the same
  window is not supported.
- In-process raw packets (a program inside the game posting its own `WM_INPUT`) are delivered through a private
  envelope that is unwrapped inside `DispatchMessageA/W`; games that read `msg.message == WM_INPUT` from their own
  message loop without dispatching, filter on `WM_INPUT`, or consume raw input from another queue do not see them.
  Packets can be *served* to a game that still ignores them: that downstream case is not fixed, only measured on
  synthetic targets.
- Isolated legacy character translation needs Windows 10 1607 or newer and an ordinary `TranslateMessage` then
  `DispatchMessageA/W` pump; dead keys, IME and Alt+numpad are not guaranteed (use `type` for exact text).
- While isolating, keyboard window messages that vinput did not publish (IME, an on-screen keyboard, the game posting
  to itself) are dropped in the game's window procedure, and pointer / touch window messages are not filtered.
- Cursor protection, physical-input isolation and retarget quiescence are tested on synthetic targets only; the
  behaviour of a particular commercial game, and of a real human using the same window at the same time, is not
  measured. See [TESTING.md](TESTING.md).

## MCP quick start

Build the server (above), then add it to your MCP client. Claude Code:

```bat
claude mcp add game-automation -- node C:\path\to\llm-game-automation-mcp\mcp\build\index.js
```

Claude Desktop (`claude_desktop_config.json`) or any stdio MCP client:

```json
{
  "mcpServers": {
    "game-automation": {
      "command": "node",
      "args": ["C:\\path\\to\\llm-game-automation-mcp\\mcp\\build\\index.js"]
    }
  }
}
```

Then ask the agent to launch a game and play it. The tools, their schemas and a short agent quick start are in
[mcp/README.md](mcp/README.md).

## How it works (inside the game process)

| part | how |
|---|---|
| fake focus | `GetForegroundWindow` / `GetActiveWindow` / `GetFocus` report the game; the game's window procedure is subclassed and deactivation (`WM_ACTIVATEAPP(0)`, `WM_ACTIVATE(WA_INACTIVE)`, `WM_NCACTIVATE(0)`, `WM_KILLFOCUS`) is swallowed, so it keeps running and rendering in the background |
| no focus stealing | while the game is not really in front, `SetForegroundWindow` / `BringWindowToTop` / `SetActiveWindow` are no-ops, `SetWindowPos` gets `SWP_NOACTIVATE`, showing the window does not activate it, cursor writes (`SetCursorPos`, `ClipCursor`, `SendInput`, `mouse_event`) go to a virtual cursor unless you are really using the game's window (cursor guard, below), DXGI / DirectDraw / D3D9 exclusive fullscreen becomes a windowed virtual fullscreen (or is refused, `config fullscreen=refuse`); every attempt is logged |
| virtual input | each event is delivered on every channel the game can read, as a real key would be (see Supported APIs) |
| isolation | while the game does not own the verified real foreground, the physical keyboard / mouse / raw-input / DirectInput readings are hidden from it, so your typing elsewhere does not leak into it; when it is in front, real input works and virtual input is added on top (and the game's own `ClipCursor` takes effect); `config isolate=false --for MS` is a bounded opt-out |
| eyes | native capture from the game's swapchain (per API), with `PrintWindow` as the fallback; replies report `capture_path` and the frame number |
| control | named pipe `\\.\pipe\vinput_<pid>`, one JSON object per line; `vinput.exe` wraps it |
| anti-cheat refusal | `src/vi_acguard.h`: the CLI refuses to inject, and the DLL refuses to start or keep working, when a known anti-cheat is detected (see below) |

## Anti-cheat refusal

Before it injects anything, vinput refuses - with `{"ok":false,"anticheat_refused":true,...}`, exit code 4 and
**no override flag** - when:

1. the target process has a module loaded whose name belongs to a known anti-cheat;
2. the game's folder (or up to three parent folders) ships a known anti-cheat component, e.g. an `EasyAntiCheat` or
   `BattlEye` folder or `start_protected_game.exe` (this catches a protected game before its anti-cheat loads, and
   is checked before `launch --exe` starts anything);
3. a known anti-cheat process is running, or a known anti-cheat service or kernel driver is active, anywhere on the
   machine.

The injected DLL repeats check 1 when it starts, and watches module loads: if an anti-cheat module is loaded into the
game later, every input and capture command is refused from then on.

Covered by name: EasyAntiCheat, BattlEye, Riot Vanguard (vgc / vgk), Ricochet, FACEIT, ESEA, nProtect GameGuard,
XIGNCODE3, ACE (AntiCheatExpert), PunkBuster, mhyprot, Denuvo Anti-Cheat. The lists are in `src/vi_acguard.h`.

**This is defence in depth, not a guarantee.** It is name-based: a renamed, new or unlisted anti-cheat is not
detected, and an absent refusal does not mean a game is safe to automate. Because check 3 is machine-wide, a kernel
anti-cheat that stays resident (for example Vanguard's `vgc` service) blocks vinput until it is stopped.

## Limitations

- **Exclusive fullscreen** is not supported: vinput turns DXGI, DirectDraw and D3D9 fullscreen requests into a windowed
  virtual fullscreen, but a game's own saved fullscreen mode is best switched to windowed / borderless in its settings.
  A **minimised** game stops rendering (vinput puts a background game back unless `config keepvisible=false`).
- **Inputs vinput does not reach:** direct syscall / `NtUserGetAsyncKeyState` readers; Windows.Gaming.Input,
  GameInput and WinRT `CoreWindow` keyboard events (many UWP games: use `pad`); HID read directly; DirectInput
  joysticks; Steam Input; `GetRawInputBuffer` in 32-bit processes; XInput users 1-3.
- **Capture:** Vulkan only when vinput is injected before the game creates its device (`launch --early`); D3D9 and
  D3D10 are not captured natively; `PrintWindow` can be black or white for some surfaces.
- **OCR** has no confidence score and can misread stylised or very small text: use `--region`, `--scale 2` or image
  matching. Image matching does not handle rotation and finds scales within about +-20 % by default.
- **Text:** `type` posts `WM_CHAR` (right for text boxes); games that only read raw keys need `type --keys`, which does
  not apply Shift for punctuation yet.
- The game can still be brought to the front by **other** programs; while it is really in front it receives the real
  keyboard and mouse, and a cursor clip it sets applies only while it stays in front (released when it loses it).
- vinput stays loaded in a game until the game exits.
- Verified live on a set of commercial D3D11, D3D12 (UWP) and OpenGL games; **Vulkan has only been verified on the
  synthetic test target**, not in a real game. See [TESTING.md](TESTING.md).

## Repository layout

```
src/            vinput DLL (vinput_core.cpp; capture: vi_cap12.h vi_capgl.h vi_capvk.h; cursor guard, isolation, readiness,
                posted input: vi_cursor_*.inl vi_isolation_lease.inl vi_readiness.inl vi_posted_input.inl ...), CLI (vinput_cli.cpp, vi_cli_*.inl),
                anti-cheat refusal (vi_acguard.h), OCR / text / image matching (vi_ocr.h vi_text.h vi_ncc.h vi_vision.h)
test/           synthetic targets: input logger, D3D12 / GL / Vulkan / XInput targets, UI menu target, anti-cheat refusal target
verify/         PowerShell self-tests (run_selftest.ps1 runs the suite)
macros/         example .vin scripts for the synthetic UI target
mcp/            the MCP server (TypeScript)
third_party/    MinHook, Khronos Vulkan headers (with their licences)
docs/CLI.md     full CLI reference
docs/AGENT_TIPS.md   tips for LLM agents (windowed setup, click timing, drag, game notes)
docs/MCP_TODO.md     MCP follow-ups that wait for core features
```

## Compiling vinput into your own DLL

Instead of injecting `vinput64.dll`, you can link `dist\vinput_x64.lib` (or `vinput_x86.lib`) into a DLL of your own:
compile with `/DVINPUT_COMPILE_IN /I<repo>\src`, link `d3d11.lib dxgi.lib dxguid.lib windowscodecs.lib ole32.lib`,
`#include "vinput.h"` and call `VINPUT_INIT();` from your init (safe in `DllMain`; it only starts a thread). Without
the define `VINPUT_INIT()` compiles to nothing. vinput's MinHook is renamed (`VIMH_*`), so it links next to your own
MinHook, and both hook chains work. If `vinput64.dll` is also injected, the second copy is a no-op. The anti-cheat
start-up check runs in this mode too.

## Contributing, security, licence

- [CONTRIBUTING.md](CONTRIBUTING.md) - PRs welcome; every change comes with a red/green test; offline-only scope.
- [SECURITY.md](SECURITY.md) - how to report a vulnerability.
- MIT licence, copyright CorruptedAngel - see [LICENSE](LICENSE). Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Disclaimer: [DISCLAIMER.md](DISCLAIMER.md).
