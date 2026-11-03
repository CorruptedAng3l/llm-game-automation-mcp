// Views over vinput's `status` reply (vinput_core.cpp StatusJson): names instead of enum numbers.
type Obj = Record<string, unknown>;

export const CURSOR_NAMES = ["auto", "virtual", "real"] as const;
export const RAW_NAMES = ["auto", "post", "buffer", "off"] as const;
export const MSGS_NAMES = ["auto", "on", "off"] as const;

const name = (names: readonly string[], v: unknown) => (typeof v === "number" && names[v] !== undefined ? names[v] : undefined);

/** The effective vinput config as the `config` command's enums/booleans, read from a status / config reply. */
export function configView(s: Obj): Obj {
  return {
    fakefocus: s.fakefocus, autoactivate: s.autoactivate, isolate: s.isolate, blockfocus: s.blockfocus,
    nofullscreen: s.nofullscreen, keepvisible: s.keepvisible,
    cursor: name(CURSOR_NAMES, s.cursor_mode) ?? s.cursor_mode,
    raw: name(RAW_NAMES, s.rawmode) ?? s.rawmode,
    msgs: name(MSGS_NAMES, s.msgmode) ?? s.msgmode,
  };
}

/** Strip vinput's "[ 12.345] " time prefix so repeated messages compare equal. */
const stripStamp = (l: string) => l.replace(/^\[\s*[\d.]+\]\s*/, "");

/** Keep the lines containing `contains`, collapse runs of identical messages into "<line> (repeated N times)", tail `n`. */
export function tailLog(all: string[], n: number, contains?: string): { lines: string[]; matched: number } {
  const needle = contains?.toLowerCase();
  const sel = needle ? all.filter((l) => l.toLowerCase().includes(needle)) : all;
  const out: string[] = [];
  for (let i = 0; i < sel.length;) {
    let j = i + 1;
    while (j < sel.length && stripStamp(sel[j]) === stripStamp(sel[i])) j++;
    const run = j - i;
    out.push(run > 1 ? `${sel[j - 1]} (repeated ${run} times, first at ${sel[i].match(/^\[\s*([\d.]+)\]/)?.[1] ?? "?"})` : sel[i]);
    i = j;
  }
  return { lines: out.slice(-n), matched: sel.length };
}

export interface FrameDiag {
  nothingPresented: boolean;
  refusals: number;
  /** a refusal loop with nothing presented: the game is stuck trying to go exclusive fullscreen */
  stuck: boolean;
  warning?: string;
  note?: string;
}

const FIX = "Fix: set the game to windowed or borderless in its own settings or INI (a saved fullscreen setting overrides " +
  "command-line switches), then relaunch it.";

/** Read frame health out of a status reply: nothing presented, and exclusive-fullscreen refusals piling up. */
export function diagnoseFrames(s: Obj): FrameDiag {
  const c = (s.counters ?? {}) as Record<string, unknown>;
  const present = typeof c.present === "number" ? c.present : undefined;
  const refusals = typeof c.fs_refused === "number" ? c.fs_refused : 0;
  const nothingPresented = present === 0;
  const stuck = nothingPresented && refusals > 0;
  const d: FrameDiag = { nothingPresented, refusals, stuck };
  if (stuck) {
    d.warning = `The game has not presented a single frame and tried to enter exclusive fullscreen ${refusals} time(s); vinput refuses ` +
      `that because exclusive fullscreen cannot be captured or driven in the background, so the game is stuck retrying. ${FIX} ` +
      `(log with contains "fullscreen" shows the refusals.)`;
  } else if (refusals > 0) {
    d.note = `The game asked for exclusive fullscreen ${refusals} time(s) (refused). It still presents frames, but ${FIX.replace("Fix: s", "consider s")}`;
  } else if (nothingPresented) {
    d.note = "No Direct3D / OpenGL / Vulkan frame has been presented yet (present=0). The game may still be loading, or it draws " +
      "with DirectDraw / GDI, which capture reads through PrintWindow and is fine. If capture stays black, see log and status.";
  }
  return d;
}

/** The essentials of a status reply (about 0.5 KB instead of 3 KB) plus warnings; `verbose` on the tool gives the raw reply. */
export function compactStatus(s: Obj): Obj {
  const c = (s.counters ?? {}) as Record<string, unknown>;
  const nonzero: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(c)) if (typeof v === "number" && v !== 0) nonzero[k] = v;
  const warnings: string[] = [];
  const d = diagnoseFrames(s);
  if (d.warning) warnings.push(d.warning);
  else if (d.note && d.refusals > 0) warnings.push(d.note);
  if (s.hwnd === "0x0") warnings.push("No game window has been adopted yet (hwnd 0x0): the game may still be starting, or it has no top-level window. Wait and check status again; capture and mouse input need the window.");
  if (s.app_active === 0 && s.really_foreground === false)
    warnings.push(`app_active is 0 while the game is in the background: the game believes it is inactive and may ignore input (old DirectDraw-era games often do). ${s.autoactivate === false ? "autoactivate is OFF: set config autoactivate=true. " : "autoactivate is on, so this should clear shortly after the first input. "}If it persists, check the game's own background-input settings.`);
  if (s.minimized === true) warnings.push("The game window is minimized; minimized games stop rendering, so captures go stale. Restore it (keepvisible=true keeps it visible).");
  if (typeof s.hooks_failed === "string" && s.hooks_failed) warnings.push(`Some hooks failed to install: ${s.hooks_failed}. Related input or capture channels may not work.`);
  if (s.anticheat_disabled === true) warnings.push("An anti-cheat module was detected after injection: vinput refuses input and capture commands for this game.");
  const claim = s.claim as { active?: boolean; owner?: string } | undefined;
  const out: Obj = {
    ok: s.ok, pid: s.pid, hwnd: s.hwnd, title: s.title,
    client: { w: s.client_w, h: s.client_h },
    really_foreground: s.really_foreground, app_active: s.app_active,
    present: c.present, frames: s.frames, capture_path: s.capture_path, frame_api: s.frame_api,
    cursor: name(CURSOR_NAMES, s.cursor_mode) ?? s.cursor_mode,
    raw: name(RAW_NAMES, s.rawmode) ?? s.rawmode, msgs: name(MSGS_NAMES, s.msgmode) ?? s.msgmode,
    counters: nonzero,
  };
  if (claim?.active) out.claimed_by = claim.owner;
  out.warnings = warnings;
  return out;
}
