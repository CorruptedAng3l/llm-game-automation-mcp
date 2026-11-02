# Security policy

## Reporting a vulnerability

Please **do not open a public issue** for a security problem. Use GitHub's private vulnerability reporting
("Security" tab -> "Report a vulnerability") on this repository. Include what you found, how to reproduce it, and the
impact you expect. You should get an answer within a few days; please allow time for a fix before disclosing.

## What counts

- The control pipe (`\\.\pipe\vinput_<pid>`) letting another user, a lower-integrity process or a remote machine drive
  or read a game it should not.
- The CLI or the MCP server executing something other than what it was asked (argument injection, path handling).
- The MCP server exposing more than the documented tools, or file access beyond the paths it is given.
- A way to make the anti-cheat refusal pass silently for a target that the lists in `src/vi_acguard.h` do cover.

## What does not count

- That vinput injects a DLL and hooks APIs in the target process: that is what it does (see `DISCLAIMER.md`).
- That the anti-cheat refusal misses an anti-cheat it does not list: it is a best-effort courtesy, not a security
  boundary. Pull requests adding names are welcome.
- Security products flagging DLL injection.

## Design notes

- The pipe is created per game process and is reachable by the same user; vinput is a local tool for your own games
  and should not be run on a shared or multi-user machine where other users must not control your games.
- vinput never generates OS-level input (`SendInput`) except the explicit `sysinput` test command.
- The MCP server is a local stdio process; it opens no network ports.
