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
