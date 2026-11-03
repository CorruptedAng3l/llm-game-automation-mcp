#!/usr/bin/env node
// llm-game-automation-mcp: an MCP (stdio) server that lets an LLM agent play single-player / offline Windows games
// through vinput. Every tool runs the vinput CLI and returns its JSON reply verbatim, with "ok" surfaced as isError.
// Offline games only: vinput injects a DLL and refuses targets with a known anti-cheat (see DISCLAIMER.md).
import { closeSync, fstatSync, openSync, readFileSync, readSync, statSync } from "node:fs";
import { resolve } from "node:path";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { configView, tailLog } from "./status.js";
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
function fail(message: string): ToolReply {
  return { content: [{ type: "text", text: JSON.stringify({ ok: false, error: message }) }], isError: true };
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
      "means the input was delivered, not that the game reacted: always verify. Never use it on online / multiplayer games " +
      "or games with anti-cheat; vinput refuses targets with a known anti-cheat and that refusal cannot be overridden.",
  },
);

function tool<S extends z.ZodRawShape>(name: string, title: string, description: string, shape: S,
  run: (a: z.objectOutputType<S, z.ZodTypeAny>) => Promise<ToolReply>, readOnly = false) {
  server.registerTool(name, { title, description, inputSchema: shape, annotations: { title, readOnlyHint: readOnly, openWorldHint: false } },
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (async (a: any) => {
      try { return await run(a); } catch (e) { return fail(`${name}: ${(e as Error).message}`); }
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
    args: z.string().optional().describe("Command line arguments for the game, e.g. \"-windowed -width 1280 -height 720\"."),
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
    const argv = ["--owner", OWNER, "launch",
      ...opt("--exe", a.exe), ...opt("--aumid", a.aumid), ...opt("--steam", a.steam_appid), ...opt("--wait-exe", a.wait_exe),
      ...opt("--args", a.args), ...opt("--early", a.early), ...opt("--settle", a.settle_s), ...opt("--timeout", a.timeout_s),
      ...opt("--lock", a.lock)];
    const r = await runVinput(argv, ((a.timeout_s ?? 240) + (a.settle_s ?? 5) + 120) * 1000);
    if (r.ok && a.claim) {
      const p = (r.json as { pid?: number }).pid;
      if (p) { const c = await runVinput([...at(p), "claim", "--ttl", "3600"], 15000); return reply(r, { claim: c.json }); }
    }
    return reply(r);
  });

tool("attach", "Attach to a running game",
  "Inject vinput into an already running game by process id (32- and 64-bit). Returns the game's status. Refused with " +
  "anticheat_refused:true if the game or the machine has a known anti-cheat. Use list_targets to see games that already have vinput.",
  { pid, claim: z.boolean().optional().describe("Claim the game for this MCP server (default false).") },
  async (a) => {
    const r = await runVinput(["inject", "--pid", String(a.pid)], 60000);
    if (!r.ok) return reply(r);
    if (a.claim) await runVinput([...at(a.pid), "claim", "--ttl", "3600"], 15000);
    for (let i = 0; i < 50; i++) {
      const s = await runVinput([...at(a.pid), "status"], 15000);
      const hwnd = (s.json as { hwnd?: string }).hwnd;
      if (!s.ok || (hwnd && hwnd !== "0x0") || i === 49) return reply(s, { injected: true });
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
    isolate: bool("Hide real background input from the game so the user's mouse and keyboard do not leak in (default on)."),
    blockfocus: bool("Stop the game taking the real foreground (default on)."),
    nofullscreen: bool("Refuse DXGI exclusive fullscreen (default on). Exclusive fullscreen loses its swapchain in the background."),
    keepvisible: bool("Undo a minimise of a background game (minimised games stop rendering) (default on)."),
    cursor: z.enum(["auto", "virtual", "real"]).optional().describe("Which cursor the game sees: virtual (vinput's own), real, or auto (decided per game)."),
    raw: z.enum(["auto", "post", "buffer", "off"]).optional().describe("How raw-input mouse deltas reach the game: post (WM_INPUT messages), buffer (GetRawInputBuffer), off, or auto."),
    msgs: z.enum(["auto", "on", "off"]).optional().describe("Whether window messages are sent for input: on, off, or auto."),
  },
  async (a) => {
    const keys = ["fakefocus", "autoactivate", "isolate", "blockfocus", "nofullscreen", "keepvisible", "cursor", "raw", "msgs"] as const;
    const want: Record<string, unknown> = {};
    for (const k of keys) if (a[k] !== undefined) want[k] = a[k];
    const r = await runVinput([...at(a.pid), "config", ...Object.entries(want).map(([k, v]) => `${k}=${v}`)], 30000);
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

tool("status", "Game status",
  "vinput status of one game: window, client size, capture path in use, really_foreground, claim, and counters showing " +
  "which input APIs the game actually reads (read these when the game ignores input).",
  { pid }, async (a) => reply(await runVinput([...at(a.pid), "status"], 15000)), true);

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
  "Returns the PNG as an image plus JSON {path, w, h, method, capture_path, frame}. Image pixels = client pixels, so " +
  "coordinates you read off the image can be passed straight to click / move. With region, the image is that crop; " +
  "pass capture_space:true to click / move to use coordinates inside the crop.",
  {
    pid,
    path: z.string().optional().describe("Where to save the PNG (default: a new file in the captures folder)."),
    region: regionSchema.optional(),
    after_frames: z.number().int().min(0).max(3600).optional().describe("Wait for this many new presented frames first (e.g. 3 after an action)."),
    method: z.enum(["auto", "native", "printwindow"]).optional().describe("auto (default) falls back to PrintWindow and says why in 'note'."),
    include_image: z.boolean().optional().describe("Return the image content (default true). False = path + JSON only."),
  },
  async (a) => {
    const path = resolve(a.path ?? defaultCapturePath(a.pid));
    const r = await runVinput([...at(a.pid), "capture", path, ...region(a.region), ...opt("--after-frames", a.after_frames), ...opt("--method", a.method === "native" ? "swapchain" : a.method)], 60000);
    const out = reply(r);
    if (r.ok && a.include_image !== false) {
      const saved = ((r.json as { path?: string }).path) || path;
      try {
        const size = statSync(saved).size;
        if (size > MAX_IMAGE_BYTES) out.content.push({ type: "text", text: `image not attached: ${size} bytes > ${MAX_IMAGE_BYTES}; use region to capture less` });
        else out.content.push({ type: "image", data: readFileSync(saved).toString("base64"), mimeType: "image/png" });
      } catch (e) { out.content.push({ type: "text", text: `capture reported ok but the PNG could not be read: ${(e as Error).message}` }); out.isError = true; }
    }
    return out;
  });

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
    optional: z.boolean().optional().describe("Succeed with skipped:true if the text never appears (for dialogs that may not show).") },
  async (a) => reply(await runVinput([...at(a.pid), "click-text", ...textArgs(a), ...opt("--timeout", a.timeout_ms), ...opt("--button", a.button),
    ...opt("--optional", a.optional)], (a.timeout_ms ?? 5000) + 60000)));

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

// ---------------------------------------------------------------- mouse
const xy = {
  x: z.number().describe("X in client pixels (or 0..1 with norm)."),
  y: z.number().describe("Y in client pixels (or 0..1 with norm)."),
  norm: z.boolean().optional().describe("x and y are fractions of the client size (0..1)."),
  capture_space: z.boolean().optional().describe("x and y are pixels of the last capture (use after a region capture)."),
};
tool("click", "Click",
  "Click a mouse button at a position (or at the current virtual cursor when x/y are omitted). The cursor hovers there " +
  "first so hover-dependent UI reacts. ok:true = the click was delivered; verify with capture / wait_text.",
  { pid, x: xy.x.optional(), y: xy.y.optional(), norm: xy.norm, capture_space: xy.capture_space, button: button.optional(),
    count: z.number().int().min(1).max(10).optional().describe("2 = double click."),
    hold_ms: z.number().int().min(1).max(10000).optional().describe("How long the button stays down (default 60).") },
  async (a) => {
    if ((a.x === undefined) !== (a.y === undefined)) return fail("give both x and y, or neither");
    const pos = a.x === undefined ? [] : [String(a.x), String(a.y)];
    return reply(await runVinput([...at(a.pid), "click", a.button ?? "left", ...pos, ...opt("--norm", a.norm), ...opt("--capture-space", a.capture_space),
      ...opt("--count", a.count), ...opt("--ms", a.hold_ms)], 60000));
  });

tool("move", "Move the cursor",
  "Move the game's virtual cursor to an absolute position (hover). The real cursor is not touched.",
  { pid, ...xy, steps: z.number().int().min(1).max(200).optional().describe("Intermediate moves (default 1).") },
  async (a) => reply(await runVinput([...at(a.pid), "mouse", String(a.x), String(a.y), ...opt("--norm", a.norm),
    ...opt("--capture-space", a.capture_space), ...opt("--steps", a.steps)], 60000)));

tool("move_relative", "Relative mouse motion",
  "Relative mouse motion (raw input / DirectInput deltas) for mouse-look / camera control in 3D games.",
  { pid, dx: z.number().int(), dy: z.number().int(), steps: z.number().int().min(1).max(200).optional(),
    raw: z.boolean().optional().describe("Do not clamp the virtual cursor to the window (games that re-centre the cursor).") },
  async (a) => reply(await runVinput([...at(a.pid), "move", String(a.dx), String(a.dy), ...opt("--steps", a.steps), ...opt("--raw", a.raw)], 60000)));

tool("drag", "Drag",
  "Press a mouse button at (x1,y1), move with it held, release at (x2,y2). Always releases, even if a step fails.",
  { pid, x1: z.number(), y1: z.number(), x2: z.number(), y2: z.number(),
    ms: z.number().int().min(1).max(30000).optional().describe("Duration of the drag (default 300)."), button: button.optional() },
  async (a) => reply(await runVinput([...at(a.pid), "drag", String(a.x1), String(a.y1), String(a.x2), String(a.y2),
    ...opt("--ms", a.ms), ...opt("--button", a.button)], 90000)));

tool("scroll", "Scroll the mouse wheel",
  "Scroll N wheel notches (positive = up / away, negative = down) at the virtual cursor, after moving it to x,y if given.",
  { pid, notches: z.number().int().min(-100).max(100), x: z.number().optional(), y: z.number().optional(),
    horizontal: z.boolean().optional() },
  async (a) => {
    if ((a.x === undefined) !== (a.y === undefined)) return fail("give both x and y, or neither");
    const pos = a.x === undefined ? [] : [String(a.x), String(a.y)];
    return reply(await runVinput([...at(a.pid), "scroll", String(a.notches), ...pos, ...opt("--horizontal", a.horizontal)], 60000));
  });

// ---------------------------------------------------------------- keyboard / pad
const keyCombo = z.string().min(1).describe("Key or chord: 'enter', 'esc', 'space', 'w', 'f5', 'up', 'ctrl+s', 'shift+tab', 'numpad2', 'vk45'. list_keys shows every name.");
tool("key", "Press a key",
  "Press and release a key or chord in the game (delivered on every channel the game may read: messages, raw input, " +
  "DirectInput, GetAsyncKeyState). hold_ms keeps it down longer (e.g. walking).",
  { pid, key: keyCombo, hold_ms: z.number().int().min(1).max(60000).optional().describe("How long the key stays down (default ~80)."),
    count: z.number().int().min(1).max(100).optional(), gap_ms: z.number().int().min(0).max(10000).optional().describe("Pause between repeats.") },
  async (a) => reply(await runVinput([...at(a.pid), "key", a.key, ...(a.hold_ms ? [String(a.hold_ms)] : []), ...opt("--count", a.count), ...opt("--gap", a.gap_ms)],
    (a.hold_ms ?? 0) * (a.count ?? 1) + (a.gap_ms ?? 0) * (a.count ?? 1) + 60000)));

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
    hold: z.boolean().optional(), off: z.boolean().optional() },
  async (a) => {
    if (a.off) return reply(await runVinput([...at(a.pid), "pad", "off"], 30000));
    return reply(await runVinput([...at(a.pid), "pad", a.buttons ?? "none", ...(a.ms ? [String(a.ms)] : []),
      ...opt("--lx", a.lx), ...opt("--ly", a.ly), ...opt("--rx", a.rx), ...opt("--ry", a.ry), ...opt("--lt", a.lt), ...opt("--rt", a.rt),
      ...opt("--hold", a.hold)], (a.ms ?? 0) + 60000));
  });

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
