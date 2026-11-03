// Size arguments a game was launched with (Unity, Unreal, id Tech, old DirectDraw titles) versus the window it really got.
// Many games ignore these switches because a saved setting (registry / INI) wins, so launch compares and warns.

/** The client size asked for on a game command line, or undefined when no (complete) size argument is present. */
export function requestedSize(args: string | undefined): { w: number; h: number; from: string } | undefined {
  if (!args) return undefined;
  const t = args.trim().split(/\s+/);
  const num = (v: string | undefined) => (v !== undefined && /^\d{2,5}$/.test(v) ? Number(v) : undefined);
  const find = (names: string[]): number | undefined => {
    for (let i = 0; i < t.length; i++) {
      const [k, ...rest] = t[i].replace(/^[-/]+/, "").split(/[=:]/);
      if (!names.includes(k.toLowerCase())) continue;
      const v = rest.length ? num(rest.join("=")) : num(t[i + 1]);
      if (v !== undefined) return v;
    }
    return undefined;
  };
  let m: RegExpExecArray | null;
  for (const tok of t) if ((m = /^[-/]?(?:r|res|resolution)[=:]?(\d{3,5})x(\d{3,5})$/i.exec(tok))) return { w: Number(m[1]), h: Number(m[2]), from: tok };
  for (let i = 0; i + 1 < t.length; i++) if (/^[-/](?:r|res|resolution)$/i.test(t[i]) && (m = /^(\d{3,5})x(\d{3,5})$/i.exec(t[i + 1]))) return { w: Number(m[1]), h: Number(m[2]), from: `${t[i]} ${t[i + 1]}` };
  const pairs: [string[], string[], string][] = [
    [["screen-width"], ["screen-height"], "-screen-width / -screen-height"],
    [["width"], ["height"], "-width / -height"],
    [["resx", "windowedwidth"], ["resy", "windowedheight"], "-ResX / -ResY"],
    [["w"], ["h"], "-w / -h"],
  ];
  for (const [wn, hn, from] of pairs) {
    const w = find(wn), h = find(hn);
    if (w !== undefined && h !== undefined) return { w, h, from };
  }
  return undefined;
}

/** A warning when the window's client size differs from the requested size; undefined when it matches or nothing was asked. */
export function sizeMismatchWarning(args: string | undefined, client: { w?: unknown; h?: unknown } | undefined): string | undefined {
  const want = requestedSize(args);
  if (!want || !client || typeof client.w !== "number" || typeof client.h !== "number" || client.w <= 0 || client.h <= 0) return undefined;
  if (client.w === want.w && client.h === want.h) return undefined;
  return `requested_size_ignored: launched with ${want.from} = ${want.w}x${want.h} but the game's client area is ${client.w}x${client.h}. ` +
    "The game ignored the size arguments (a saved setting usually overrides them). Set the window size in the game's own options or INI, " +
    "then re-read status client; every coordinate depends on this size.";
}
