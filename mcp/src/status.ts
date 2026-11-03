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
