#!/usr/bin/env node
// llm-game-automation-mcp: an MCP (stdio) server that lets an LLM agent play single-player / offline Windows games
// through vinput. Every tool runs the vinput CLI and returns its JSON reply verbatim, with "ok" surfaced as isError.
// Offline games only: vinput injects a DLL and refuses targets with a known anti-cheat (see DISCLAIMER.md).
import { sizeMismatchWarning } from "./launchsize.js";
import { closeSync, fstatSync, mkdtempSync, openSync, readFileSync, readSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { drawGrid, resample, toClient, type Mapping } from "./imgtools.js";
import { decodePng, encodePng } from "./png.js";
import { compactStatus, configView, diagnoseFrames, tailLog } from "./status.js";
import { confirmExit, exitMessage, forgetExit, knownExit, watchTarget } from "./exit.js";
import { CliResult, defaultCapturePath, resolveVinputExe, runVinput } from "./vinput.js";

const VERSION = "0.1.0";
const OWNER = (process.env.VINPUT_OWNER ?? "").trim() || `mcp-${process.pid}`;
const MAX_IMAGE_BYTES = Number(process.env.VINPUT_MCP_MAX_IMAGE_BYTES ?? 8 * 1024 * 1024);

type Content = { type: "text"; text: string } | { type: "image"; data: string; mimeType: string };
type ToolReply = { content: Content[]; isError?: boolean };

function reply(r: CliResult, extra?: Record<string, unknown>): ToolReply {
  let json = r.json;
  if (extra && json && typeof json === "object" && !Array.isArray(json)) json = { ...(json as object), ...extra };
  const content: Content[] = [{ type: "text", text: JSON.stringify(json) }];
  if (!r.ok && r.stderr) content.push({ type: "text", text: `vinput stderr:\n${r.stderr.slice(-4000)}` });
  return { content, isError: !r.ok };
}
/** last `bytes` of a text file as lines (a cut first line is dropped) */
function readTail(path: string, bytes: number): string[] {
  const fd = openSync(path, "r");
  try {
    const size = fstatSync(fd).size, start = Math.max(0, size - bytes), buf = Buffer.alloc(size - start);
    readSync(fd, buf, 0, buf.length, start);
    const lines = buf.toString("utf8").split(String.fromCharCode(10)).map((l) => l.split(String.fromCharCode(13)).join("")).filter((l) => l.length);
    return start > 0 ? lines.slice(1) : lines;
  } finally { closeSync(fd); }
}
/** The game process has ended: say so plainly instead of the raw pipe error. */
function exitedReply(pid: number, code: number | null): ToolReply {
  const error = exitMessage(pid, code);
  return { content: [{ type: "text", text: JSON.stringify({ ok: false, error, target_exited: true, pid, exit_code: code }) }], isError: true };
}
/** Extra reply fields describing frame health, from one status call ({} when status cannot be read). */
async function frameHealth(pid: number, launchArgs?: string): Promise<Record<string, unknown>> {
  const st = await runVinput([...at(pid), "status"], 15000);
  if (!st.ok || typeof st.json !== "object" || !st.json) return {};
  const d = diagnoseFrames(st.json as Record<string, unknown>);
  const out: Record<string, unknown> = {};
  if (d.nothingPresented) out.nothing_presented = true;
  if (d.refusals) out.fullscreen_refusals = d.refusals;
  const warnings: string[] = d.warning ? [d.warning] : [];
  const sj = st.json as { client_w?: unknown; client_h?: unknown };
  const sizeWarn = sizeMismatchWarning(launchArgs, { w: sj.client_w, h: sj.client_h });
  if (sizeWarn) { warnings.push(sizeWarn); out.requested_size_ignored = true; }
  if (warnings.length) out.warnings = warnings;
  if (d.note) out.notes = [d.note];
  return out;
}
function fail(message: string): ToolReply {
  return { content: [{ type: "text", text: JSON.stringify({ ok: false, error: message }) }], isError: true };
}
/** The CLI refused an option because the injected DLL is older than the CLI (it says so before sending any input). */
const lacksSupport = (r: CliResult) => !r.ok && /\blacks\b/i.test(String((r.json as { error?: unknown })?.error ?? ""));
/** A placement drag that failed BEFORE the pointer moved because the game never READ the button (Unity and other games that read
 *  buttons through window messages, not DirectInput). Nothing was dragged, so the plain drag is the safe retry. */
const placementPressNotSeen = (r: CliResult): string | undefined =>
  r.ok ? undefined : /button_down_not_observed|press_position_not_observed|start_position_not_observed/.exec(JSON.stringify(r.json ?? {}))?.[0];
/** Polled-click / release / edge evidence from one vinput reply, as a small summary (undefined when the build reports none). */
function inputEvidence(j: unknown): Record<string, unknown> | undefined {
  if (!j || typeof j !== "object") return undefined;
  const s = j as Record<string, unknown>;
  const keys = ["di_poll_mode", "di_poll_confirmed", "di_poll_timed_out", "di_poll_warning", "di_release_confirmed", "di_release_timed_out",
    "di_release_warning", "di_press_release_confirmed", "edge_contact", "edge_sides", "edge_guard_applied", "edge_guard_bypassed"];
  const out: Record<string, unknown> = {};
  for (const k of keys) if (s[k] !== undefined) out[k] = s[k];
  return Object.keys(out).length ? out : undefined;
}

/** argv prefix for a command aimed at one game process */
const at = (pid: number) => ["--owner", OWNER, "--pid", String(pid)];
const region = (r?: { x: number; y: number; w: number; h: number }) => (r ? ["--region", `${r.x},${r.y},${r.w},${r.h}`] : []);
const opt = (flag: string, v: unknown) => (v === undefined || v === null || v === false ? [] : v === true ? [flag] : [flag, String(v)]);

const pid = z.number().int().positive().describe("Process id of the game, as returned by launch / attach / list_targets.");
const regionSchema = z.object({
  x: z.number().int().min(0), y: z.number().int().min(0), w: z.number().int().positive(), h: z.number().int().positive(),
}).describe("Rectangle in client pixels (same as capture pixels): x, y = top-left corner, w, h = size.");
const button = z.enum(["left", "right", "middle", "x1", "x2"]);
const timeoutMs = (def: number) => z.number().int().min(0).max(600000).optional().describe(`How long to keep looking, in ms (default ${def}).`);

const server = new McpServer(
  { name: "llm-game-automation-mcp", version: VERSION },
  {
    instructions:
      "Plays single-player / offline Windows games through vinput: input and screenshots go to ONE game process in the " +
      "background, so the game never needs focus and the user keeps the mouse and keyboard. Workflow: launch (or attach) -> " +
      "capture to look -> act (click_text / click / key / pad) -> verify with wait_text, capture or wait_change. " +
      "Coordinates are the game's client pixels, which equal the pixels of a full capture. ok:true on an input tool only " +
      "means the input was delivered, not that the game reacted: always verify. capture_after:true requests a following " +
      "image; a zero-Present PrintWindow fallback does not prove a new rendered frame. Use capture_after:0 to look immediately. " +
      "Do not guess pixels from a big screenshot: capture with grid to read exact coordinates, or " +
      "max_width / zoom and then click with coord_space:\"capture\". Run games windowed or borderless; if launch / capture " +
      "warns about fullscreen, fix the game's own settings. status shows warnings; log, trace and config diagnose input problems. Never use it on online / multiplayer games " +
      "or games with anti-cheat; vinput refuses targets with a known anti-cheat and that refusal cannot be overridden.",
  },
);

function tool<S extends z.ZodRawShape>(name: string, title: string, description: string, shape: S,
  run: (a: z.objectOutputType<S, z.ZodTypeAny>) => Promise<ToolReply>, readOnly = false) {
  server.registerTool(name, { title, description, inputSchema: shape, annotations: { title, readOnlyHint: readOnly, openWorldHint: false } },
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (async (a: any) => {
      const p: number | undefined = typeof a?.pid === "number" ? a.pid : undefined;
      try {
        if (p !== undefined) {
          watchTarget(p);
          if (name === "attach" || name === "detach") advanceCaptureSession(p);
          const k = name === "attach" ? undefined : knownExit(p);   // attach may be a reused pid number
          if (k) return exitedReply(p, k.code);
        }
        const out = await run(a);
        if (p !== undefined && out.isError) {
          const ex = await confirmExit(p);
          if (ex) return exitedReply(p, ex.code);
        }
        return out;
      } catch (e) { return fail(`${name}: ${(e as Error).message}`); }
    }) as never);
}

// ---------------------------------------------------------------- targets
tool("launch", "Launch a game",
  "Start a game and inject vinput into it. Returns {ok, pid, injected, foreground_restored, ...}; keep the pid for every " +
  "other tool. Give exactly one of exe (a .exe path), aumid (a UWP / Xbox app id 'PackageFamilyName!AppId') or " +
  "steam_appid (+ wait_exe). Run games WINDOWED or borderless (exclusive fullscreen breaks background capture). Use " +
  "early:true for Vulkan / OpenGL games so capture hooks are in before the device is created (exe only). Refused with " +
  "anticheat_refused:true if the game or the machine has a known anti-cheat.",
  {
    exe: z.string().optional().describe("Full path of the game executable."),
    args: z.string().optional().describe("Command line arguments for the game, e.g. \"-windowed -width 1280 -height 720\". Many games ignore size switches because a saved setting wins: launch then warns (warnings, requested_size_ignored) when the window's client size differs from the size you asked for, so set the size in the game's own options."),
    cwd: z.string().optional().describe("Working directory for the game (exe launches only). Default: the exe's own folder, which is what most old games need, so leave it out unless the game wants something else."),
    aumid: z.string().optional().describe("UWP / Xbox app user model id, e.g. 'Publisher.Game_abc123!App'."),
    steam_appid: z.string().optional().describe("Steam app id; starts the game through Steam (needs wait_exe)."),
    wait_exe: z.string().optional().describe("Executable name to wait for, e.g. 'Game-Win64-Shipping.exe' (launchers, Steam relaunches)."),
    early: z.boolean().optional().describe("Create the process suspended and inject before its first instruction (exe only)."),
    settle_s: z.number().min(0).max(600).optional().describe("Seconds to let the game settle after injection (default 5)."),
    timeout_s: z.number().min(1).max(1800).optional().describe("Seconds to wait for the game window (default 240)."),
    lock: z.string().optional().describe("Optional lock file shared by tools that may touch the real foreground; held only while launching."),
    claim: z.boolean().optional().describe("Claim the game for this MCP server so other vinput clients are refused (default false)."),
  },
  async (a) => {
    const n = [a.exe, a.aumid, a.steam_appid].filter((v) => v).length;
    if (n !== 1) return fail("give exactly one of exe, aumid or steam_appid");
    let cwd: string | undefined;
    if (a.cwd) {
      if (!a.exe) return fail("cwd only applies to exe launches (aumid / steam_appid games choose their own folder)");
      cwd = resolve(a.cwd);
      let isDir = false; try { isDir = statSync(cwd).isDirectory(); } catch { /* not there */ }
      if (!isDir) return fail(`cwd does not exist or is not a folder: ${cwd}`);
    }
    const argv = ["--owner", OWNER, "launch",
      ...opt("--exe", a.exe), ...opt("--aumid", a.aumid), ...opt("--steam", a.steam_appid), ...opt("--wait-exe", a.wait_exe),
      ...(a.args === undefined ? [] : [`--args=${a.args}`]), ...opt("--cwd", cwd), ...opt("--early", a.early), ...opt("--settle", a.settle_s), ...opt("--timeout", a.timeout_s),
      ...opt("--lock", a.lock)];
    const r = await runVinput(argv, ((a.timeout_s ?? 240) + (a.settle_s ?? 5) + 120) * 1000);
    { const np = (r.json as { pid?: unknown }).pid; if (typeof np === "number" && np > 0) { advanceCaptureSession(np); forgetExit(np); watchTarget(np); } }
    const lp = r.ok ? (r.json as { pid?: number }).pid : undefined;
    const extra: Record<string, unknown> = {};
    if (lp && a.claim) { const c = await runVinput([...at(lp), "claim", "--ttl", "3600"], 15000); extra.claim = c.json; }
    if (lp) Object.assign(extra, await frameHealth(lp, a.args));
    return reply(r, extra);
  });

tool("attach", "Attach to a running game",
  "Inject vinput into an already running game by process id (32- and 64-bit). Returns the game's status. Refused with " +
  "anticheat_refused:true if the game or the machine has a known anti-cheat. Use list_targets to see games that already have vinput.",
  { pid, claim: z.boolean().optional().describe("Claim the game for this MCP server (default false).") },
  async (a) => {
    forgetExit(a.pid);
    const r = await runVinput(["inject", "--pid", String(a.pid)], 60000);
    if (!r.ok) return reply(r);
    if (a.claim) await runVinput([...at(a.pid), "claim", "--ttl", "3600"], 15000);
    for (let i = 0; i < 50; i++) {
      const s = await runVinput([...at(a.pid), "status"], 15000);
      const hwnd = (s.json as { hwnd?: string }).hwnd;
      if (!s.ok || (hwnd && hwnd !== "0x0") || i === 49) return statusReply(s, false, { injected: true });
      await new Promise((res) => setTimeout(res, 200));
    }
    return fail("unreachable");
  });

const bool = (what: string) => z.boolean().optional().describe(what);
tool("config", "Configure vinput for a game",
  "Change (or, with no settings, just read) how vinput treats one game, and return the config now in effect. Use it when the " +
  "game ignores input or focus tricks misbehave: raw=post / buffer for games that never see WM_INPUT, cursor=real for games " +
  "that read the real cursor, msgs=on for games that need window messages, nofullscreen=false to allow exclusive fullscreen. " +
  "Check 'warnings' in the reply: it lists any requested value that did not take effect.",
  {
    pid,
    fakefocus: bool("Fake focus / activation APIs so a background game believes it is active (default on)."),
    autoactivate: bool("Send the activation messages a real window switch would, to a game that started in the background (default on)."),
    isolate: bool("Hide real keyboard / mouse / raw / DirectInput input from the game unless it truly owns the foreground (default on). " +
      "isolate:false passes the user's physical input through, even in the background; prefer a bounded opt-out with for_ms."),
    for_ms: z.number().int().min(1).max(120000).optional().describe(
      "Only with isolate:false: pass physical input for this many ms (1..120000), then vinput restores isolate:true inside the game, " +
      "even if this server goes away. Needs a vinput DLL with timed isolation; an older DLL is refused and nothing is changed."),
    blockfocus: bool("Stop the game taking the real foreground (default on)."),
    nofullscreen: bool("Refuse DXGI exclusive fullscreen (default on). Exclusive fullscreen loses its swapchain in the background."),
    keepvisible: bool("Undo a minimise of a background game (minimised games stop rendering) (default on)."),
    cursor: z.enum(["auto", "virtual", "real"]).optional().describe("Which cursor the game sees: virtual (vinput's own), real, or auto (decided per game)."),
    raw: z.enum(["auto", "post", "buffer", "off"]).optional().describe("How raw-input mouse deltas reach the game: post (WM_INPUT messages), buffer (GetRawInputBuffer), off, or auto."),
    msgs: z.enum(["auto", "on", "off"]).optional().describe("Whether window messages are sent for input: on, off, or auto."),
    edge_margin: z.number().int().min(0).max(4096).optional().describe(
      "Edge guard: keep injected cursor paths this many client pixels inside the window edge (vinput default 8, 0 = off), so " +
      "games with edge scrolling do not pan. Replies then carry edge_contact / edge_guard_applied. Needs a vinput build with the edge guard."),
    fullscreen: z.enum(["borderless", "refuse"]).optional().describe(
      "What vinput does when the game asks for DXGI exclusive fullscreen (with nofullscreen on): borderless (default, the game " +
      "gets a virtual fullscreen window and keeps rendering) or refuse (the request fails, the game stays windowed)."),
  },
  async (a) => {
    const keys = ["fakefocus", "autoactivate", "isolate", "blockfocus", "nofullscreen", "keepvisible", "cursor", "raw", "msgs", "edge_margin", "fullscreen"] as const;
    const want: Record<string, unknown> = {};
    for (const k of keys) if (a[k] !== undefined) want[k] = a[k];
    if (a.for_ms !== undefined && a.isolate !== false) return fail("for_ms applies only together with isolate:false");
    const r = await runVinput([...at(a.pid), "config", ...Object.entries(want).map(([k, v]) => `${k}=${v}`), ...opt("--for", a.for_ms)], 30000);
    if (!r.ok || !r.json || typeof r.json !== "object") return reply(r);
    const eff = configView(r.json as Record<string, unknown>);
    const warnings = Object.entries(want).filter(([k, v]) => eff[k] !== v).map(([k, v]) => `${k}=${v} requested but the game reports ${k}=${eff[k]}`);
    return reply({ ...r, json: { ok: true, pid: a.pid, config: eff, ...(warnings.length ? { warnings } : {}) } });
  });

tool("list_targets", "List controllable games",
  "List every process that currently has vinput loaded: pid, exe, owner (claim), uptime, really_foreground, responding.",
  {}, async () => {
    const r = await runVinput(["list"], 30000);
    return reply({ ...r, json: Array.isArray(r.json) ? { ok: r.ok, targets: r.json } : r.json });
  }, true);

/** status reply as the tool returns it: compact unless verbose */
function statusReply(r: CliResult, verbose?: boolean, extra?: Record<string, unknown>): ToolReply {
  if (verbose || !r.ok || !r.json || typeof r.json !== "object" || Array.isArray(r.json)) return reply(r, extra);
  return reply({ ...r, json: compactStatus(r.json as Record<string, unknown>) }, extra);
}
tool("status", "Game status",
  "Essentials of one game: pid, window handle, client size, really_foreground, app_active (0 = the game thinks it is " +
  "inactive and may ignore input), present count (0 = no frame presented yet), cursor / raw / msgs modes, the input-API " +
  "counters that are non-zero (they show which APIs the game actually reads: read them when it ignores input) and " +
  "warnings[] (fullscreen refusal loop, no window, minimized, failed hooks). verbose:true returns vinput's full status " +
  "(about 3 KB: every counter, hook lists, per-API frame counts, Vulkan / D3D12 internals).",
  { pid, verbose: z.boolean().optional().describe("Return the full vinput status instead of the compact view.") },
  async (a) => statusReply(await runVinput([...at(a.pid), "status"], 15000), a.verbose), true);

tool("log", "Read vinput's log for a game",
  "Tail of vinput's log for this game (hooks installed, refused fullscreen, injected-input problems, anything the DLL " +
  "reported). Read it when a game hangs, shows nothing or ignores input and status does not explain why. Identical " +
  "consecutive messages are collapsed ('repeated N times'). lines = how many to return (default 40), contains = " +
  "case-insensitive substring filter. source 'ring' (default) is the last 200 lines held in the game; 'file' reads the " +
  "tail of the log file on disk (status log_path) for older history.",
  { pid, lines: z.number().int().min(1).max(1000).optional().describe("How many lines to return, newest last (default 40)."),
    contains: z.string().min(1).optional().describe("Only lines containing this text (case-insensitive)."),
    source: z.enum(["ring", "file"]).optional().describe("ring = in-game buffer of the last 200 lines (default); file = tail of the log file on disk.") },
  async (a) => {
    const source = a.source ?? "ring";
    let all: string[];
    if (source === "file") {
      const s = await runVinput([...at(a.pid), "status"], 15000);
      if (!s.ok) return reply(s);
      const path = (s.json as { log_path?: string }).log_path;
      if (!path) return fail("this vinput build reports no log_path in status; use source 'ring'");
      all = readTail(path, 512 * 1024);
    } else {
      const r = await runVinput([...at(a.pid), "log"], 15000);
      const l = (r.json as { log?: unknown }).log;
      if (!r.ok || !Array.isArray(l)) return reply(r);
      all = l.map(String);
    }
    const t = tailLog(all, a.lines ?? 40, a.contains);
    return reply({ ok: true, json: { ok: true, pid: a.pid, source, total: all.length, matched: t.matched, returned: t.lines.length, lines: t.lines }, exitCode: 0, stderr: "", timedOut: false, argv: [] });
  }, true);

// ---------------------------------------------------------------- eyes
tool("capture", "Capture a frame",
  "Screenshot of the game's own frame (D3D11/D3D12/OpenGL/Vulkan swapchain, PrintWindow fallback) taken in the background. " +
  "Returns the PNG (or JPEG) as an image plus JSON {path, w, h, method, capture_path, frame}. Unscaled image pixels = client " +
  "pixels, so coordinates you read off the image can be passed straight to click / move. With region, the image is that crop; " +
  "pass capture_space:true to click / move to use coordinates inside the crop (after max_width / scale: inside the scaled image). " +
  "max_width / scale <= 1 / format jpeg are done by vinput itself when the game's vinput supports it (image.scaled_by " +
  "\"vinput\"), else by this server (\"mcp\").",
  {
    pid,
    path: z.string().optional().describe("Where to save the PNG (default: a new file in the captures folder)."),
    region: regionSchema.optional(),
    after_frames: z.number().int().min(0).max(3600).optional().describe("Wait for this many new presented frames first (e.g. 3 after an action)."),
    method: z.enum(["auto", "native", "printwindow"]).optional().describe("auto (default) falls back to PrintWindow and says why in 'note'."),
    include_image: z.boolean().optional().describe("Return the image content (default true). False = path + JSON only."),
    grid: z.number().int().min(10).max(2000).optional().describe(
      "Draw a labelled coordinate grid on the RETURNED image only (the saved PNG stays clean): a line every N client pixels, " +
      "labelled with the client x (top edge) and y (left edge). Read exact coordinates off it instead of guessing: 100 for a " +
      "full frame, 20-50 inside a region."),
    max_width: z.number().int().min(64).max(8192).optional().describe(
      "Downscale the RETURNED image to at most this many pixels wide (never upscales) to save tokens, e.g. 960. The reply's image " +
      "{scale, offset_x, offset_y} maps it back: client = offset + image / scale. click / move / drag take coord_space:\"capture\" to " +
      "do that for you."),
    scale: z.number().min(0.05).max(8).optional().describe("Explicit resize factor for the returned image (0.5 = half size). Give only one of scale, max_width / zoom (max_width may be combined with zoom as a cap)."),
    zoom: z.number().min(1).max(8).optional().describe("Enlarge the returned image (crisp pixels) to read small text / icons: use with region, e.g. region 200x100 + zoom 4. Combine with max_width to cap the zoomed width (the zoom is reduced to fit, e.g. region 400 wide + zoom 4 + max_width 960 returns 960 px). Not with scale. Needs format png."),
    format: z.enum(["png", "jpeg"]).optional().describe("png (default, exact pixels) or jpeg (much smaller, lossy: do not use it to compare colours). jpeg needs a vinput build with capture output support and cannot be combined with grid or zoom (both are drawn on the decoded PNG; for a small labelled image use format png with max_width or scale instead)."),
    quality: z.number().int().min(1).max(100).optional().describe("JPEG quality 1..100 (default 85)."),
  },
  async (a) => {
    if (a.scale !== undefined && (a.max_width !== undefined || a.zoom !== undefined)) return fail("give only one of scale, max_width / zoom (max_width may be combined with zoom as a cap on the zoomed width)");
    const v = validateFormat(a);
    if (v) return fail(v);
    const g = await grabFrame(a.pid, { path: a.path, region: a.region, after_frames: a.after_frames, method: a.method, include_image: a.include_image, grid: a.grid, max_width: a.max_width, scale: a.scale, zoom: a.zoom, format: a.format, quality: a.quality });
    return g.out;
  });

function validateFormat(a: { format?: "png" | "jpeg"; quality?: number; grid?: number; zoom?: number }): string | undefined {
  if (a.quality !== undefined && a.format !== "jpeg") return "quality applies only to format jpeg";
  if (a.format === "jpeg" && a.grid !== undefined) return "grid needs format png: the grid is drawn on the decoded image and only PNG can be decoded here. Drop format (png) and shrink with max_width or scale instead, or keep jpeg and drop grid (read coordinates from a png capture first)";
  if (a.format === "jpeg" && a.zoom !== undefined) return "zoom needs format png: the enlargement is done on the decoded image and only PNG can be decoded here. Drop format (png) and cap the size with max_width, or keep jpeg and drop zoom (use max_width / scale with jpeg)";
  return undefined;
}

/** Mapping of the last image returned per game (image_px = (client_px - offset) * scale), for coord_space:"capture". */
const lastMapping = new Map<number, Mapping>();
// Cooperative attachment epoch only; this is not OS process-creation identity.
const captureSessionEpoch = new Map<number, number>();
function advanceCaptureSession(pid: number) { captureSessionEpoch.set(pid, (captureSessionEpoch.get(pid) ?? 0) + 1); }

/** One frame grab: runs vinput capture, attaches the PNG, and diagnoses "nothing presented / fullscreen refused" when the
 *  native path was not used. Shared by capture and the capture_after option of the action tools. */
async function grabFrame(pid: number, o: { path?: string; region?: z.infer<typeof regionSchema>; after_frames?: number; method?: "auto" | "native" | "printwindow"; include_image?: boolean; grid?: number; max_width?: number; scale?: number; zoom?: number; format?: "png" | "jpeg"; quality?: number }) {
  const jpeg = o.format === "jpeg";
  const path = resolve(o.path ?? defaultCapturePath(pid, jpeg ? "jpg" : "png"));
  const base = [...at(pid), "capture", path, ...region(o.region), ...opt("--after-frames", o.after_frames), ...opt("--method", o.method === "native" ? "swapchain" : o.method)];
  // vinput's own output transform (capture_output_version 1): it can only shrink, and it also re-bases vinput's capture-space
  // map on the saved image. zoom (enlarging) stays here.
  const coreArgs = o.zoom !== undefined ? [] : [
    ...(o.max_width !== undefined ? ["--max-width", String(o.max_width)] : []),
    ...(o.scale !== undefined && o.scale <= 1 ? ["--scale", String(o.scale)] : []),
    ...(jpeg ? ["--format", "jpeg", ...opt("--quality", o.quality)] : [])];
  let r = await runVinput([...base, ...coreArgs], 60000);
  let scaledBy: "vinput" | "mcp" | undefined, fallbackNote: string | undefined;
  if (coreArgs.length && lacksSupport(r)) {
    if (jpeg) return { r, out: reply(r, { error: "format jpeg needs a vinput DLL with capture output support in the game (restart the game with the current build); capture without format for PNG" }), saved: undefined as string | undefined, summary: undefined as Record<string, unknown> | undefined };
    fallbackNote = "this game's vinput DLL has no capture output support: the image was scaled by the MCP server instead";
    r = await runVinput(base, 60000);
  } else if (coreArgs.length && r.ok) {
    // Trust the core transform only when the reply proves it ran (it reports the client rectangle the image covers and the
    // saved format); otherwise the image is the unscaled frame and the TS path below scales it.
    const cj = r.json as { region_client_w?: unknown; format?: unknown };
    const coreRan = typeof cj.region_client_w === "number" && cj.region_client_w > 0;
    if (jpeg && cj.format !== "jpeg")
      return { r, out: reply({ ...r, ok: false }, { error: "format jpeg was requested but vinput did not report a JPEG; capture without format for PNG" }), saved: undefined as string | undefined, summary: undefined as Record<string, unknown> | undefined };
    if (coreRan && (o.max_width !== undefined || o.scale !== undefined)) scaledBy = "vinput";
  }
  const j0 = (r.json && typeof r.json === "object" ? r.json : {}) as { method?: string; path?: string; error?: string };
  let extra: Record<string, unknown> = {};
  let stuck = false;
  if (!r.ok || j0.method === "printwindow") {
    const st = await runVinput([...at(pid), "status"], 15000);
    if (st.ok && st.json && typeof st.json === "object") {
      const d = diagnoseFrames(st.json as Record<string, unknown>);
      stuck = d.stuck;
      if (d.nothingPresented) extra.nothing_presented = true;
      if (d.refusals) extra.fullscreen_refusals = d.refusals;
      if (d.warning) extra.warnings = [d.warning];
      if (d.note) extra.notes = [d.note];
    }
  }
  if (stuck) {
    const msg = (extra.warnings as string[])[0];
    const out = reply({ ...r, ok: false }, { ...extra, error: r.ok ? msg : `${j0.error ?? "capture failed"}. ${msg}` });
    return { r, out, saved: undefined as string | undefined, summary: undefined as Record<string, unknown> | undefined };
  }
  let saved: string | undefined;
  let png: Buffer | undefined, imgErr: string | undefined;
  const mapping: Mapping = { scale: 1, offset_x: o.region?.x ?? 0, offset_y: o.region?.y ?? 0 };
  let imageInfo: Record<string, unknown> | undefined;
  if (scaledBy === "vinput") {
    // vinput reports the client rectangle the saved image covers: image_px = (client_px - origin) * w / region_client_w
    const j = r.json as { w?: number; region_client_w?: number; origin_x?: number; origin_y?: number };
    if (j.w && j.region_client_w) mapping.scale = j.w / j.region_client_w;
    if (typeof j.origin_x === "number") mapping.offset_x = j.origin_x;
    if (typeof j.origin_y === "number") mapping.offset_y = j.origin_y;
  }
  if (r.ok && o.include_image !== false) {
    saved = j0.path || path;
    try {
      png = readFileSync(saved);
      let w = (r.json as { w?: number }).w, h = (r.json as { h?: number }).h;
      const wantScale = scaledBy ? undefined : o.scale ?? o.zoom, wantW = scaledBy ? undefined : o.max_width;
      if (wantScale !== undefined || wantW !== undefined) scaledBy = "mcp";
      if (o.grid || wantScale !== undefined || wantW !== undefined) {
        try {
          let im = decodePng(png);
          let factor = wantScale ?? 1;
          if (wantW !== undefined && im.width * factor > wantW) factor = wantW / im.width;   // max_width caps the final (zoomed) width
          if (factor !== 1) {
            const nw = Math.max(1, Math.round(im.width * factor)), nh = Math.max(1, Math.round(im.height * factor));
            mapping.scale = nw / im.width;
            im = resample(im, nw, nh);
          }
          if (o.grid) im = drawGrid(im, mapping, o.grid);
          if (o.grid || factor !== 1) png = encodePng(im);
          w = im.width; h = im.height;
        } catch (e) {
          imgErr = `could not process the image (${(e as Error).message}); the original image is returned unchanged`;
          if (scaledBy === "mcp") { mapping.scale = 1; scaledBy = undefined; }
        }
      }
      imageInfo = { w, h, ...mapping, ...(scaledBy ? { scaled_by: scaledBy } : {}), ...(jpeg ? { format: "jpeg" } : {}),
        ...(o.grid && !imgErr ? { grid: o.grid } : {}), ...(imgErr ? { image_note: imgErr } : {}) };
      lastMapping.set(pid, { ...mapping });
    } catch (e) { imgErr = `capture reported ok but the image could not be read: ${(e as Error).message}`; }
  }
  if (fallbackNote) extra = { ...extra, notes: [...((extra.notes as string[] | undefined) ?? []), fallbackNote] };
  const out = reply(r, { ...extra, ...(imageInfo ? { image: imageInfo } : {}), ...(imageInfo?.image_note ? { image_note: imageInfo.image_note } : {}) });
  if (png) {
    if (png.length > MAX_IMAGE_BYTES) out.content.push({ type: "text", text: `image not attached: ${png.length} bytes > ${MAX_IMAGE_BYTES}; use region, max_width or format jpeg to capture less` });
    else out.content.push({ type: "image", data: png.toString("base64"), mimeType: jpeg ? "image/jpeg" : "image/png" });
  } else if (imgErr) { out.content.push({ type: "text", text: imgErr }); out.isError = true; }
  const j = r.json as Record<string, unknown>;
  const summary = r.ok ? { path: j.path, w: j.w, h: j.h, method: j.method, capture_path: j.capture_path, frame: j.frame, ...extra } : undefined;
  return { r, out, saved, summary };
}

const captureAfterSchema = z.union([z.boolean(), z.number().int().min(0).max(3600)]).optional().describe(
  "Look right after acting: true waits for 3 presented frames, with one explicitly reported fresh PrintWindow fallback if that wait times out and status shows no target Present stream; a number strictly waits for that many frames " +
  "first (0 = capture immediately). The reply then includes the frame as an inline image plus capture_after {path, w, h, ...}, " +
  "saving a capture round trip. Skipped when the action itself failed.");
const DEFAULT_LOOK_FRAMES = 3;
/** Act-then-look: append the post-action frame to an action's reply. */
async function look(pid: number, out: ToolReply, ca: boolean | number | undefined): Promise<ToolReply> {
  if (ca === undefined || ca === false || out.isError) return out;
  const session = captureSessionEpoch.get(pid) ?? 0;
  let g = await grabFrame(pid, { after_frames: ca === true ? DEFAULT_LOOK_FRAMES : ca });
  let fallback: Record<string, unknown> | undefined;
  // A number remains a strict Present request. Only the convenience boolean may
  // recover from this exact timeout when fresh status proves no observed stream.
  const initial = g.r.json as { error?: unknown } | undefined;
  if (ca === true && (captureSessionEpoch.get(pid) ?? 0) === session && !g.r.ok && initial?.error === "not enough target Present calls before capture timeout") {
    const status = await runVinput([...at(pid), "status"], 15000);
    const s = status.ok && status.json && typeof status.json === "object" && !Array.isArray(status.json)
      ? status.json as Record<string, unknown> : undefined;
    const counters = s?.counters && typeof s.counters === "object" && !Array.isArray(s.counters)
      ? s.counters as Record<string, unknown> : undefined;
    const fullscreen = s?.fullscreen && typeof s.fullscreen === "object" && !Array.isArray(s.fullscreen)
      ? s.fullscreen as Record<string, unknown> : undefined;
    const blockedBackend = [s?.d3d9, s?.ddraw].some(value => value !== undefined &&
      (!value || typeof value !== "object" || Array.isArray(value) ||
        (value as Record<string, unknown>).blocked_before_first_present !== false));
    if ((captureSessionEpoch.get(pid) ?? 0) === session && s?.frames === 0 && s.frame_api === "none" &&
        fullscreen?.refused === 0 && fullscreen.failures === 0 && fullscreen.blocked_before_first_present === false && !blockedBackend &&
        (s.counters === undefined || counters !== undefined) &&
        (counters?.fs_refused === undefined || counters.fs_refused === 0) &&
        (counters?.present_ours === undefined || counters.present_ours === 0)) {
      fallback = { reason: "no_target_present_stream_observed", requested_present_frames: DEFAULT_LOOK_FRAMES,
        present_wait_satisfied: false, method: "printwindow", scope: "fresh PrintWindow request; no render-frame or application-acceptance guarantee" };
      g = await grabFrame(pid, { after_frames: 0, method: "printwindow" });
    }
  }
  let j: Record<string, unknown> = {};
  try { j = JSON.parse((out.content[0] as { text: string }).text); } catch { /* keep empty */ }
  if (g.summary && !g.out.isError) {
    out.content[0] = { type: "text", text: JSON.stringify({ ...j, capture_after: { ...g.summary, ...(fallback ? { fallback } : {}) } }) };
    for (const c of g.out.content) if (c.type === "image") out.content.push(c);
  } else {
    let e: unknown = "capture failed";
    try { e = (JSON.parse((g.out.content[0] as { text: string }).text) as { error?: string }).error ?? e; } catch { /* default */ }
    out.content[0] = { type: "text", text: JSON.stringify({ ...j, capture_after_error: e, ...(fallback ? { capture_after_fallback: fallback } : {}) }) };
  }
  return out;
}

tool("ocr", "Read text on screen",
  "OCR the current frame with the built-in Windows OCR engine. Returns text and lines[] with boxes [x,y,w,h] in client " +
  "pixels (words[] too when words:true). Small or stylised text: use region and scale 2.",
  {
    pid, region: regionSchema.optional(),
    scale: z.number().min(1).max(4).optional().describe("Upscale before OCR (2 helps small text)."),
    lang: z.string().optional().describe("OCR language tag, e.g. en-US (default: the user's profile language)."),
    words: z.boolean().optional().describe("Also return every word with its box (bigger reply)."),
    tiles: z.boolean().optional().describe("Tiled second pass that finds words the full-frame pass drops (default true; false is faster)."),
  },
  async (a) => reply(await runVinput([...at(a.pid), "ocr", ...region(a.region), ...opt("--scale", a.scale), ...opt("--lang", a.lang),
    ...(a.words ? [] : ["--lines-only"]), ...(a.tiles === false ? [] : ["--tiles"])], 60000)), true);

const textMatch = {
  text: z.string().min(1).describe("Text to find. Default matching: case-insensitive, whitespace ignored, substring of a line."),
  nth: z.number().int().min(1).optional().describe("Pick the Nth match in reading order (1-based). Without it, several matches = refusal listing the candidates."),
  exact: z.boolean().optional().describe("Case-sensitive, whole-word match."),
  fuzzy: z.number().int().min(0).max(3).optional().describe("Also accept whole words within N edits (OCR slips)."),
  region: regionSchema.optional(),
};
const textArgs = (a: { text: string; nth?: number; exact?: boolean; fuzzy?: number; region?: z.infer<typeof regionSchema> }) =>
  [a.text, ...opt("--nth", a.nth), ...opt("--exact", a.exact), ...opt("--fuzzy", a.fuzzy), ...region(a.region)];

tool("find_text", "Find text on screen",
  "Locate text without clicking. Returns the matched box, centre, match_count and match_score, or ok:false (with what OCR " +
  "did read) when it is not there.",
  { pid, ...textMatch, timeout_ms: timeoutMs(0) },
  async (a) => reply(await runVinput([...at(a.pid), "click-text", ...textArgs(a), "--dry-run", "--timeout", String(a.timeout_ms ?? 0)], (a.timeout_ms ?? 0) + 60000)), true);

tool("click_text", "Click text on screen",
  "Find text with OCR and click the centre of its box (keeps looking for timeout_ms). Refuses and clicks nothing when " +
  "the text matches more than once and nth is not given. Verify the result with wait_text / capture.",
  { pid, ...textMatch, timeout_ms: timeoutMs(5000), button: button.optional(),
    optional: z.boolean().optional().describe("Succeed with skipped:true if the text never appears (for dialogs that may not show)."),
    capture_after: captureAfterSchema },
  async (a) => look(a.pid, reply(await runVinput([...at(a.pid), "click-text", ...textArgs(a), ...opt("--timeout", a.timeout_ms), ...opt("--button", a.button),
    ...opt("--optional", a.optional)], (a.timeout_ms ?? 5000) + 60000)), a.capture_after));

tool("wait_text", "Wait for text",
  "Block until text appears on screen (or, with gone:true, disappears). Times out with timed_out:true and ocr_saw.",
  { pid, text: textMatch.text, region: regionSchema.optional(), gone: z.boolean().optional().describe("Wait until the text is NOT on screen."),
    timeout_ms: timeoutMs(15000), optional: z.boolean().optional().describe("Succeed with skipped:true on timeout.") },
  async (a) => reply(await runVinput([...at(a.pid), "wait-text", a.text, ...region(a.region), ...opt("--gone", a.gone),
    ...opt("--timeout", a.timeout_ms), ...opt("--optional", a.optional)], (a.timeout_ms ?? 15000) + 60000)), true);

const imageArgs = {
  template: z.string().min(1).describe("Path of a PNG template (e.g. an icon cut from an earlier capture with region)."),
  threshold: z.number().min(0).max(1).optional().describe("Match score needed, 0..1 (default 0.8; use 0.9+ for repetitive patterns)."),
  scales: z.string().optional().describe("Comma-separated scales to try, e.g. '0.9,1,1.1' (default 0.85..1.18)."),
  region: regionSchema.optional(),
};
const imgArgv = (a: { template: string; threshold?: number; scales?: string; region?: z.infer<typeof regionSchema> }) =>
  [resolve(a.template), ...opt("--threshold", a.threshold), ...opt("--scales", a.scales), ...region(a.region)];

tool("find_image", "Find an image on screen",
  "Template-match a PNG on the current frame (normalised cross-correlation, brightness-independent, no rotation). " +
  "Returns matches[] with box, score and scale.",
  { pid, ...imageArgs }, async (a) => reply(await runVinput([...at(a.pid), "find-image", ...imgArgv(a)], 60000)), true);

tool("click_image", "Click an image on screen",
  "Find a PNG template on screen and click its centre. Refuses when it matches more than once and nth is not given.",
  { pid, ...imageArgs, nth: z.number().int().min(1).optional(), timeout_ms: timeoutMs(5000), button: button.optional(),
    optional: z.boolean().optional() },
  async (a) => reply(await runVinput([...at(a.pid), "click-image", ...imgArgv(a), ...opt("--nth", a.nth), ...opt("--timeout", a.timeout_ms),
    ...opt("--button", a.button), ...opt("--optional", a.optional)], (a.timeout_ms ?? 5000) + 60000)));

tool("wait_image", "Wait for an image",
  "Block until a PNG template appears on screen (or disappears with gone:true).",
  { pid, ...imageArgs, gone: z.boolean().optional(), timeout_ms: timeoutMs(15000) },
  async (a) => reply(await runVinput([...at(a.pid), "wait-image", ...imgArgv(a), ...opt("--gone", a.gone), ...opt("--timeout", a.timeout_ms)],
    (a.timeout_ms ?? 15000) + 60000)), true);

tool("pixel", "Read one pixel", "RGB colour of one client pixel: {r,g,b,hex}.",
  { pid, x: z.number().int().min(0), y: z.number().int().min(0) },
  async (a) => reply(await runVinput([...at(a.pid), "pixel", String(a.x), String(a.y)], 30000)), true);

tool("wait_change", "Wait for the screen to change",
  "Poll frame hashes until the picture (or a region of it) changes, e.g. after an action. A change proves pixels changed, not that a particular action succeeded.",
  { pid, region: regionSchema.optional(), timeout_s: z.number().min(0.001).max(120).optional().describe("Seconds (default 10).") },
  async (a) => reply(await runVinput([...at(a.pid), "wait-change", ...region(a.region), ...opt("--timeout", a.timeout_s)], ((a.timeout_s ?? 10) + 60) * 1000)), true);

tool("capture_burst", "Capture a contact sheet",
  "Watch something happen: capture frames tiles (default 8) interval_ms apart (default 250) into ONE contact sheet image, in " +
  "time order, left to right then top to bottom. Each tile is shrunk to max_width (default 320). Use it for animations, " +
  "loading progress or whether a held key moves the character, instead of many capture calls. Late frames are skipped, " +
  "not bunched; frames[] in the reply has each tile's actual time and rectangle. A sheet is NOT a single game image: do not " +
  "click coordinates read off it (capture once more first). Other tools keep working while it runs. Needs a vinput build " +
  "with capture output support in the game.",
  {
    pid,
    path: z.string().optional().describe("Where to save the sheet (default: a new file in the captures folder)."),
    frames: z.number().int().min(1).max(16).optional().describe("Number of tiles, 1..16 (default 8)."),
    interval_ms: z.number().int().min(10).max(5000).optional().describe("Time between tiles, 10..5000 ms (default 250). frames x interval is at most 30 s."),
    columns: z.number().int().min(1).max(16).optional().describe("Tiles per row (default min(4, frames))."),
    max_width: z.number().int().min(16).max(8192).optional().describe("Width of each tile in pixels (default 320)."),
    scale: z.number().min(0.05).max(1).optional().describe("Tile scale instead of max_width (0.05..1)."),
    format: z.enum(["png", "jpeg"]).optional().describe("png (default) or jpeg (smaller, lossy)."),
    quality: z.number().int().min(1).max(100).optional().describe("JPEG quality (default 85)."),
    region: regionSchema.optional(),
    include_image: z.boolean().optional().describe("Return the sheet as an image (default true)."),
  },
  async (a) => {
    if (a.max_width !== undefined && a.scale !== undefined) return fail("give only one of max_width, scale");
    if (a.quality !== undefined && a.format !== "jpeg") return fail("quality applies only to format jpeg");
    if (a.columns !== undefined && a.columns > (a.frames ?? 8)) return fail("columns cannot exceed frames");
    const jpeg = a.format === "jpeg";
    const path = resolve(a.path ?? defaultCapturePath(a.pid, jpeg ? "jpg" : "png", "_burst"));
    const r = await runVinput([...at(a.pid), "capture-burst", path, ...opt("--frames", a.frames), ...opt("--interval-ms", a.interval_ms),
      ...opt("--columns", a.columns), ...opt("--max-width", a.max_width), ...opt("--scale", a.scale), ...opt("--format", a.format),
      ...opt("--quality", a.quality), ...region(a.region)], 120000);
    const out = reply(r);
    if (r.ok && a.include_image !== false) {
      try {
        const img = readFileSync((r.json as { path?: string }).path || path);
        if (img.length > MAX_IMAGE_BYTES) out.content.push({ type: "text", text: `image not attached: ${img.length} bytes > ${MAX_IMAGE_BYTES}; use fewer frames, a smaller max_width or format jpeg` });
        else out.content.push({ type: "image", data: img.toString("base64"), mimeType: jpeg ? "image/jpeg" : "image/png" });
      } catch (e) { out.content.push({ type: "text", text: `burst reported ok but the sheet could not be read: ${(e as Error).message}` }); out.isError = true; }
    }
    return out;
  }, true);

// ---------------------------------------------------------------- mouse
const xy = {
  x: z.number().describe("X in client pixels (or 0..1 with norm)."),
  y: z.number().describe("Y in client pixels (or 0..1 with norm)."),
  norm: z.boolean().optional().describe("x and y are fractions of the client size (0..1)."),
  capture_space: z.boolean().optional().describe("x and y are pixels of the last capture (use after a region capture). vinput binds that map to one window and client size and refuses it after a resize or retarget: capture again."),
};
const mappingSchema = z.object({ scale: z.number().positive(), offset_x: z.number(), offset_y: z.number() })
  .describe("Image-to-client mapping as returned by capture (image: {scale, offset_x, offset_y}).");
const coordArgs = {
  coord_space: z.enum(["client", "capture"]).optional().describe(
    "client (default): x / y are game client pixels. capture: x / y are pixels of the image you were shown by capture (after " +
    "max_width / scale / zoom / region), converted with the last capture's mapping, or with 'mapping' when given."),
  mapping: mappingSchema.optional(),
};
type CoordIn = { coord_space?: "client" | "capture"; mapping?: Mapping; norm?: boolean; capture_space?: boolean };
/** Convert image-space points to client points when coord_space is "capture"; an error string when that is impossible. */
function mapPoints(pid: number, a: CoordIn, pts: [number, number][]): { pts: [number, number][]; mapped: boolean } | { error: string } {
  if (a.coord_space !== "capture") return { pts, mapped: false };
  if (a.norm || a.capture_space) return { error: "coord_space \"capture\" cannot be combined with norm or capture_space" };
  const m = a.mapping ?? lastMapping.get(pid);
  if (!m) return { error: "coord_space \"capture\" needs a capture first (the mapping comes from the last capture of this game) or an explicit mapping" };
  return { pts: pts.map(([x, y]) => toClient(m, x, y)), mapped: true };
}

// ---- edge-scroll guard: a cursor resting at the window edge pans the view in many games (The Sims, strategy games)
const DEFAULT_EDGE_MARGIN = 8;
const edgeArgs = {
  edge_margin: z.number().int().min(0).max(200).optional().describe(
    `Warn when the position (or a drag's start / end) is within this many client pixels of the window edge, where games with edge scrolling pan the view (default ${DEFAULT_EDGE_MARGIN}; 0 = off). The action is still performed: the target may be in the margin on purpose. A straight path between two points outside the margin never enters it.`),
};
const parkArg = {
  park_cursor: z.boolean().optional().describe("After the action, move the cursor to the centre of the window so it does not rest at an edge (default off)."),
};
const sizeCache = new Map<number, { w: number; h: number; t: number }>();
/** Client size of a game from status, cached for 5 s (a window can be resized, but not often). Undefined when unreadable. */
async function clientSize(pid: number): Promise<{ w: number; h: number } | undefined> {
  const c = sizeCache.get(pid);
  if (c && Date.now() - c.t < 5000) return c;
  const st = await runVinput([...at(pid), "status"], 15000);
  const j = st.json as { client_w?: number; client_h?: number };
  if (!st.ok || !j.client_w || !j.client_h) return undefined;
  const v = { w: j.client_w, h: j.client_h, t: Date.now() };
  sizeCache.set(pid, v);
  return v;
}
/** Warnings for points (client px, or 0..1 with norm) that sit inside the edge margin. */
async function edgeWarnings(pid: number, pts: { label: string; x: number; y: number }[], margin: number | undefined, norm?: boolean): Promise<string[]> {
  const m = margin ?? DEFAULT_EDGE_MARGIN;
  if (m <= 0 || pts.length === 0) return [];
  const sz = await clientSize(pid);
  if (!sz) return [];
  const out: string[] = [];
  for (const p of pts) {
    const x = norm ? p.x * sz.w : p.x, y = norm ? p.y * sz.h : p.y;
    const near: string[] = []; let d = Infinity;
    if (x < m) { near.push("left"); d = Math.min(d, x); }
    if (x >= sz.w - m) { near.push("right"); d = Math.min(d, sz.w - 1 - x); }
    if (y < m) { near.push("top"); d = Math.min(d, y); }
    if (y >= sz.h - m) { near.push("bottom"); d = Math.min(d, sz.h - 1 - y); }
    if (near.length) out.push(`${p.label} (${Math.round(x)},${Math.round(y)}) is ${Math.max(0, Math.round(d))} px from the ${near.map((n) => n + " edge").join(" and ")} of the ${sz.w}x${sz.h} window (edge_margin ${m}). Games with edge scrolling pan the view while the cursor rests there; aim further inside, or pass park_cursor:true to move the cursor to the centre afterwards.`);
  }
  return out;
}
/** Move the cursor to the window centre. */
async function parkCursor(pid: number): Promise<{ x: number; y: number } | undefined> {
  const sz = await clientSize(pid);
  if (!sz) return undefined;
  const x = Math.round(sz.w / 2), y = Math.round(sz.h / 2);
  const r = await runVinput([...at(pid), "mouse", String(x), String(y)], 60000);
  return r.ok ? { x, y } : undefined;
}

const DEFAULT_CLICK_HOVER_MS = 80, DEFAULT_CLICK_HOLD_MS = 140;
tool("click", "Click",
  "Click a mouse button at a position (or at the current virtual cursor when x/y are omitted). The cursor hovers there " +
  "first (hover_ms) and the button is held for hold_ms, both long enough for games that poll input once a frame. Old menus: hold_ms 300. " +
  "Placement / drop-in-world clicks: hold_ms ~900. ok:true = the click was delivered; verify with capture / wait_text.",
  { pid, x: xy.x.optional(), y: xy.y.optional(), norm: xy.norm, capture_space: xy.capture_space, button: button.optional(),
    count: z.number().int().min(1).max(10).optional().describe("2 = double click."),
    hold_ms: z.number().int().min(1).max(10000).optional().describe(
      `How long the button stays down (default ${DEFAULT_CLICK_HOLD_MS} ms). Games that poll the mouse once a frame or per tick ignore very short ` +
      "presses: measured, under about 200 ms is often ignored in some old games, old menus needed about 300 ms, and furniture / item placement " +
      "needed about 900 ms."),
    hover_ms: z.number().int().min(0).max(5000).optional().describe(
      `Extra pause with the cursor already over the target, before the button goes down (default ${DEFAULT_CLICK_HOVER_MS} ms; 0 = none). ` +
      "vinput already waits for the hover to be dispatched and rendered; this adds time for hover-gated UI. Ignored when x / y are omitted."),
    di_poll_timeout_ms: z.number().int().min(0).max(5000).optional().describe(
      "For games that poll the mouse through DirectInput: vinput keeps the button down until the game has READ the press, and waits " +
      "for a read of the release (default: automatic, at most 1000 ms extra per phase, only when the game read DirectInput recently). " +
      "N = require both reads within N extra ms each (error if not seen); 0 = do not wait. The reply's evidence " +
      "{di_poll_confirmed, di_release_confirmed} says what was observed; a read is not proof the game acted."),
    di_release_timeout_ms: z.number().int().min(0).max(5000).optional().describe("Separate cap for the release read (0 = do not wait for it)."),
    ...coordArgs, ...edgeArgs, ...parkArg, capture_after: captureAfterSchema },
  async (a) => {
    if ((a.x === undefined) !== (a.y === undefined)) return fail("give both x and y, or neither");
    let pos: string[] = a.x === undefined ? [] : [String(a.x), String(a.y)];
    let mappedInfo: Record<string, unknown> | undefined;
    if (a.x !== undefined) {
      const m = mapPoints(a.pid, a, [[a.x, a.y as number]]);
      if ("error" in m) return fail(m.error);
      pos = [String(m.pts[0][0]), String(m.pts[0][1])];
      if (m.mapped) mappedInfo = { mapped: { x: m.pts[0][0], y: m.pts[0][1] } };
    }
    const warnings = a.x === undefined ? [] : await edgeWarnings(a.pid, [{ label: "click target", x: Number(pos[0]), y: Number(pos[1]) }], a.edge_margin, a.norm);
    const cr = await runVinput([...at(a.pid), "click", a.button ?? "left", ...pos, ...opt("--norm", a.norm), ...opt("--capture-space", a.capture_space),
      ...(a.x === undefined ? [] : ["--settle-ms", String(a.hover_ms ?? DEFAULT_CLICK_HOVER_MS)]),
      ...opt("--count", a.count), "--ms", String(a.hold_ms ?? DEFAULT_CLICK_HOLD_MS),
      ...(a.di_poll_timeout_ms !== undefined ? ["--di-poll-timeout-ms", String(a.di_poll_timeout_ms)] : []),
      ...(a.di_release_timeout_ms !== undefined ? ["--di-release-timeout-ms", String(a.di_release_timeout_ms)] : [])],
      60000 + (a.hold_ms ?? 0) * (a.count ?? 1) + (a.hover_ms ?? 0) + ((a.di_poll_timeout_ms ?? 1000) + (a.di_release_timeout_ms ?? 1000)) * (a.count ?? 1));
    const parked = a.park_cursor && cr.ok ? await parkCursor(a.pid) : undefined;
    const evidence = inputEvidence(cr.json);
    return look(a.pid, reply(cr, { ...mappedInfo, ...(evidence ? { evidence } : {}), ...(warnings.length ? { warnings } : {}), ...(parked ? { parked } : {}) }), a.capture_after);
  });

tool("move", "Move the cursor",
  "Move the game's virtual cursor to an absolute position (hover). The real cursor is not touched.",
  { pid, ...xy, steps: z.number().int().min(1).max(200).optional().describe("Intermediate moves (default 1)."), ...coordArgs, ...edgeArgs, capture_after: captureAfterSchema },
  async (a) => {
    const m = mapPoints(a.pid, a, [[a.x, a.y]]);
    if ("error" in m) return fail(m.error);
    const warnings = await edgeWarnings(a.pid, [{ label: "move target", x: m.pts[0][0], y: m.pts[0][1] }], a.edge_margin, a.norm);
    const mr = await runVinput([...at(a.pid), "mouse", String(m.pts[0][0]), String(m.pts[0][1]), ...opt("--norm", a.norm),
      ...opt("--capture-space", a.capture_space), ...opt("--steps", a.steps)], 60000);
    const evidence = inputEvidence(mr.json);
    return look(a.pid, reply(mr, { ...(m.mapped ? { mapped: { x: m.pts[0][0], y: m.pts[0][1] } } : {}), ...(evidence ? { evidence } : {}), ...(warnings.length ? { warnings } : {}) }), a.capture_after);
  });

tool("move_relative", "Relative mouse motion",
  "Relative mouse motion (raw input / DirectInput deltas) for mouse-look / camera control in 3D games.",
  { pid, dx: z.number().int(), dy: z.number().int(), steps: z.number().int().min(1).max(200).optional(),
    raw: z.boolean().optional().describe("Do not clamp the virtual cursor to the window (games that re-centre the cursor).") },
  async (a) => reply(await runVinput([...at(a.pid), "move", String(a.dx), String(a.dy), ...opt("--steps", a.steps), ...opt("--raw", a.raw)], 60000)));

const sleepMs = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));
const DEFAULT_DRAG_HOLD_START_MS = 150, DEFAULT_DRAG_HOLD_END_MS = 250;
tool("drag", "Drag",
  "Press a mouse button at (x1,y1), hold briefly, move to (x2,y2) in small steps with the button held, dwell there, then " +
  "release. The hold and the dwell matter for games that poll the mouse (item drops and furniture placement are dropped " +
  "otherwise): raise hold_end_ms to 500-900 for placement. Always releases the button, even if a step fails.",
  { pid, x1: z.number(), y1: z.number(), x2: z.number(), y2: z.number(),
    ms: z.number().int().min(1).max(30000).optional().describe("Duration of the stepped move (default 300)."), button: button.optional(),
    steps: z.number().int().min(1).max(200).optional().describe("Number of intermediate mouse moves (default: one per 20 ms of ms, 3 to 60)."),
    hold_start_ms: z.number().int().min(0).max(10000).optional().describe(`Hold the button at the start point before moving (default ${DEFAULT_DRAG_HOLD_START_MS} ms).`),
    hold_end_ms: z.number().int().min(0).max(10000).optional().describe(`Dwell at the destination, button still down, before releasing (default ${DEFAULT_DRAG_HOLD_END_MS} ms).`),
    placement: z.boolean().optional().describe(
      "Use vinput's owned placement drag (one job inside the game): it waits until the game has READ the press, each step's cursor " +
      "position, the final position and the release, so a slow game cannot miss the pickup or the drop. Needs a game that reads " +
      "the cursor (GetCursorPos) and the mouse buttons through DirectInput, and a vinput build that supports it; otherwise the " +
      "normal press / move / release sequence is used and the reply says why (placement_fallback). The same fallback happens " +
      "automatically when the game never reads the press (button_down_not_observed: Unity-style games that read buttons through window " +
      "messages); plain drag works there. The reply's evidence shows "+
      "what was observed; it is still not proof the game placed the item."),
    poll_timeout_ms: z.number().int().min(1).max(5000).optional().describe("placement: how long to wait for each read (default 1000)."),
    ...coordArgs, ...edgeArgs, ...parkArg, capture_after: captureAfterSchema },
  async (a) => {
    const m = mapPoints(a.pid, a, [[a.x1, a.y1], [a.x2, a.y2]]);
    if ("error" in m) return fail(m.error);
    const [[x1, y1], [x2, y2]] = m.pts;
    const warnings = await edgeWarnings(a.pid, [{ label: "drag start", x: x1, y: y1 }, { label: "drag destination", x: x2, y: y2 }], a.edge_margin);
    const btn = a.button ?? "left", ms = a.ms ?? 300;
    const steps = a.steps ?? Math.min(60, Math.max(3, Math.round(ms / 20)));
    const holdStart = a.hold_start_ms ?? DEFAULT_DRAG_HOLD_START_MS, holdEnd = a.hold_end_ms ?? DEFAULT_DRAG_HOLD_END_MS;
    const where = { ...(m.mapped ? { mapped: { x1, y1, x2, y2 } } : {}), ...(warnings.length ? { warnings } : {}) };
    let placementFallback: string | undefined;
    if (a.placement) {
      const pr = await runVinput([...at(a.pid), "placement-drag", String(x1), String(y1), String(x2), String(y2), "--steps", String(Math.min(128, steps)),
        "--ms", String(ms), "--hold-ms", String(holdStart), "--dwell-ms", String(holdEnd), "--button", btn, ...opt("--poll-timeout-ms", a.poll_timeout_ms)],
        ms + holdStart + holdEnd + (Math.min(128, steps) + 4) * (a.poll_timeout_ms ?? 1000) + 60000);
      const notSeen = placementPressNotSeen(pr);
      if (!lacksSupport(pr) && !notSeen) {
        const ok = pr.ok;
        const parked = a.park_cursor && ok ? await parkCursor(a.pid) : undefined;
        const evidence = inputEvidence(pr.json);
        return look(a.pid, reply(pr, { placement: true, steps: Math.min(128, steps), ms, hold_start_ms: holdStart, hold_end_ms: holdEnd,
          ...(evidence ? { evidence } : {}), ...where, ...(parked ? { parked } : {}) }), a.capture_after);
      }
      placementFallback = notSeen
        ? `${notSeen}: this game does not read mouse buttons through DirectInput (Unity-style window messages), so the placement drag could not confirm the press; used the plain press / move / release drag instead`
        : `${(pr.json as { error?: string }).error ?? "placement drag unsupported"}: used press / move / release instead`;
    }
    const down = await runVinput([...at(a.pid), "mousedown", btn, String(x1), String(y1)], 60000);
    if (!down.ok) return reply({ ...down, ok: false, json: { ok: false, error: "drag: mouse down failed", down: down.json } });
    let mv: CliResult | undefined, up: CliResult | undefined;
    try {
      await sleepMs(holdStart);
      mv = await runVinput([...at(a.pid), "mouse", String(x2), String(y2), "--steps", String(steps), "--step-ms", String(Math.max(1, Math.round(ms / steps)))], ms + 60000);
      await sleepMs(holdEnd);
    } finally {
      up = await runVinput([...at(a.pid), "mouseup", btn, String(x2), String(y2)], 60000);   // always release
    }
    const ok = mv.ok && up.ok;
    const parked = a.park_cursor && ok ? await parkCursor(a.pid) : undefined;
    const evidence = { ...(inputEvidence(mv.json) ?? {}), ...(inputEvidence(up.json) ?? {}) };
    return look(a.pid, reply({ ok, exitCode: ok ? 0 : 1, stderr: [mv.stderr, up.stderr].filter(Boolean).join("\n"), timedOut: false, argv: [],
      json: { ok, steps, ms, hold_start_ms: holdStart, hold_end_ms: holdEnd, down: down.json, move: mv.json, up: up.json,
        ...(ok ? {} : { error: !mv.ok ? `drag: move failed (the button was released): ${JSON.stringify((mv.json as { error?: string }).error ?? mv.json)}` : "drag: mouse up failed" }) } },
      { ...(placementFallback ? { placement_fallback: placementFallback } : {}), ...(Object.keys(evidence).length ? { evidence } : {}), ...where, ...(parked ? { parked } : {}) }), a.capture_after);
  });

