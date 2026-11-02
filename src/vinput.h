// vinput.h - per-process virtual input + frame capture for automating single-player / offline games.
// Offline games only; see DISCLAIMER.md. vinput refuses to run next to a known anti-cheat (vi_acguard.h).
//
// Two ways to use it:
//   (a) RECOMMENDED: inject dist/vinput64.dll / vinput32.dll with `vinput.exe inject --pid N` (or
//       `vinput.exe launch ...`). Nothing in your own code changes.
//   (b) Compiled into your own DLL behind VINPUT_COMPILE_IN: link vinput_<arch>.lib (built by build.bat) and call
//       VINPUT_INIT() from your init. With VINPUT_COMPILE_IN undefined VINPUT_INIT() compiles to nothing, and if
//       you also leave the .lib out of the link nothing of vinput ships.
//
// Both paths are guarded per process: whichever initialises first wins, the other is a no-op.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Starts vinput in the current process. Idempotent; never blocks (spawns a worker thread), so it is
// safe to call from DllMain. Returns 1 if this call started it, 0 if it was already running.
int VInput_Init(void);

// Version string, e.g. "vinput 1.0".
const char* VInput_Version(void);

#ifdef __cplusplus
}
#endif

#if defined(VINPUT_COMPILE_IN)
#define VINPUT_INIT() VInput_Init()
#else
#define VINPUT_INIT() ((void)0)
#endif
