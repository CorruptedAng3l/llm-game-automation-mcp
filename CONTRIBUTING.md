# Contributing

Pull requests are welcome: bug fixes, new input or capture paths, better OCR / matching, MCP tools, docs.

## Scope: offline games only

This project is for single-player / offline games. Contributions that help bypass, hide from, or work around
anti-cheat systems, that target online or multiplayer games, or that weaken or remove the anti-cheat refusal
(`src/vi_acguard.h`) will not be accepted. Adding more anti-cheat names to the refusal lists is very welcome.

## Every change comes with a red/green test

- Add or extend a test that **fails on the code before your change and passes after it** (a red/green pair), and say
  in the PR how you saw it red - for example by running the test against a build of `main` with `-CliDir`.
- Tests use the **synthetic targets** in `test/` (they log what they receive, so the test checks the target's view,
  not only vinput's reply). Real games cannot be part of the automated suite; if you verified something in a real game,
  describe it in the PR (game, API, what you checked), but keep game-specific files out of the repository.
- Clean up: tests stop only the processes they started, by PID.
- Run before you open the PR:

  ```bat
  build.bat                                   &rem x64 + x86, must build with 0 warnings
  pwsh -File verify\run_selftest.ps1          &rem full suite, PowerShell 7
  cd mcp && npm ci && npm run build && npm test && npm run smoke
  ```

## Style

- C++17 / C++20 with MSVC, `/W3` clean, no new external dependencies without discussion (vendored code goes into
  `third_party/` with its licence and an entry in `THIRD_PARTY_NOTICES.md`).
- Every CLI command answers one JSON line with `ok`; errors are explicit, never silent.
- Keep the game's own threads fast: no blocking waits in hooks on the present path.
- Documentation changes go with the code change (`README.md`, `docs/CLI.md`, `mcp/README.md`).

## Reporting bugs

Use the issue templates. Include the vinput reply JSON, `status` output, the DLL log (`dist\run\logs\vinput_<pid>.log`)
and the graphics API / engine of the game. Do not report problems with online or anti-cheat protected games.