tool("scroll", "Scroll the mouse wheel",
  "Scroll N wheel notches (positive = up / away, negative = down) at the virtual cursor, after moving it to x,y if given.",
  { pid, notches: z.number().int().min(-100).max(100), x: z.number().optional(), y: z.number().optional(),
    horizontal: z.boolean().optional(), ...edgeArgs },
  async (a) => {
    if ((a.x === undefined) !== (a.y === undefined)) return fail("give both x and y, or neither");
    const pos = a.x === undefined ? [] : [String(a.x), String(a.y)];
    const warnings = a.x === undefined ? [] : await edgeWarnings(a.pid, [{ label: "scroll position", x: a.x, y: a.y as number }], a.edge_margin);
    return reply(await runVinput([...at(a.pid), "scroll", String(a.notches), ...pos, ...opt("--horizontal", a.horizontal)], 60000), warnings.length ? { warnings } : undefined);
  });

// ---------------------------------------------------------------- keyboard / pad
const keyCombo = z.string().min(1).describe("Key or chord: 'enter', 'esc', 'space', 'w', 'f5', 'up', 'ctrl+s', 'shift+tab', 'numpad2', 'vk45'. list_keys shows every name.");
// ---- owned input jobs (vinput runs timed input on its own worker inside the game; capture and other tools keep working)
const asyncArg = z.boolean().optional().describe(
  "Return at once with a job_id instead of waiting for the input to finish; the hold keeps running inside the game (its own " +
  "deadline still releases it if this server goes away). Check it with job_status, stop it early with job_cancel. Accepted " +
  "means scheduled, not delivered. Needs a vinput build with input jobs in the game.");
