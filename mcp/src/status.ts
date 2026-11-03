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
