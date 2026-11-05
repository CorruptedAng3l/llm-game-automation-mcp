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
    ...(s.edge_margin !== undefined ? { edge_margin: s.edge_margin } : {}),
    ...(fsPolicy(s) !== undefined ? { fullscreen: fsPolicy(s) } : {}),
  };
}
/** DXGI fullscreen policy from status.fullscreen.policy (borderless / refuse / native), when the build reports it. */
function fsPolicy(s: Obj): unknown { const f = s.fullscreen as { policy?: unknown } | undefined; return f && typeof f === "object" ? f.policy : undefined; }

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
  const present = typeof s.frames === "number" ? s.frames : typeof c.present === "number" ? c.present : undefined;
  const refusals = typeof c.fs_refused === "number" ? c.fs_refused : 0;
  const nothingPresented = present === 0;
  const stuck = nothingPresented && refusals > 0;
  const d: FrameDiag = { nothingPresented, refusals, stuck };
  const d9 = s.d3d9 && typeof s.d3d9 === "object" ? s.d3d9 as Obj : undefined;
  if (nothingPresented && typeof d9?.render_warning === "string") {
    d.warning = d9.render_warning;
    return d;
  }
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

/** Aggregate drops include deliberately isolated unrecognized messages. Owned
 * delivery is separate evidence and still does not establish application action. */
export function mouseMessageHealth(s: Obj): Obj | undefined {
  const c = s.counters && typeof s.counters === "object" ? s.counters as Obj : {};
  const count = (k: string) => typeof c[k] === "number" && Number.isSafeInteger(c[k]) && (c[k] as number) >= 0 ? c[k] as number : undefined;
  const total = count("wm_mouse"), dropped = count("wm_mouse_dropped");
  if (total === undefined || dropped === undefined || dropped > total) return;
  const ratio = total ? dropped / total : 0;
  const owned = count("wm_mouse_owned"), delivered = count("wm_mouse_owned_delivered"), ownedDropped = count("wm_mouse_owned_dropped"), unrecognizedDropped = count("wm_mouse_unrecognized_dropped");
  const out: Obj = { received: total, dropped, drop_ratio: ratio };
  const known = owned !== undefined && delivered !== undefined && ownedDropped !== undefined && unrecognizedDropped !== undefined && delivered + ownedDropped <= owned && owned <= total && delivered <= total-dropped && ownedDropped <= dropped && unrecognizedDropped <= dropped && ownedDropped + unrecognizedDropped <= dropped && unrecognizedDropped <= total-owned;
  Object.assign(out, known ? { owned_received: owned, owned_delivered: delivered, owned_dropped: ownedDropped, unrecognized_dropped: unrecognizedDropped } : { owned_received: null, owned_delivered: null, owned_dropped: null, unrecognized_dropped: null });
  if (total >= 20 && ratio >= 0.5) out.warning = `Mouse message isolation suppressed ${dropped} of ${total} messages (${Math.round(ratio * 100)}%). ` +
    (known ? `${delivered} vinput-owned messages reached the window callback; ${ownedDropped} owned messages were dropped. ` : "This DLL does not separate owned and unrecognized mouse delivery. ") +
    "Unrecognized sources include real input and application-generated messages; this ratio alone does not identify why a click had no effect. Check capture and trace before retrying.";
  return out;
}

/** Completed virtual XInput returns, not key-trace reads or game acceptance. */
function padReturns(s: Obj): Obj | undefined {
  const p = s.virtual_pad;
  if (s.pad_readiness_version !== 1 || !p || typeof p !== "object" || Array.isArray(p)) return;
  const v = p as Obj;
  const counts = ["publication", "virtual_returns", "standard_returns", "extended_returns", "native_calls_bypassed", "last_return_publication"];
  const uint64 = (n: unknown) => typeof n === "string" && /^(0|[1-9][0-9]{0,19})$/.test(n) &&
    (n.length < 20 || n <= "18446744073709551615");
  if (!counts.every((k) => uint64(v[k])) || typeof v.active !== "boolean" ||
      typeof v.capabilities_virtualized !== "boolean" || typeof v.device_arrival_announced !== "boolean" ||
      v.application_ready !== null) return;
  if (v.virtual_returns === "0") {
    if (v.last_return_age_ms !== null || v.last_return_neutral !== null) return;
  } else if (typeof v.last_return_age_ms !== "number" || !Number.isSafeInteger(v.last_return_age_ms) ||
      v.last_return_age_ms < 0 || typeof v.last_return_neutral !== "boolean") return;
  const out: Obj = { active: v.active };
  for (const k of counts) out[k] = v[k];
  for (const k of ["last_return_age_ms", "last_return_neutral", "capabilities_virtualized", "device_arrival_announced", "application_ready"]) out[k] = v[k];
  return out;
}