const replaceArg = z.boolean().optional().describe("async: replace an overlapping job / held key instead of being refused.");
/** Hint added to an accepted async reply. */
function jobExtra(r: CliResult): Record<string, unknown> | undefined {
  const id = (r.json as { job_id?: unknown })?.job_id;
  return r.ok && typeof id === "string" ? { note: `running in the background as job ${id}: job_status to follow it, job_cancel to stop it` } : undefined;
}
tool("key", "Press a key",
  "Press and release a key or chord in the game (delivered on every channel the game may read: messages, raw input, " +
  "DirectInput, GetAsyncKeyState). hold_ms keeps it down longer (e.g. walking).",
  { pid, key: keyCombo, hold_ms: z.number().int().min(1).max(60000).optional().describe("How long the key stays down (default ~80)."),
    count: z.number().int().min(1).max(100).optional(), gap_ms: z.number().int().min(0).max(10000).optional().describe("Pause between repeats."),
    async: asyncArg, replace: replaceArg, capture_after: captureAfterSchema },
  async (a) => {
    if (a.async && !a.hold_ms) return fail("async needs hold_ms (a bounded hold)");
    if (a.async && (a.count ?? 1) > 1) return fail("async holds one press; count is not supported with async");
    const r = await runVinput([...at(a.pid), "key", a.key, ...(a.hold_ms ? [String(a.hold_ms)] : []), ...opt("--count", a.count), ...opt("--gap", a.gap_ms),
      ...opt("--async", a.async), ...opt("--replace", a.replace)], a.async ? 60000 : (a.hold_ms ?? 0) * (a.count ?? 1) + (a.gap_ms ?? 0) * (a.count ?? 1) + 60000);
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after);
  });

