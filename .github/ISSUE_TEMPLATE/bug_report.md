---
name: Bug report
about: Something does not work as documented
title: "[bug] "
labels: bug
---

<!-- Offline / single-player games only. Issues about online, multiplayer or anti-cheat protected games will be closed. -->

**What happened**
A clear description, and what you expected instead.

**How to reproduce**
1. Command(s) or MCP tool call(s), exactly as sent:
2. The JSON reply you got:

**Environment**
- Windows version (e.g. Windows 11 23H2):
- GPU and driver:
- vinput commit / release:
- CLI or MCP server (and MCP client):
- Game graphics API / engine if known (D3D11, D3D12, OpenGL, Vulkan; Unity, Unreal, ...), 32- or 64-bit, windowed or borderless:
- Is the game offline / single-player with no anti-cheat? (yes / no)

**Diagnostics**
- `vinput --pid N status` output:
- The DLL log (`dist\run\logs\vinput_<pid>.log`), relevant part:

**Can it be reproduced with a synthetic target from `test/`?**
If yes, which one and how.
