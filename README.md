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
- **You keep your PC.** vinput never generates OS-level input (no `SendInput`); your real typing is hidden from a
  background game, and the game cannot steal the foreground from you.
- **Several games at once.** Every game has its own pipe and command queue, so several agents can drive several
  games in parallel, each with an optional ownership claim.
- **Pixels from the game itself.** Captures come from the frame the game presents (not a desktop screenshot), so they
  work while the window is covered by other windows.

## Supported APIs

| | supported | notes |
|---|---|---|
| **Capture** | Direct3D 11, Direct3D 12, OpenGL, Vulkan, `PrintWindow` fallback | HDR formats (R10G10B10A2, RGBA16F) converted to 8-bit; Vulkan needs early injection (`launch --early`); D3D9/D3D10 fall back to `PrintWindow` |
| **Keyboard** | window messages (`WM_KEYDOWN`/`WM_CHAR`, with the game's own `TranslateMessage`), Raw Input (`WM_INPUT`, `GetRawInputBuffer` on x64), DirectInput 8, `GetAsyncKeyState`/`GetKeyState`/`GetKeyboardState` | mirrors the game's own `RegisterRawInputDevices` choices (e.g. `RIDEV_NOLEGACY`) |
| **Mouse** | messages, Raw Input, DirectInput 8, virtual cursor behind `GetCursorPos` / `WindowFromPoint` | absolute (`click`, `move`) and relative (`move_relative`, mouse-look) |
| **Gamepad** | XInput pad 0 (`XInputGetState`, `XInputGetStateEx`) | buttons, both sticks, triggers |
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
[docs/CLI.md](docs/CLI.md).

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
| no focus stealing | while the game is not really in front, `SetForegroundWindow` / `BringWindowToTop` / `SetActiveWindow` are no-ops, `SetWindowPos` gets `SWP_NOACTIVATE`, showing the window does not activate it, `ClipCursor` / `SetCursorPos` never touch the real cursor, DXGI exclusive fullscreen is refused; every attempt is logged |
| virtual input | each event is delivered on every channel the game can read, as a real key would be (see Supported APIs) |
| isolation | while the game is not really in front, the real keyboard and mouse are hidden from it, so your typing elsewhere does not leak into it; when it is in front, real input works and virtual input is added on top |
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

- **Exclusive fullscreen** is not supported (run windowed / borderless); a **minimised** game stops rendering (vinput
  puts a background game back unless `config keepvisible=false`).
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
  keyboard.
- vinput stays loaded in a game until the game exits.
- Verified live on a set of commercial D3D11, D3D12 (UWP) and OpenGL games; **Vulkan has only been verified on the
  synthetic test target**, not in a real game. See [TESTING.md](TESTING.md).

## Repository layout

```
src/            vinput DLL (vinput_core.cpp; capture: vi_cap12.h vi_capgl.h vi_capvk.h), CLI (vinput_cli.cpp, vi_cli_*.inl),
                anti-cheat refusal (vi_acguard.h), OCR / text / image matching (vi_ocr.h vi_text.h vi_ncc.h vi_vision.h)
test/           synthetic targets: input logger, D3D12 / GL / Vulkan / XInput targets, UI menu target, anti-cheat refusal target
verify/         PowerShell self-tests (run_selftest.ps1 runs the suite)
macros/         example .vin scripts for the synthetic UI target
mcp/            the MCP server (TypeScript)
third_party/    MinHook, Khronos Vulkan headers (with their licences)
docs/CLI.md     full CLI reference
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