tool("key_down", "Hold a key down", "Press a key (or mouse button name like 'rbutton') and keep it down until key_up or release.",
  { pid, key: keyCombo }, async (a) => reply(await runVinput([...at(a.pid), "down", a.key], 30000)));

tool("key_up", "Release a key", "Release a key held with key_down.",
  { pid, key: keyCombo }, async (a) => reply(await runVinput([...at(a.pid), "up", a.key], 30000)));

tool("type_text", "Type text",
  "Type text into the focused text box of the game (WM_CHAR per character, any Unicode). as_keys:true presses real keys " +
  "instead (for games that only read raw keys).",
  { pid, text: z.string().min(1).max(4000), delay_ms: z.number().int().min(0).max(5000).optional(), as_keys: z.boolean().optional() },
  async (a) => {
    const cmd: Record<string, unknown> = { cmd: "type", text: a.text };
    if (a.delay_ms !== undefined) cmd.delay = a.delay_ms;
    if (a.as_keys) cmd.keys = true;
    return reply(await runVinput([...at(a.pid), "json", JSON.stringify(cmd)], (a.delay_ms ?? 30) * a.text.length + 60000));
  });

tool("pad", "Gamepad (XInput)",
  "Drive virtual XInput pad 0. buttons: '+'-joined names from a b x y lb rb start back up down left right ls rs guide, " +
  "or 'none'. Sticks -32768..32767, triggers 0..255. Held for ms (default 100), or until changed with hold:true. off:true " +
  "stops overriding the pad.",
  { pid, buttons: z.string().optional().describe("e.g. 'a', 'a+b', 'down', 'none'."),
    ms: z.number().int().min(1).max(60000).optional(),
    lx: z.number().int().min(-32768).max(32767).optional(), ly: z.number().int().min(-32768).max(32767).optional(),
    rx: z.number().int().min(-32768).max(32767).optional(), ry: z.number().int().min(-32768).max(32767).optional(),
    lt: z.number().int().min(0).max(255).optional(), rt: z.number().int().min(0).max(255).optional(),
    hold: z.boolean().optional(), off: z.boolean().optional(), async: asyncArg, replace: replaceArg, capture_after: captureAfterSchema },
  async (a) => {
    if (a.off) return look(a.pid, reply(await runVinput([...at(a.pid), "pad", "off"], 30000)), a.capture_after);
    if (a.async && (a.hold || !a.ms)) return fail("async needs ms (a bounded duration) and cannot be combined with hold");
    const r = await runVinput([...at(a.pid), "pad", a.buttons ?? "none", ...(a.ms ? [String(a.ms)] : []),
      ...opt("--lx", a.lx), ...opt("--ly", a.ly), ...opt("--rx", a.rx), ...opt("--ry", a.ry), ...opt("--lt", a.lt), ...opt("--rt", a.rt),
      ...opt("--hold", a.hold), ...opt("--async", a.async), ...opt("--replace", a.replace)], a.async ? 60000 : (a.ms ?? 0) + 60000);
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after);
  });