/** Preserve typed native visibility; missing/old metadata never implies visible. */
export function hiddenWindowView(s: Obj): Obj {
  const out: Obj = {};
  if (s.hidden_window_version === 1 && typeof s.window_adopted === "boolean" &&
      (typeof s.window_hidden === "boolean" || s.window_hidden === null)) {
    out.window_adopted = s.window_adopted;
    out.window_hidden = s.window_hidden;
    if (typeof s.automatic_visibility_restoration === "boolean") out.automatic_visibility_restoration = s.automatic_visibility_restoration;
    const p = s.presentation_window;
    if (p && typeof p === "object" && !Array.isArray(p)) {
      const v = p as Obj;
      const flags = ["ambiguous", "capacity_unconfirmed", "evidence_unconfirmed", "candidates_truncated"];
      if (v.version === 1 && typeof v.hidden_candidate_count === "number" && Number.isSafeInteger(v.hidden_candidate_count) &&
          v.hidden_candidate_count >= 0 && v.hidden_candidate_count <= 128 && flags.every(k => typeof v[k] === "boolean"))
        out.presentation_window = Object.fromEntries(["hidden_candidate_count", ...flags].map(k => [k, v[k]]));
    }
  }
  const h = s.hidden_show;
  if (s.show_hidden_version === 1 && h && typeof h === "object" && !Array.isArray(h)) {
    const v = h as Obj;
    const flags = ["pending", "completed", "call_attempted", "visible_after"];
    const decimal = (value: unknown) => typeof value === "string" && /^(0|[1-9][0-9]{0,19})$/.test(value);
    if (decimal(v.ticket) && decimal(v.adoption_generation) && typeof v.hwnd === "string" && /^0x[0-9a-f]{1,16}$/i.test(v.hwnd) &&
        flags.every(k => typeof v[k] === "boolean") && !(v.pending === true && v.completed === true) &&
        typeof v.reason === "string" && v.reason.length <= 128 && typeof v.expires_in_ms === "number" &&
        Number.isSafeInteger(v.expires_in_ms) && v.expires_in_ms >= 0 && v.expires_in_ms <= 5000)
      out.hidden_show = Object.fromEntries(["ticket", "hwnd", "adoption_generation", ...flags, "reason", "expires_in_ms"].map(k => [k, v[k]]));
  }
  return out;
}

