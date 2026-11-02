# Third-party notices

This project (LLM Game Automation MCP / vinput) is MIT-licensed, copyright CorruptedAngel (see `LICENSE`). It
includes or depends on the components below, which keep their own licences.

## Vendored in this repository (compiled into the binaries)

| component | version | where | licence | licence file |
|---|---|---|---|---|
| **MinHook** - The Minimalistic API Hooking Library for x64/x86, Copyright (C) 2009-2017 Tsuda Kageyu | commit `8af6b4acae5a9388fd742b56fa79ece89d96f823`, unmodified | `third_party/minhook/` | BSD 2-Clause | `third_party/minhook/LICENSE.txt`, `AUTHORS.txt` |
| **Hacker Disassembler Engine 32 C / 64 C** (part of MinHook), Copyright (c) 2008-2009 Vyacheslav Patkov | as shipped with MinHook | `third_party/minhook/src/hde/` | BSD 2-Clause | `third_party/minhook/LICENSE.txt` |
| **Vulkan-Headers** (`vk_platform.h`, `vulkan.h`, `vulkan_core.h`, `vulkan_win32.h`), Copyright 2015-2026 The Khronos Group Inc. | 1.4.356, unmodified | `third_party/vulkan/` | Apache-2.0 OR MIT (used under MIT) | `third_party/vulkan/LICENSE.txt` |

`third_party/vulkan/vk_video/*.h` are placeholder stubs written for this project (not Khronos code) and are MIT under
this project's licence.

MinHook's licence requires its copyright notice, conditions and disclaimer to be reproduced in the documentation of
binary redistributions. If you redistribute `vinput*.dll` / `vinput*.exe` / `vinput_*.lib`, ship
`third_party/minhook/LICENSE.txt` (or this file together with it) alongside them.

The full MinHook / HDE licence text, reproduced from `third_party/minhook/LICENSE.txt`:

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

The Hacker Disassembler Engine 32 C and 64 C notices (Copyright (c) 2008-2009, Vyacheslav Patkov, same BSD 2-Clause
terms, with "REGENTS" in place of "COPYRIGHT HOLDER") follow in that file.

## npm dependencies of the MCP server (`mcp/`, installed by `npm ci`, not vendored)

| package | licence |
|---|---|
| `@modelcontextprotocol/sdk` 1.32.2 | MIT |
| `zod` 3.25.76 | MIT |
| transitive runtime dependencies (93 packages, see `mcp/package-lock.json`) | 83 MIT, 7 ISC (`inherits`, `isexe`, `once`, `setprototypeof`, `which`, `wrappy`, `zod-to-json-schema`), 2 BSD-3-Clause (`fast-uri`, `qs`), 1 BSD-2-Clause (`json-schema-typed`) |
| dev only: `typescript` 5.9.3, `@types/node` 24.19.2 | Apache-2.0, MIT |

Each package's licence text is in `mcp/node_modules/<package>/` after installation.

## Operating-system components (used, not redistributed)

Windows APIs used at run time - `Windows.Media.Ocr` (WinRT OCR), Windows Imaging Component, DirectX (D3D11/D3D12/DXGI),
OpenGL (`opengl32.dll`), the Vulkan loader (`vulkan-1.dll`, resolved at run time and never linked), DirectInput 8,
XInput - are part of Windows or the GPU driver and are not included in this repository.