const axis = z.enum(["lx", "ly", "rx", "ry", "lt", "rt"]);
const stickV = z.number().int().min(-32768).max(32767), trigV = z.number().int().min(0).max(255);
const padConst = {
  lx: stickV.optional(), ly: stickV.optional(), rx: stickV.optional(), ry: stickV.optional(), lt: trigV.optional(), rt: trigV.optional(),
};
const pacingArg = z.enum(["frames", "time"]).optional().describe(
  "frames (default): advance at the game's presented frames, so a slow or paused game is not overrun (needs a game that presents " +
  "through D3D / GL / Vulkan); time: wall clock, for games vinput sees no frames from.");

tool("pad_ramp", "Ramp a pad axis",
  "Proportional steering: move ONE XInput axis smoothly from 'from' to 'to' over ms (e.g. ease the left stick from 0 to " +
  "16000 to turn gently), with the other axes held at the constants given, then hold the end value for end_hold_ms and " +
  "release. Runs as a job inside the game (async:true returns at once with a job_id). The reply reports updates, skipped " +
  "frames and lateness; it proves what the game was handed, not what it did.",
  { pid, axis, from: z.number().int().min(-32768).max(32767), to: z.number().int().min(-32768).max(32767),
    ms: z.number().int().min(1).max(118000).describe("Ramp duration."),
    ...padConst, buttons: z.string().optional().describe("Buttons held during the ramp, e.g. 'a' or 'a+rb'."),
    end_hold_ms: z.number().int().min(0).max(10000).optional().describe("Hold the final value this long before releasing (default 0)."),
    pacing: pacingArg, async: asyncArg, replace: replaceArg, capture_after: captureAfterSchema },
  async (a) => {
    if ((a.axis === "lt" || a.axis === "rt") && (a.from < 0 || a.from > 255 || a.to < 0 || a.to > 255)) return fail("trigger ramps run 0..255");
    if (a[a.axis] !== undefined) return fail(`${a.axis} is the ramped axis; give it only as from / to`);
    const cmd: Record<string, unknown> = { cmd: "pad-ramp", axis: a.axis, from: a.from, to: a.to, ms: a.ms };
    for (const k of ["lx", "ly", "rx", "ry", "lt", "rt"] as const) if (a[k] !== undefined) cmd[k] = a[k];
    if (a.buttons) cmd.buttons = a.buttons;
    if (a.end_hold_ms !== undefined) cmd.end_hold_ms = a.end_hold_ms;
    if (a.pacing) cmd.pacing = a.pacing;
    if (a.async) cmd.async = true;
    if (a.replace) cmd.replace = true;
    const r = await runVinput([...at(a.pid), "json", JSON.stringify(cmd)], a.async ? 60000 : a.ms + (a.end_hold_ms ?? 0) + 60000);
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after);
  });