/** The essentials of a status reply (about 0.5 KB instead of 3 KB) plus warnings; `verbose` on the tool gives the raw reply. */
export function compactStatus(s: Obj): Obj {
  const c = (s.counters ?? {}) as Record<string, unknown>;
  const nonzero: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(c)) if (typeof v === "number" && v !== 0) nonzero[k] = v;
  const warnings: string[] = [];
  const hidden = hiddenWindowView(s);
  const d = diagnoseFrames(s);
  if (d.warning) warnings.push(d.warning);
  else if (d.note && d.refusals > 0) warnings.push(d.note);
  if (s.hwnd === "0x0") warnings.push(hidden.window_hidden === true ? "A hidden-window observation exists, but no game window is adopted yet. Check presentation evidence and launch diagnostics; capture and mouse input still need a protected adopted window." : "No game window has been adopted yet (hwnd 0x0): the game may still be starting, or it has no top-level window. Wait and check status again; capture and mouse input need the window.");
  if (hidden.window_adopted === true && hidden.window_hidden === true) warnings.push("The adopted game window is hidden. Visibility changes require an explicit show_hidden launch option; adoption and Present evidence do not prove application input readiness.");
  if (s.app_active === 0 && s.really_foreground === false)
    warnings.push(`app_active is 0 while the game is in the background: the game believes it is inactive and may ignore input (old DirectDraw-era games often do). ${s.autoactivate === false ? "autoactivate is OFF: set config autoactivate=true. " : "autoactivate is on, so this should clear shortly after the first input. "}If it persists, check the game's own background-input settings.`);
  if (s.minimized === true) warnings.push("The game window is minimized; minimized games stop rendering, so captures go stale. Restore it (keepvisible=true keeps it visible).");
  if (typeof s.hooks_failed === "string" && s.hooks_failed) warnings.push(`Some hooks failed to install: ${s.hooks_failed}. Related input or capture channels may not work.`);
  const mouse = s.directinput_mouse && typeof s.directinput_mouse === "object" ? s.directinput_mouse as Obj : undefined;
  if (typeof mouse?.warning === "string") warnings.push(mouse.warning);
  const messages = mouseMessageHealth(s);
  if (typeof messages?.warning === "string") warnings.push(messages.warning);
  if (s.anticheat_disabled === true) warnings.push("An anti-cheat module was detected after injection: vinput refuses input and capture commands for this game.");
  const claim = s.claim as { active?: boolean; owner?: string } | undefined;
  const out: Obj = {
    ok: s.ok, pid: s.pid, hwnd: s.hwnd, title: s.title, ...hidden,
    client: { w: s.client_w, h: s.client_h },
    really_foreground: s.really_foreground, app_active: s.app_active,
    present: c.present, frames: s.frames, capture_path: s.capture_path, frame_api: s.frame_api,
    cursor: name(CURSOR_NAMES, s.cursor_mode) ?? s.cursor_mode,
    raw: name(RAW_NAMES, s.rawmode) ?? s.rawmode, msgs: name(MSGS_NAMES, s.msgmode) ?? s.msgmode,
    counters: nonzero,
  };
  // Native published VK state, including mouse-button VKs; absent/malformed is unknown.
  if (Array.isArray(s.virtual_down) && s.virtual_down.every((v) => typeof v === "number" && Number.isInteger(v) && v >= 1 && v <= 255))
    out.virtual_down = [...s.virtual_down];
  const pad = padReturns(s);
  if (pad) out.virtual_pad = pad;
  if (claim?.active) out.claimed_by = claim.owner;
  // the owner (human) is using the game window: real input passes through and the virtual cursor stands down until the next agent command
  const cursorGuard = s.cursor_guard && typeof s.cursor_guard === "object" && !Array.isArray(s.cursor_guard) ? s.cursor_guard as Obj : undefined;
  if (typeof cursorGuard?.cursor_effective === "string") out.cursor_effective = cursorGuard.cursor_effective;
  else if (typeof s.cursor_effective === "string") out.cursor_effective = s.cursor_effective;
  if (s.owner_active === true || cursorGuard?.owner_active === true) {
    out.owner_active = true;
    warnings.push("The user is using the game window right now (owner_active): their real mouse and keyboard reach the game and the cursor the game reads is the real one, until your next input command. Expect the picture to change under you; look before you act.");
  }
  // what this game's vinput DLL can do (older DLLs report none of these)
  const caps: Obj = {};
  if (typeof s.capture_output_version === "number") caps.capture_output_version = s.capture_output_version;
  if (s.hidden_window_version === 1) caps.hidden_window_version = 1;
  if (s.show_hidden_version === 1) caps.show_hidden_version = 1;
  if (s.input_precision && typeof s.input_precision === "object") Object.assign(caps, s.input_precision);
  if (Object.keys(caps).length) out.capabilities = caps;
  if (s.edge_contact === true) { out.edge_contact = true; out.edge_sides = s.edge_sides;
    warnings.push("The virtual cursor rests on the window edge (edge_contact): games with edge scrolling pan the view while it stays there. Move it inside, or use park_cursor."); }
  if (fsPolicy(s) !== undefined) out.fullscreen = s.fullscreen;
  if (s.d3d9 && typeof s.d3d9 === "object") {
    const d9 = s.d3d9 as Obj;
    if (d9.hooked || d9.present_hooked || d9.render_warning) out.d3d9 = Object.fromEntries(
      ["hooked", "present_hooked", "present_attempts", "presented", "last_present_hresult", "device_lost_returns", "capture_failures", "late_probe_attempted", "late_probe_9_hresult", "late_probe_ex_hresult", "render_warning"]
        .filter(k => d9[k] !== undefined).map(k => [k, d9[k]]));
  }
  if (mouse) out.directinput_mouse = mouse;
  if (messages) out.mouse_messages = messages;
  out.warnings = warnings;
  return out;
}
