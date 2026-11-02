# Disclaimer

**Read this before you use LLM Game Automation MCP (vinput).**

1. **Single-player / offline games only.** This project exists to automate, test and play games that run entirely on
   your own machine.
2. **Do not use it in online or multiplayer games, or with any game protected by anti-cheat.** Injecting code into
   such a game breaks its terms of service and will very likely get your account banned. Automating other players'
   experience is not what this project is for.
3. **It injects a DLL into the target process.** `vinput.exe` loads `vinput64.dll` / `vinput32.dll` into the game and
   hooks Windows input, focus and graphics APIs inside that process. Security software may flag this, and a game can
   behave differently or crash with foreign code in it. Back up your saves first.
4. **The anti-cheat refusal is a courtesy, not a guarantee.** vinput refuses to inject when it recognises a known
   anti-cheat (EasyAntiCheat, BattlEye, Riot Vanguard, Ricochet, FACEIT, ESEA, nProtect GameGuard, XIGNCODE3,
   ACE / AntiCheatExpert, PunkBuster, mhyprot, Denuvo Anti-Cheat) loaded in the game, installed next to it, or
   running on the machine. The check is name-based and best-effort: a renamed, new or unlisted anti-cheat will not be
   detected. **It is your responsibility** to make sure a game is offline and unprotected before you point this tool
   at it. The refusal has no override flag; please do not patch it out.
5. **Use at your own risk.** The software is provided "as is", without warranty of any kind (see `LICENSE`). The
   authors and contributors are not responsible for account bans, lost or corrupted saves, data loss, crashes,
   damage to your system, violations of any terms of service, or anything else that results from using it.
6. **LLM agents act on their own.** When an AI agent drives a game through the MCP server, it can press keys and click
   things you did not anticipate (spend in-game currency, overwrite a save, quit without saving). Supervise it, and
   only give it games where such mistakes are acceptable.