const timelineState = z.object({
  at_ms: z.number().int().min(0).max(120000).describe("When this state starts, ms from the start (first state 0, strictly increasing)."),
  buttons: z.string().optional().describe("Digital buttons held in this state, e.g. 'a', 'a+lt' or 'none'. Omitted = none; every state is a complete snapshot."),
  phase: z.string().regex(/^[A-Za-z0-9_.-]{1,32}$/).optional().describe("Optional label (1..32 letters, digits, _ . -) echoed in the job receipt."),
  ...padConst,
}).strict();
tool("pad_timeline", "Timed pad script",
  "Play a list of XInput stick / trigger states at given times, e.g. [{at_ms:0,lx:0,ly:20000},{at_ms:400,lx:16000,ly:20000}," +
  "{at_ms:900,lx:0,ly:0}] = drive forward, steer right, stop. Axes not given in a state are 0. Runs as one job inside the " +
  "game; frame pacing never bunches overdue states (it reports skipped_states / max_lateness_ms), strict_frames fails instead " +
  "of skipping. ms = total duration if it should run past the last state (default: the last at_ms). At most 256 states. " +
  "stagger_ms (0..1000) delays a newly pressed button or trigger until after the stick change that precedes it has been " +
  "published (and, with frame pacing, a later presented frame); releases are never delayed.",
  { pid, states: z.array(timelineState).min(1).max(256),
    ms: z.number().int().min(0).max(120000).optional(), end_hold_ms: z.number().int().min(0).max(10000).optional(),
    stagger_ms: z.number().int().min(0).max(1000).optional().describe("Wait at least this long after a stick change before a new button / trigger press (needs a vinput DLL with pad-timeline stagger)."),
    pacing: pacingArg, strict_frames: z.boolean().optional(), async: asyncArg, replace: replaceArg, capture_after: captureAfterSchema },
  async (a) => {
    if (a.states[0].at_ms !== 0) return fail("the first state must have at_ms 0");
    for (let i = 1; i < a.states.length; i++) if (a.states[i].at_ms <= a.states[i - 1].at_ms) return fail("at_ms must increase strictly from state to state");
    if (a.strict_frames && a.pacing === "time") return fail("strict_frames needs frame pacing");
    const cmd: Record<string, unknown> = { cmd: "pad-timeline", states_json: JSON.stringify(a.states) };
    if (a.ms !== undefined) cmd.ms = a.ms;
    if (a.end_hold_ms !== undefined) cmd.end_hold_ms = a.end_hold_ms;
    if (a.pacing) cmd.pacing = a.pacing;
    if (a.strict_frames) cmd.strict_frames = true;
    if (a.stagger_ms !== undefined) cmd.stagger_ms = a.stagger_ms;
    if (a.async) cmd.async = true;
    if (a.replace) cmd.replace = true;
    const total = Math.max(a.ms ?? 0, a.states[a.states.length - 1].at_ms) + (a.end_hold_ms ?? 0);
    const r = await runVinput([...at(a.pid), "json", JSON.stringify(cmd)], a.async ? 60000 : total + 60000);
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after);
  });

const seqStep = z.object({
  action: z.enum(["hold", "key", "wait"]).describe("hold / key: press the chord for ms; wait: do nothing for ms."),
  key: keyCombo.optional().describe("Key or chord (hold / key only), e.g. 'w' or 'w+space'."),
  ms: z.number().int().min(1).max(118000),
}).strict();
tool("key_sequence", "Run a keyboard sequence",
  "Run a validated keyboard sequence as ONE background job inside the game: each step presses a key or chord for ms (hold / key) " +
  "or waits (wait), e.g. [{action:'hold',key:'w+space',ms:1500},{action:'wait',ms:100},{action:'key',key:'d',ms:300}]. Returns a job_id at once; " +
  "follow it with job_status (current step) or stop it with job_cancel, which releases the active chord. Keys of later steps are reserved, " +
  "so pressing one of them by hand cancels the rest. Limits: 1024 steps, 118000 ms of declared timing. Needs a vinput DLL with key sequences.",
  { pid, steps: z.array(seqStep).min(1).max(1024), replace: replaceArg },
  async (a) => {
    const lines: string[] = [];
    for (const [i, st] of a.steps.entries()) {
      if (st.action === "wait") { if (st.key !== undefined) return fail(`step ${i + 1}: wait takes no key`); lines.push(`wait ${st.ms}`); continue; }
      if (!st.key) return fail(`step ${i + 1}: ${st.action} needs a key`);
      if (/\s/.test(st.key)) return fail(`step ${i + 1}: key names cannot contain spaces`);
      lines.push(`${st.action} ${st.key} ${st.ms}`);
    }
    const dir = mkdtempSync(join(tmpdir(), "vinput-seq-"));
    const file = join(dir, "sequence.vin");
    try {
      writeFileSync(file, lines.join("\n") + "\n");
      const r = await runVinput([...at(a.pid), "seq", file, ...(a.replace ? ["--replace=true"] : [])], 60000);
      return reply(r, jobExtra(r));
    } finally { try { rmSync(dir, { recursive: true, force: true }); } catch { /* best effort */ } }
  });

tool("pulse", "Pulse a key (proportional)",
  "Key PWM for proportional control with digital keys: within each period_ms the key is down for duty percent (e.g. steer " +
  "gently with 'a' at duty 30). Runs for ms in total as a job inside the game. Late edges are coalesced, never bunched; the " +
  "game may still miss very short pulses, so use periods of 100 ms or more.",
  { pid, key: keyCombo, ms: z.number().int().min(20).max(118000).describe("Total duration."),
    duty: z.number().min(0).max(100).describe("Percent of each period the key is down."),
    period_ms: z.number().int().min(20).max(10000).optional().describe("PWM period (default 100)."),
    async: asyncArg, replace: replaceArg, capture_after: captureAfterSchema },
  async (a) => {
    const r = await runVinput([...at(a.pid), "pulse", a.key, String(a.ms), "--duty", String(a.duty), ...opt("--period-ms", a.period_ms),
      ...opt("--async", a.async), ...opt("--replace", a.replace)], a.async ? 60000 : a.ms + 60000);
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after);
  });

tool("hold_while", "Hold until text appears",
  "Hold a key (or pad buttons / sticks) and watch the screen: stops as soon as until_text appears (OCR), or after timeout_ms. " +
  "One call, no polling: e.g. hold 'w' until 'Village' shows. The hold always ends when the call returns (success, miss or " +
  "error). The text appearing is what was observed, not proof the hold caused it.",
  { pid, key: keyCombo.optional().describe("Key or chord to hold (give key, or pad buttons / sticks)."),
    pad_buttons: z.string().optional().describe("Pad buttons to hold instead of a key, e.g. 'a' or 'none' with sticks."),
    ...padConst, until_text: z.string().min(1).max(512),
    timeout_ms: z.number().int().min(100).max(118000).optional().describe("Give up after this long (default 5000)."),
    region: regionSchema.optional(), exact: z.boolean().optional(), fuzzy: z.number().int().min(0).max(3).optional() },
  async (a) => {
    const sticks = (["lx", "ly", "rx", "ry", "lt", "rt"] as const).filter((k) => a[k] !== undefined);
    const isPad = a.pad_buttons !== undefined || sticks.length > 0;
    if (isPad === (a.key !== undefined)) return fail("give either key, or pad_buttons / sticks");
    const argv = [...at(a.pid), isPad ? "pad-hold-while" : "hold-while", isPad ? (a.pad_buttons ?? "none") : (a.key as string),
      "--until-text", a.until_text, ...opt("--timeout-ms", a.timeout_ms), ...region(a.region), ...opt("--exact", a.exact), ...opt("--fuzzy", a.fuzzy),
      ...sticks.flatMap((k) => [`--${k}`, String(a[k])])];
    return reply(await runVinput(argv, (a.timeout_ms ?? 5000) + 60000));
  });

tool("job_status", "Input job status",
  "State of a background input job (from async key / pad / pulse / pad_ramp / pad_timeline / key_sequence / drag placement): job_state, " +
  "job_reason, job_complete, hold_ready, counts and timing. Without id: every job vinput still remembers for this game " +
  "(finished jobs are kept for about 60 s).",
  { pid, id: z.string().min(1).optional().describe("job_id from the async reply.") },
  async (a) => reply(await runVinput([...at(a.pid), ...(a.id ? ["job-status", a.id] : ["jobs"])], 15000)), true);

tool("job_cancel", "Cancel an input job",
  "Stop one background input job now and release exactly what it was holding (other held input is untouched). Use release to " +
  "drop everything instead.",
  { pid, id: z.string().min(1).describe("job_id from the async reply.") },
  async (a) => reply(await runVinput([...at(a.pid), "job-cancel", a.id], 15000)));

tool("trace", "Trace one key's delivery",
  "Per-key diagnosis: how many times vinput injected this key and through which channels the game actually read it " +
  "(window messages, GetAsyncKeyState, GetKeyState, DirectInput state / buffer, raw input). Press the key with the key tool, " +
  "then trace it: messages or raw deliveries with no read increase mean the game's polling never looked at that key. " +
  "Counters prove delivery, not that the game acted.",
  { pid, key: keyCombo }, async (a) => reply(await runVinput([...at(a.pid), "trace", "key", a.key], 15000)), true);

tool("list_keys", "List key names", "Every key name vinput accepts, with its virtual-key code.", {},
  async () => reply(await runVinput(["keys"], 15000)), true);

tool("run_script", "Run a macro script",
  "Run a .vin macro on the game: one vinput command per line (no --pid), '#' comments, 'sleep ms'. Stops at the first failing line unless continue_on_error.",
  { pid, file: z.string().min(1), continue_on_error: z.boolean().optional(), timeout_s: z.number().min(1).max(3600).optional() },
  async (a) => reply(await runVinput([...at(a.pid), "script", resolve(a.file), ...opt("--continue", a.continue_on_error)], (a.timeout_s ?? 600) * 1000)));

// ---------------------------------------------------------------- release
tool("release", "Release all input",
  "Release every key, mouse button and pad input this server is holding in the game.",
  { pid }, async (a) => reply(await runVinput([...at(a.pid), "release"], 30000)));

tool("detach", "Stop controlling a game",
  "Release all input and drop this server's claim. vinput stays loaded (inert) until the game exits; the game keeps running.",
  { pid }, async (a) => {
    const rel = await runVinput([...at(a.pid), "release"], 30000);
    const un = await runVinput([...at(a.pid), "unclaim"], 30000);
    return reply(rel, { unclaim: un.json, note: "vinput stays loaded until the game exits; no further input is sent" });
  });

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  process.stderr.write(`llm-game-automation-mcp ${VERSION} ready (vinput: ${resolveVinputExe()}, owner: ${OWNER})\n`);
}
main().catch((e) => { process.stderr.write(`fatal: ${(e as Error).stack ?? e}\n`); process.exit(1); });
