#!/usr/bin/env node
// llm-game-automation-mcp: an MCP (stdio) server that lets an LLM agent play single-player / offline Windows games
// through vinput. Vinput-backed tools return CLI JSON with "ok" surfaced as isError; wait uses only a host timer.
// Offline games only: vinput injects a DLL and refuses targets with a known anti-cheat (see DISCLAIMER.md).
import { sizeMismatchWarning } from "./launchsize.js";
import { MOVE_DIRECTIONS, moveVector, smoothMoveStates, calibratedLook, rotateStick, driveStates } from "./controller.js";
import { closeSync, fstatSync, mkdtempSync, openSync, readFileSync, readSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { drawGrid, imageOutputSize, resample, toClient, type Mapping } from "./imgtools.js";
import { decodePng, encodePng } from "./png.js";
import { compactStatus, configView, diagnoseFrames, tailLog } from "./status.js";
import { confirmExit, exitMessage, forgetExit, knownExit, watchTarget } from "./exit.js";
import { CliResult, defaultCapturePath, resolveVinputExe, runVinput } from "./vinput.js";
import { completedJobView, isCompletedJobOutcome } from "./completedjob.js";
import { burstImage } from "./burstimage.js";
import { requireSequenceWait, sequenceWaitError, waitSequence, sequenceJob } from "./sequencewait.js";
import { requireTextBoundary } from "./textboundary.js";
import { keyScheduleLines } from "./keyschedule.js";
import { withMouseModifiers } from "./mousemodifiers.js";
import { presentOcr } from "./ocr.js";
import { presentInputReads } from "./inputreads.js";
import { presentMouseDiagnostics } from "./actiondiagnostics.js";
import {verifiedClick,clickScopeCapability} from './clickverify.js';
import { CaptureMappings, nativeMapping } from "./capturemapping.js";
import { registerCameraHelpers } from "./camerahelpers.js";
import { registerNavigationMotion } from "./navigationmotion.js";
import { frameworkClickTiming } from "./frameworkclick.js";
import { waitTextQuery, presentWaitTextAny } from "./waittextany.js";
import { registerAgentLoops, noteOtherInput } from "./agentloops.js";
import { loadedLaunchFailure,launchReadinessNote } from "./launchstate.js";
import { launcherFollowAction } from "./launcherfollow.js";
import {captureDestination,destinationError,type CaptureDestination} from './capturedestination.js';
import {compactRenderBackends} from './backendview.js';
import {measureTimedInput} from './holdmotion.js';
import { prepareSequenceConditions, guardSequence, type PreparedConditions } from "./sequenceconditions.js";
import { compactEnabled, compactReply } from "./compactreply.js";
import {prepareCapturePlan,sequenceFollowAction} from './sequencecaptures.js';

const VERSION = "0.3.0";
const OWNER = (process.env.VINPUT_OWNER ?? "").trim() || `mcp-${process.pid}`;
const MAX_IMAGE_BYTES = Number(process.env.VINPUT_MCP_MAX_IMAGE_BYTES ?? 8 * 1024 * 1024);

type Content = { type: "text"; text: string } | { type: "image"; data: string; mimeType: string };
type ToolReply = { content: Content[]; isError?: boolean };

function reply(r: CliResult, extra?: Record<string, unknown>): ToolReply {
  r=presentMouseDiagnostics(presentInputReads(r));
  let json = r.json;
  if (extra && json && typeof json === "object" && !Array.isArray(json)) json = { ...(json as object), ...extra };
  json = completedJobView(json, r.ok);
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
/** The same typed P5 contract is used after attach or launch; no input fallback. */
async function neutralPadArm(pid: number, waitMs: number) {
  const r = await runVinput([...at(pid), "json", JSON.stringify({ cmd: "pad-arm", wait_ms: waitMs })], 10000);
  const j = r.json as { pad_readiness_version?: unknown; arm_accepted?: unknown; pad_armed?: unknown;
    poll_observed?: unknown; neutral_return_observed?: unknown; superseded?: unknown } | null;
  const acknowledged = r.ok && !!j && j.pad_readiness_version === 1 && j.arm_accepted === true &&
    typeof j.pad_armed === "boolean" && typeof j.poll_observed === "boolean" && typeof j.superseded === "boolean" &&
    j.pad_armed !== j.superseded &&
    (j.poll_observed ? typeof j.neutral_return_observed === "boolean" : j.neutral_return_observed === null) &&
    (!j.poll_observed || (j.pad_armed && !j.superseded));
  return { r, acknowledged };
}
const region = (r?: { x: number; y: number; w: number; h: number }) => (r ? ["--region", `${r.x},${r.y},${r.w},${r.h}`] : []);
const opt = (flag: string, v: unknown) => (v === undefined || v === null || v === false ? [] : v === true ? [flag] : [flag, String(v)]);

const pid = z.number().int().positive().describe("Process id of the game, as returned by launch / attach / list_targets.");
const regionRect = z.object({
  x: z.number().int().min(0), y: z.number().int().min(0), w: z.number().int().positive(), h: z.number().int().positive(),
});
const regionSchema = z.union([regionRect,
  z.tuple([regionRect.shape.x, regionRect.shape.y, regionRect.shape.w, regionRect.shape.h])
    .transform(([x, y, w, h]) => ({ x, y, w, h })),
]).describe("Rectangle in client pixels: {x,y,w,h} or [x,y,w,h]. Integer top-left x/y >=0 and positive integer width/height; both forms are identical.");
const button = z.enum(["left", "right", "middle", "x1", "x2"]);
const timeoutMs = (def: number) => z.number().int().min(0).max(600000).optional().describe(`How long to keep looking, in ms (default ${def}).`);

const server = new McpServer(
  { name: "llm-game-automation-mcp", version: VERSION },
  {
    instructions:
      "Plays single-player / offline Windows games through vinput: input and screenshots go to ONE game process in the " +
      "background, so the game never needs focus and the user keeps the mouse and keyboard. Workflow: launch (or attach) -> " +
      "capture to look -> act (click_text / click / key / pad) -> verify with wait_text, capture or wait_change. " +
      "For repeated placement use click_many/click_grid; wait_text accepts any-of banners; click_to_move checks scoped pixel change/stability with one bounded hold and unknown displacement. " +
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
  run: (a: z.objectOutputType<S, z.ZodTypeAny>, runtime?: {signal?:AbortSignal}) => Promise<ToolReply>, readOnly = false) {
  if(name==='type_text')tool('type',title,'Alias of type_text. '+description,shape,run,readOnly);
  server.registerTool(name, { title, description, inputSchema: { ...shape,
    compact: z.boolean().optional().describe("Return a short presentation: action/job state, observed reader channels, warnings and image path. Images are retained; application response stays unknown. Defaults to VINPUT_MCP_COMPACT; false requests full detail.") }, annotations: { title, readOnlyHint: readOnly, openWorldHint: false } },
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (async (a: any, extra: {signal?:AbortSignal}) => {
      const terse = compactEnabled(a?.compact);
      const present = (out: ToolReply) => compactReply(out, terse, name);
      // Presentation never reaches native requests or changes handler decisions.
      const { compact: _compact, ...input } = a ?? {};
      a = input;
      const p: number | undefined = typeof a?.pid === "number" ? a.pid : undefined;
      try {
        if (p !== undefined) {
          watchTarget(p);
          if (name === "attach" || name === "detach") advanceCaptureSession(p);
          const k = name === "attach" ? undefined : knownExit(p);   // attach may be a reused pid number
          if (k) return present(exitedReply(p, k.code));
          if(!readOnly&&!['click_many','click_grid','click_to_move'].includes(name))noteOtherInput(p);
        }
        const out = await run(a,{signal:extra?.signal});
        if (p !== undefined && out.isError) {
          const ex = await confirmExit(p);
          if (ex) {
            try {const j=JSON.parse((out.content[0] as {text:string}).text);if(j.input_session||j.input_receipt||j.input_read_observation){out.content[0]={type:'text',text:JSON.stringify({...j,target_exited:true,pid:p,exit_code:ex.code})};return present(out);}}catch{/* Existing pre-input/malformed replies keep normal exit behavior. */}
            if (name === "key_sequence" && a.wait === true) {
              try {
                const j = JSON.parse((out.content[0] as { text: string }).text);
                if (j.sequence_input && j.sequence_wait) {
                  out.content[0] = { type: "text", text: JSON.stringify({ ...j, target_exited: true, pid: p, exit_code: ex.code }) };
                  return present(out);
                }
              } catch { /* Pre-action or malformed replies keep the existing exit behavior. */ }
            }
            return present(exitedReply(p, ex.code));
          }
        }
        return present(out);
      } catch (e) { return present(fail(`${name}: ${(e as Error).message}`)); }
    }) as never);
}

// ---------------------------------------------------------------- targets
tool("launch", "Launch a game",
  "Start a game with optional args (for example '-windowed') and inject vinput into its real window-owning process. Returns {ok, pid, injected, followed_from, foreground_restored, ...}; keep the pid for every " +
  "other tool. Give exactly one of exe (a .exe path), aumid (a UWP / Xbox app id 'PackageFamilyName!AppId') or " +
  "steam_appid (+ wait_exe). Run games WINDOWED or borderless (exclusive fullscreen breaks background capture). Use " +
  "early:true for Vulkan / OpenGL games so capture hooks are in before the device is created (exe only). Refused with " +
  "anticheat_refused:true if the game or the machine has a known anti-cheat. Interactive Steam launchers return launch_state:'launcher_ready' and waiting_for; click Play, then call launch with follow_pid, follow_created and wait_exe from that receipt to adopt the game without starting another copy. For Steam load error 5:0000065434 use steam_appid + wait_exe.",
  {
    exe: z.string().optional().describe("Full path of the game executable."),
    verbose:z.boolean().optional().describe('Return inactive backend diagnostic blocks too. Default omits inactive ddraw/d3d9 blocks only when another frame backend is known and the block has no activity or failure evidence.'),
    follow_pid: z.number().int().positive().optional().describe("Continue an existing launch after clicking Play; use instead of exe/aumid/steam_appid. Does not start another process."),
    follow_created: z.string().regex(/^\d+$/).optional().describe("process_created from the launcher receipt; decimal string retains its identity after the launcher exits."),
    args: z.string().optional().describe("Command line arguments for the game, e.g. \"-windowed -width 1280 -height 720\". Many games ignore size switches because a saved setting wins: launch then warns (warnings, requested_size_ignored) when the window's client size differs from the size you asked for, so set the size in the game's own options."),
    cwd: z.string().optional().describe("Working directory for the game (exe launches only). Default: the exe's own folder, which is what most old games need, so leave it out unless the game wants something else."),
    cpu_affinity: z.string().regex(/^0[xX][0-9a-fA-F]+$/).optional().describe("Optional nonzero hexadecimal CPU mask for direct exe launches, applied before the first instruction. Example: '0xF' uses four cores, matching Prototype's Restrict Cores.bat. Does not apply to Steam/AUMID/follow_pid launches or assert a relaunched child's affinity."),
    aumid: z.string().optional().describe("UWP / Xbox app user model id, e.g. 'Publisher.Game_abc123!App'."),
    steam_appid: z.string().optional().describe("Steam app id; starts the game through Steam (needs wait_exe)."),
    wait_exe: z.string().optional().describe("Executable name to wait for, e.g. 'Game-Win64-Shipping.exe' (launchers, Steam relaunches)."),
    early: z.boolean().optional().describe("Create the process suspended and inject before its first instruction (exe only)."),
    show_hidden:z.boolean().optional().describe('Explicitly request SW_SHOWNA for the exact adopted presenting hidden window (default false). One native UI-thread request with a5s expiry; inspect show_hidden_reply/status and window_hidden because queue admission does not confirm visibility.'),
    settle_s: z.number().min(0).max(600).optional().describe("Seconds to let the game settle after injection (default 5)."),
    timeout_s: z.number().min(1).max(1800).optional().describe("Seconds to wait for the game window (default 240)."),
    lock: z.string().optional().describe("Optional lock file shared by tools that may touch the real foreground; held only while launching."),
    claim: z.boolean().optional().describe("Claim the game for this MCP server so other vinput clients are refused (default false)."),
    claim_ttl_s: z.number().int().min(1).max(86400).optional().describe("Only with claim:true: native claim inactivity TTL in seconds, default 3600. Matching-owner input commands renew it; read-only status and host wait do not."),
    arm_pad: z.boolean().optional().describe("After successful launch, opt in to the same neutral user0 pad arming as attach. Busy state is preserved; no buttons, motion, hotplug or glyph guarantee."),
    pad_wait_ms: z.number().int().min(0).max(2000).optional().describe("Only with arm_pad:true: bounded wait for a completed virtual GetState/Ex return, default 500 ms. Arm failure preserves the launched target and is reported separately."),
  },
  async (a) => {
    if (a.pad_wait_ms !== undefined && !a.arm_pad) return fail("pad_wait_ms requires arm_pad:true; nothing was sent");
    if (a.claim_ttl_s !== undefined && !a.claim) return fail("claim_ttl_s requires claim:true; nothing was sent");
    const n = [a.exe, a.aumid, a.steam_appid, a.follow_pid].filter((v) => v).length;
    if (n !== 1) return fail("give exactly one of exe, aumid, steam_appid or follow_pid");
    if (a.follow_created !== undefined && !a.follow_pid) return fail("follow_created requires follow_pid");
    if (a.cpu_affinity !== undefined && !a.exe) return fail("cpu_affinity requires a direct exe launch");
    if (a.cpu_affinity !== undefined && /^0x0+$/i.test(a.cpu_affinity)) return fail("cpu_affinity must be nonzero");
    let cwd: string | undefined;
    if (a.cwd) {
      if (!a.exe) return fail("cwd only applies to exe launches (aumid / steam_appid games choose their own folder)");
      cwd = resolve(a.cwd);
      let isDir = false; try { isDir = statSync(cwd).isDirectory(); } catch { /* not there */ }
      if (!isDir) return fail(`cwd does not exist or is not a folder: ${cwd}`);
    }
    const argv = ["--owner", OWNER, "launch",
      ...opt("--exe", a.exe), ...opt("--aumid", a.aumid), ...opt("--steam", a.steam_appid), ...opt("--wait-exe", a.wait_exe),
      ...opt("--follow-pid", a.follow_pid), ...opt("--follow-created", a.follow_created),
      ...opt("--cpu-affinity", a.cpu_affinity),
      ...(a.args === undefined ? [] : [`--args=${a.args}`]), ...opt("--cwd", cwd), ...opt("--early", a.early),...opt('--show-hidden',a.show_hidden), ...opt("--settle", a.settle_s), ...opt("--timeout", a.timeout_s),
      ...opt("--lock", a.lock), ...opt("--claim-ttl", a.claim ? a.claim_ttl_s ?? 3600 : undefined)];
    const r = await runVinput(argv, ((a.timeout_s ?? 240) + (a.settle_s ?? 5) + 120) * 1000);
    { const np = (r.json as { pid?: unknown }).pid; if (typeof np === "number" && np > 0) { advanceCaptureSession(np); forgetExit(np); watchTarget(np); } }
    const candidatePid = (r.json as { pid?: unknown } | null)?.pid;
    const lp = r.ok && typeof candidatePid === "number" && Number.isSafeInteger(candidatePid) && candidatePid > 0 ? candidatePid : undefined;
    const extra: Record<string, unknown> = {};
    Object.assign(extra,launchReadinessNote(r.json));
    const follow=launcherFollowAction(r.json,a.claim===true,a.claim_ttl_s);if(follow)extra.next_action=follow;
    if (!r.ok) Object.assign(extra, loadedLaunchFailure(r.json, a.claim === true, a.claim_ttl_s));
    let claimOk = true, compoundOk = r.ok;
    if (r.ok && a.claim && !lp) {
      compoundOk = claimOk = false; extra.claim_ok = false;
      extra.error = "Launch succeeded without a valid positive integer pid; the requested claim was not sent.";
    }
    if (lp && a.claim) {
      const c = await runVinput([...at(lp), "claim", "--ttl", String(a.claim_ttl_s ?? 3600)], 15000);
      extra.claim = c.json; extra.claim_ok = claimOk = c.ok;
      if (!claimOk) {
        compoundOk = false;
        extra.error = "The target launched successfully, but the requested claim failed. Keep this PID; do not relaunch it. Inspect claim.";
      }
    }
    if (r.ok && a.arm_pad) {
      extra.pad_arm_attempted = !!lp && claimOk;
      if (lp && claimOk) {
        const arm = await neutralPadArm(lp, a.pad_wait_ms ?? 500);
        extra.pad_arm = arm.r.json; extra.pad_arm_acknowledged = arm.acknowledged;
        if (!arm.acknowledged) {
          compoundOk = false;
          extra.error = extra.pad_arm_error = "The target launched successfully, but neutral pad arming was refused or unconfirmed. Keep this PID; do not relaunch it. Inspect pad_arm. No input fallback or retry was attempted.";
        }
      } else {
        compoundOk = false; extra.pad_arm_acknowledged = false;
        extra.pad_arm_error = lp ? "Launch succeeded, but claim failed; neutral pad arming was not sent." : "Launch succeeded without a valid positive integer pid; neutral pad arming was not sent.";
        if (!extra.error) extra.error = extra.pad_arm_error;
      }
    }
    if (lp && (r.json as {input_ready?:unknown}).input_ready!==false) Object.assign(extra, await frameHealth(lp, a.args));
    else if(lp)extra.render_health_unmeasured_reason='input queue readiness is unconfirmed; full status may require its UI thread; inspect native launch/presentation receipts';
    if (r.ok && !compoundOk) { extra.ok = false; extra.launch_ok = true; extra.launched = true; }
    const presented={...(r.json as Record<string,unknown>),...extra};return reply({ ...r, ok: compoundOk,json:a.verbose?presented:compactRenderBackends(presented) });
  });

tool("claim", "Claim or renew a game",
  "Claim this target for the current MCP owner, or renew that same owner's claim. Another active owner is refused; this tool never forces takeover. TTL is inactivity time: matching-owner input renews it, read-only status/host wait do not.",
  { pid, claim_ttl_s: z.number().int().min(1).max(86400).optional().describe("Native claim inactivity TTL in seconds 1..86400 (default 3600). Keep VINPUT_OWNER stable across one-shot MCP servers.") },
  async (a) => reply(await runVinput([...at(a.pid), "claim", "--ttl", String(a.claim_ttl_s ?? 3600)], 15000)));

tool("attach", "Attach to a running game",
  "Inject vinput into an already running game by process id (32- and 64-bit). Returns the game's status. Refused with " +
  "anticheat_refused:true if the game or the machine has a known anti-cheat. Use list_targets to see games that already have vinput.",
  { pid, claim: z.boolean().optional().describe("Claim the game for this MCP server (default false)."),
    claim_ttl_s: z.number().int().min(1).max(86400).optional().describe("Only with claim:true: native inactivity TTL in seconds, default 3600; matching-owner input renews it."),
    arm_pad: z.boolean().optional().describe("Opt in to a neutral virtual XInput user0 contribution. Preserves existing holds/jobs by refusing busy state; this is not hotplug or glyph/application readiness. Default false."),
    pad_wait_ms: z.number().int().min(0).max(2000).optional().describe("Only with arm_pad:true: bounded wait for an actual completed user0 virtual GetState/Ex return, default500ms. No game reaction is implied.") },
  async (a) => {
    if (a.pad_wait_ms !== undefined && !a.arm_pad) return fail("pad_wait_ms requires arm_pad:true; nothing was sent");
    if (a.claim_ttl_s !== undefined && !a.claim) return fail("claim_ttl_s requires claim:true; nothing was sent");
    forgetExit(a.pid);
    const r = await runVinput(["inject", "--pid", String(a.pid)], 60000);
    if (!r.ok) return reply(r);
    if (a.claim) {
      const c = await runVinput([...at(a.pid), "claim", "--ttl", String(a.claim_ttl_s ?? 3600)], 15000);
      if (!c.ok) return reply(c, { pid: a.pid, injected: true, claim: c.json });
    }
    let padArm: unknown;
    if (a.arm_pad) {
      const { r: p, acknowledged } = await neutralPadArm(a.pid, a.pad_wait_ms ?? 500);
      if (!p.ok) return reply(p, { injected: true, pad_arm: p.json });
      if (!acknowledged) return fail("Injected target did not acknowledge neutral pad arming; use matching vinput DLL. No controller readiness is claimed.");
      padArm = p.json;
    }
    for (let i = 0; i < 50; i++) {
      const s = await runVinput([...at(a.pid), "status"], 15000);
      const hwnd = (s.json as { hwnd?: string }).hwnd;
      if (!s.ok || (hwnd && hwnd !== "0x0") || i === 49) return statusReply(s, false, { injected: true, ...(padArm === undefined ? {} : { pad_arm: padArm }) });
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
  "counters that are non-zero (they show which APIs the game actually reads: read them when it ignores input), " +
  "virtual_down (published virtual VK codes, including mouse buttons; not physical state or game consumption), and " +
  "warnings[] (fullscreen refusal loop, no window, minimized, failed hooks). verbose:true returns vinput's full status " +
  "(about 3 KB: every counter, hook lists, per-API frame counts, Vulkan / D3D12 internals).",
  { pid, verbose: z.boolean().optional().describe("Return the full vinput status instead of the compact view.") },
  async (a) => statusReply(await runVinput([...at(a.pid), "status"], 15000), a.verbose), true);

tool("doctor", "Diagnose a game connection",
  "Read-only native CLI diagnostics: process/architecture, reachable status, observed input-channel counters, " +
  "capture/foreground/claim state and native suggestions. Optional key adds its read trace. Counters show activity " +
  "since injection, not the game's selected backend or successful actions. No injection, input, claim renewal or repair is attempted.",
  { pid, key: z.string().min(1).max(64).optional().describe("Optional single key name or VK (e.g. right, enter, vk39) for native trace diagnostics.") },
  async (a) => reply(await runVinput([...at(a.pid), "doctor", ...opt("--key", a.key)], 15000)), true);

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
const captureImageArgs = {
  region: regionSchema.optional(),
  max_width: z.number().int().min(64).max(8192).optional().describe(
    "Downscale the RETURNED image to at most this many pixels wide (never upscales) to save tokens, e.g. 960. The reply's image " +
    "{scale, offset_x, offset_y} maps it back: client = offset + image / scale. click / move / drag take coord_space:\"capture\" to " +
    "do that for you."),
  scale: z.number().min(0.05).max(64).optional().describe("Explicit resize factor 0.05..64 for the returned PNG (0.5 = half size). Output remains bounded to 32768 per side and 64 million pixels. Give only one of scale, max_width / zoom."),
  upscale: z.boolean().optional().describe('With max_width, explicitly resize a PNG crop up to that width as well as shrinking. Default false preserves the width cap. Enlargement applies to the returned preview, with an updated image-to-client mapping.'),
  save_path: z.string().min(1).optional().describe('Exact saved capture path; alias of capture.path. Give only one destination.'),
  out_dir: z.string().min(1).optional().describe('Save a uniquely named capture in this directory (created if needed), instead of the default captures folder.'),
  format: z.enum(["png", "jpeg"]).optional().describe("png (default, exact pixels) or jpeg (much smaller, lossy: do not use it to compare colours). jpeg needs a vinput build with capture output support and cannot be combined with grid or zoom (both are drawn on the decoded PNG; for a small labelled image use format png with max_width or scale instead)."),
  quality: z.number().int().min(1).max(100).optional().describe("JPEG quality 1..100 (default 85)."),
};
type CaptureImageOptions = CaptureDestination & { region?: z.infer<typeof regionSchema>; max_width?: number; scale?: number; upscale?:boolean;format?: "png" | "jpeg"; quality?: number };
tool("capture", "Capture a frame",
  "Screenshot of the game's own frame (D3D11/D3D12/OpenGL/Vulkan swapchain, PrintWindow fallback) taken in the background. " +
  "Returns the PNG (or JPEG) as an image plus JSON {path, w, h, method, capture_path, frame}. Unscaled image pixels = client " +
  "pixels, so coordinates you read off the image can be passed straight to click / move. With region, the image is that crop; " +
  "pass capture_space:true to click / move to use coordinates inside the crop (after max_width / scale: inside the scaled image). " +
  "max_width / scale <= 1 / format jpeg are done by vinput itself when the game's vinput supports it (image.scaled_by " +
  "\"vinput\"), else by this server (\"mcp\").",
  {
    pid, ...captureImageArgs,
    path: z.string().optional().describe("Where to save the PNG (default: a new file in the captures folder)."),
    after_frames: z.number().int().min(0).max(3600).optional().describe("Wait for this many new presented frames first (e.g. 3 after an action)."),
    method: z.enum(["auto", "native", "printwindow"]).optional().describe("auto (default) falls back to PrintWindow and says why in 'note'."),
    include_image: z.boolean().optional().describe("Return the image content (default true). False = path + JSON only."),
    grid: z.number().int().min(10).max(2000).optional().describe(
      "Draw a labelled coordinate grid on the RETURNED image only (the saved PNG stays clean): a line every N client pixels, " +
      "labelled with the client x (top edge) and y (left edge). Read exact coordinates off it instead of guessing: 100 for a " +
      "full frame, 20-50 inside a region."),
    zoom: z.number().min(1).max(64).optional().describe("Enlarge the returned PNG 1..64 times; output is bounded to 32768 per side and 64 million pixels. Combine with max_width to cap zoomed width. Not with scale."),
  },
  async (a) => {
    if (a.scale !== undefined && (a.max_width !== undefined || a.zoom !== undefined)) return fail("give only one of scale, max_width / zoom (max_width may be combined with zoom as a cap on the zoomed width)");
    const v = validateFormat(a);
    if (v) return fail(v);
    const pathError=destinationError(a);if(pathError)return fail(pathError);
    const g = await grabFrame(a.pid, a);
    return g.out;
  });

function validateFormat(a: { format?: "png" | "jpeg"; quality?: number; grid?: number; zoom?: number; scale?: number;upscale?:boolean;max_width?:number;include_image?:boolean }): string | undefined {
  if(a.upscale&&(a.format==='jpeg'||a.max_width===undefined||a.scale!==undefined||a.zoom!==undefined||a.include_image===false))return 'upscale:true requires max_width and an included PNG preview, without scale/zoom; JPEG supports shrinking only';
  if (a.quality !== undefined && a.format !== "jpeg") return "quality applies only to format jpeg";
  if (a.format === "jpeg" && a.grid !== undefined) return "grid needs format png: the grid is drawn on the decoded image and only PNG can be decoded here. Drop format (png) and shrink with max_width or scale instead, or keep jpeg and drop grid (read coordinates from a png capture first)";
  if (a.format === "jpeg" && a.zoom !== undefined) return "zoom needs format png: the enlargement is done on the decoded image and only PNG can be decoded here. Drop format (png) and cap the size with max_width, or keep jpeg and drop zoom (use max_width / scale with jpeg)";
  if (a.format === "jpeg" && a.scale !== undefined && a.scale > 1) return "scale > 1 needs format png: JPEG supports shrinking only. Use format png with scale/zoom and max_width, or keep jpeg with scale <= 1";
  return undefined;
}

/** Mapping of the last image returned per game (image_px = (client_px - offset) * scale), for coord_space:"capture". */
const lastMapping = new CaptureMappings();
// Cooperative attachment epoch only; this is not OS process-creation identity.
const captureSessionEpoch = new Map<number, number>();
function advanceCaptureSession(pid: number) { captureSessionEpoch.set(pid, (captureSessionEpoch.get(pid) ?? 0) + 1);lastMapping.clear(pid); }

/** One frame grab: runs vinput capture, attaches the PNG, and diagnoses "nothing presented / fullscreen refused" when the
 *  native path was not used. Shared by capture and the capture_after option of the action tools. */
async function grabFrame(pid: number, o: CaptureImageOptions & { path?: string; after_frames?: number; method?: "auto" | "native" | "printwindow"; include_image?: boolean; grid?: number; zoom?: number }) {
  const captureEpoch=captureSessionEpoch.get(pid)??0;
  const jpeg = o.format === "jpeg";
  const path = captureDestination(o,pid,jpeg?'jpg':'png',()=>defaultCapturePath(pid,jpeg?'jpg':'png'));
  const base = [...at(pid), "capture", path, ...region(o.region), ...opt("--after-frames", o.after_frames), ...opt("--method", o.method === "native" ? "swapchain" : o.method)];
  // vinput's own output transform (capture_output_version 1): it can only shrink, and it also re-bases vinput's capture-space
  // map on the saved image. zoom (enlarging) stays here.
  const coreArgs = o.zoom !== undefined||o.upscale ? [] : [
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
  const captureReceipt=r.json as {capture_observation?:unknown;capture_freshness_warning?:unknown};
  let extra: Record<string, unknown> = {...(o.region?{requested_region:o.region,region_coordinate_system:'client_pixels'}:{}),
    ...(captureReceipt.capture_observation&&typeof captureReceipt.capture_observation==='object'?{capture_observation:captureReceipt.capture_observation}:{}),
    ...(typeof captureReceipt.capture_freshness_warning==='string'?{capture_freshness_warning:captureReceipt.capture_freshness_warning}:{})};
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
  const nativeMap = nativeMapping(r.json,captureEpoch,pid);
  let persistedMap=nativeMap;
  const mapping: Mapping = nativeMap ? {...nativeMap.mapping} : { scale: 1, offset_x: o.region?.x ?? 0, offset_y: o.region?.y ?? 0 };
  let imageInfo: Record<string, unknown> | undefined;
  if (!nativeMap && scaledBy === "vinput") {
    // vinput reports the client rectangle the saved image covers: image_px = (client_px - origin) * w / region_client_w
    const j = r.json as { w?: number; region_client_w?: number; origin_x?: number; origin_y?: number };
    if (j.w && j.region_client_w) mapping.scale = j.w / j.region_client_w;
    if (typeof j.origin_x === "number") mapping.offset_x = j.origin_x;
    if (typeof j.origin_y === "number") mapping.offset_y = j.origin_y;
  }
  let finalW=nativeMap?.imageW??(r.json as {w?:number}).w,finalH=nativeMap?.imageH??(r.json as {h?:number}).h;
  const baseMapping={...mapping};
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
          if (wantW !== undefined && (o.upscale||im.width * factor > wantW)) factor = wantW / im.width;
          if (factor !== 1) {
            const { width: nw, height: nh } = imageOutputSize(Math.max(1, Math.round(im.width * factor)), Math.max(1, Math.round(im.height * factor)));
            mapping.scale = baseMapping.scale * nw / im.width;
            mapping.scale_y = (baseMapping.scale_y??baseMapping.scale) * nh / im.height;
            im = resample(im, nw, nh);
          }
          if (o.grid) im = drawGrid(im, mapping, o.grid);
          if (o.grid || factor !== 1) png = encodePng(im);
          w = im.width; h = im.height;
        } catch (e) {
          imgErr = `could not process the image (${(e as Error).message}); the original image is returned unchanged`;
          if (scaledBy === "mcp") { Object.assign(mapping,baseMapping); scaledBy = undefined; }
        }
      }
      imageInfo = { w, h, ...mapping, ...(scaledBy ? { scaled_by: scaledBy } : {}), ...(jpeg ? { format: "jpeg" } : {}),
        ...(o.grid && !imgErr ? { grid: o.grid } : {}), ...(imgErr ? { image_note: imgErr } : {}) };
      finalW=w;finalH=h;
    } catch (e) { imgErr = `capture reported ok but the image could not be read: ${(e as Error).message}`; }
  }
  if(r.ok&&(captureSessionEpoch.get(pid)??0)===captureEpoch){
    if(nativeMap&&finalW&&finalH&&(finalW!==nativeMap.imageW||finalH!==nativeMap.imageH)){
      const resized=await runVinput([...at(pid),'json',JSON.stringify({cmd:'capture-map-resize',capture_map_id:nativeMap.mapId,expected_capture_scope:nativeMap.scope,image_w:finalW,image_h:finalH})],2000);
      persistedMap=resized.ok?nativeMapping(resized.json,captureEpoch,pid):undefined;
      if(!persistedMap)extra.capture_mapping_warning='postprocessed image mapping was not persisted; pass the returned mapping explicitly across MCP sessions';
    }
    if((captureSessionEpoch.get(pid)??0)===captureEpoch){
      lastMapping.remember(pid,persistedMap??{mapping:{...mapping},epoch:captureEpoch,imageW:finalW,imageH:finalH,...(nativeMap?{scope:nativeMap.scope}:{})});
      extra.mapping={...mapping};extra.capture_mapping_persisted=!!persistedMap;
      if(!nativeMap)extra.capture_mapping_warning='this DLL did not confirm persisted capture mapping; another MCP session requires an explicit mapping';
    }else extra.capture_mapping_warning='attachment changed during capture mapping publication';
  }
  if (fallbackNote) extra = { ...extra, notes: [...((extra.notes as string[] | undefined) ?? []), fallbackNote] };
  const out = reply(r, { ...extra, ...(imageInfo ? { image: imageInfo } : {}), ...(imageInfo?.image_note ? { image_note: imageInfo.image_note } : {}) });
  if (png) {
    if (png.length > MAX_IMAGE_BYTES) out.content.push({ type: "text", text: `image not attached: ${png.length} bytes > ${MAX_IMAGE_BYTES}; use region, max_width or format jpeg to capture less` });
    else out.content.push({ type: "image", data: png.toString("base64"), mimeType: jpeg ? "image/jpeg" : "image/png" });
  } else if (imgErr) { out.content.push({ type: "text", text: imgErr }); out.isError = true; }
  const j = r.json as Record<string, unknown>;
  const summary = r.ok ? { path: j.path, w: j.w, h: j.h, method: j.method, capture_path: j.capture_path, frame: j.frame, ...extra, ...(imageInfo && (o.region !== undefined || o.max_width !== undefined || o.scale !== undefined || o.format !== undefined) ? { image: imageInfo } : {}) } : undefined;
  return { r, out, saved, summary };
}

const captureAfterSchema = z.union([z.boolean(), z.number().int().min(0).max(3600)]).optional().describe(
  "Look right after acting: true waits for 3 presented frames, with one explicitly reported fresh PrintWindow fallback if that wait times out and status shows no target Present stream; a number is a FRAME COUNT, never milliseconds, and strictly waits for that many frames " +
  "first (0 = capture immediately). Use observe_settle for bounded scoped stable-frame observation after the action; capture_after itself is one image. The reply then includes the frame as an inline image plus capture_after {path, w, h, ...}, " +
  "saving a capture round trip. Skipped when the action itself failed.");
const DEFAULT_LOOK_FRAMES = 3;
type PostActionObservation = { settle_ms_requested: number; attachment_epoch: number; action_state: string; image_options?: CaptureImageOptions };
/** Act-then-look: append the post-action frame to an action's reply. */
async function look(pid: number, out: ToolReply, ca: boolean | number | undefined, observe?: PostActionObservation): Promise<ToolReply> {
  if (ca === undefined || ca === false || out.isError) return out;
  const session = observe?.attachment_epoch ?? (captureSessionEpoch.get(pid) ?? 0);
  const observation = observe ? { ...observe, settle_ms_elapsed: 0,
    scope: "after successful CLI reply; ongoing jobs/holds may still be active; no application or rendered-frame guarantee" } : undefined;
  const changed = () => (captureSessionEpoch.get(pid) ?? 0) !== session;
  const attachmentChanged = () => {
    let j: Record<string, unknown> = {};
    try { j = JSON.parse((out.content[0] as { text: string }).text); } catch { /* keep original outcome if not JSON */ }
    out.content[0] = { type: "text", text: JSON.stringify({ ...j,
      capture_after_error: "target attachment changed before post-action capture", capture_after_observation: observation }) };
    return out;
  };
  if (observation) {
    if (changed()) return attachmentChanged();
    const start = performance.now();
    if (observation.settle_ms_requested) await new Promise<void>((res) => setTimeout(res, observation.settle_ms_requested));
    observation.settle_ms_elapsed = Math.round(performance.now() - start);
    if (changed()) return attachmentChanged();
  }
  try {
  let g = await grabFrame(pid, { ...observe?.image_options, after_frames: ca === true ? DEFAULT_LOOK_FRAMES : ca });
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
      g = await grabFrame(pid, { ...observe?.image_options, after_frames: 0, method: "printwindow" });
    }
  }
  let j: Record<string, unknown> = {};
  try { j = JSON.parse((out.content[0] as { text: string }).text); } catch { /* keep empty */ }
  if (g.summary && !g.out.isError) {
    out.content[0] = { type: "text", text: JSON.stringify({ ...j, capture_after: { ...g.summary, ...(fallback ? { fallback } : {}), ...(observation ? { observation } : {}) } }) };
    for (const c of g.out.content) if (c.type === "image") out.content.push(c);
  } else {
    let e: unknown = "capture failed";
    try { e = (JSON.parse((g.out.content[0] as { text: string }).text) as { error?: string }).error ?? e; } catch { /* default */ }
    out.content[0] = { type: "text", text: JSON.stringify({ ...j, capture_after_error: e, ...(fallback ? { capture_after_fallback: fallback } : {}), ...(observation ? { capture_after_observation: observation } : {}) }) };
  }
  return out;
  } catch (e) {
    if (!observe) throw e; // Legacy action callers without observation options keep their exception behavior.
    let j: Record<string, unknown> = {};
    try { j = JSON.parse((out.content[0] as { text: string }).text); } catch { /* action replies normally contain JSON */ }
    const error = e instanceof Error ? e.message : String(e);
    out.content[0] = { type: "text", text: JSON.stringify({ ...j,
      capture_after_error: `post-action capture failed: ${error}`, capture_after_observation: observation }) };
    return out; // The action already ran; never repeat it because its observation failed.
  }
}

const actionCaptureArgs = {
  ...captureImageArgs,
  max_width: captureImageArgs.max_width.describe("Action image width cap (default 960 unless scale is explicit); upscale:true explicitly enlarges PNG crops. Same image-to-client mapping as capture."),
  scale: captureImageArgs.scale.describe("Action image factor: PNG 0.05..64 within output bounds, JPEG 0.05..1; give only one of scale and max_width."),
  format: captureImageArgs.format.describe("png (default, exact pixels) or jpeg (lossy observation, requires native capture output support). quality applies only to jpeg."),
  region: captureImageArgs.region.describe("Pre-scale client-pixel rectangle for the action observation, not coordinates in a previous image."),
  capture_after: captureAfterSchema,
  capture_settle_ms: z.number().int().min(0).max(5000).optional().describe(
    "Extra timer delay after a successful CLI reply, before capture_after (default 0). Requires capture_after. Async jobs may still run and hold:true remains held; this does not wait for their completion."),
};
function actionCaptureError(a: CaptureImageOptions & { capture_after?: boolean | number; capture_settle_ms?: number }): string | undefined {
  const destination=destinationError(a);if(destination)return destination;
  const disabled = a.capture_after === undefined || a.capture_after === false;
  if (a.capture_settle_ms !== undefined && disabled) return "capture_settle_ms requires capture_after";
  if (disabled && [a.region, a.max_width, a.scale, a.format, a.quality,a.upscale,a.save_path,a.out_dir].some((v) => v !== undefined))
    return "action image options require enabled capture_after; pass capture_after:0 for an immediate image or capture_after:true to request three presented frames";
  if (a.scale !== undefined && a.max_width !== undefined) return "give only one of scale, max_width";
  if (a.format === "jpeg" && a.scale !== undefined && a.scale > 1) return "JPEG action images support scale <= 1; use PNG for MCP enlargement";
  return validateFormat(a);
}
const actionObservation = (a: CaptureImageOptions & { pid: number; capture_settle_ms?: number; async?: boolean; hold?: boolean }): PostActionObservation => ({
  settle_ms_requested: a.capture_settle_ms ?? 0, attachment_epoch: captureSessionEpoch.get(a.pid) ?? 0,
  action_state: a.async ? "async_job_may_be_running" : a.hold ? "held_until_changed" : "action_reply",
  image_options: { region: a.region, max_width: a.max_width ?? (a.scale === undefined ? 960 : undefined),
    scale: a.scale, format: a.format, quality: a.quality,upscale:a.upscale,save_path:a.save_path,out_dir:a.out_dir },
});


tool("ocr", "Read text on screen",
  "OCR the current frame with the built-in Windows OCR engine. Returns text and lines[] with boxes [x,y,w,h] in client " +
  "pixels (words[] too when words:true). Empty results automatically try bounded upscale/contrast passes on the SAME captured image. " +
  "Nonempty OCR is not retried and may still be wrong. text_only omits geometry and verbose metadata while preserving capture/timing provenance.",
  {
    pid, region: regionSchema.optional(),
    scale: z.number().min(1).max(4).optional().describe("Upscale before OCR (2 helps small text)."),
    lang: z.string().optional().describe("OCR language tag, e.g. en-US (default: the user's profile language)."),
    words: z.boolean().optional().describe("Also return every word with its box (bigger reply)."),
    tiles: z.boolean().optional().describe("Tiled second pass that finds words the full-frame pass drops (default true; false is faster)."),
    preprocess: z.enum(['none','gray','contrast','threshold','invert','auto']).optional().describe('Initial preprocessing. An explicit mode is retained during retries; auto starts with contrast.'),
    retry_empty: z.boolean().optional().describe('Retry successful empty OCR with bounded upscale/contrast passes on the same capture (default true). Explicit scale is respected; false keeps one requested-scale pass.'),
    text_only: z.boolean().optional().describe('Compact text/count plus selected pass and capture/timing provenance; omit line/word boxes and confidence prose.'),
  },
  async (a) => reply(presentOcr(await runVinput([...at(a.pid), "ocr", ...region(a.region), ...opt("--scale", a.scale), ...opt("--lang", a.lang),...opt('--preprocess',a.preprocess),
    ...(a.words ? [] : ["--lines-only"]), ...(a.tiles === false ? [] : ["--tiles"]),`--retry-empty=${a.retry_empty!==false}`,...(a.text_only?['--text-only']:[])], 60000),!!a.text_only,a.retry_empty!==false)), true);

const textMatch = {
  text: z.string().min(1).describe("Text to find. Default matching: case-insensitive, whitespace ignored, substring of a line."),
  nth: z.number().int().min(1).optional().describe("Pick the Nth match in reading order (1-based). Without it, several matches = refusal listing the candidates."),
  exact: z.boolean().optional().describe("Case-sensitive, whole-word match."),
  exact_line: z.boolean().optional().describe('Require the query to cover an entire OCR line under the selected case/whitespace/fuzzy rules. Prevents clicking an ordinary word inside a paragraph. Strict line searches keep the requested capture context and do not accept clipped OCR tiles.'),
  fuzzy: z.number().int().min(0).max(3).optional().describe("Also accept whole words within N edits (OCR slips)."),
  region: regionSchema.optional(),
};
const textArgs = (a: { text: string; nth?: number; exact?: boolean; exact_line?: boolean; whole_word?: boolean; fuzzy?: number; region?: z.infer<typeof regionSchema> }) =>
  [a.text, ...opt("--nth", a.nth), ...opt("--exact", a.exact),
    ...(a.whole_word === undefined ? [] : [`--whole-word=${a.whole_word}`]), ...(a.exact_line===undefined?[]:[`--exact-line=${a.exact_line}`]), ...opt("--fuzzy", a.fuzzy), ...region(a.region)];

tool("find_text", "Find text on screen",
  "Locate text without clicking. Returns the matched box, centre, match_count and match_score, or ok:false (with what OCR " +
  "did read) when it is not there.",
  { pid, ...textMatch, timeout_ms: timeoutMs(0) },
  async (a) => {const unsupported=await requireTextBoundary(false,false,a.exact_line);if(unsupported)return reply(unsupported);return reply(await runVinput([...at(a.pid), "click-text", ...textArgs(a), "--dry-run", "--timeout", String(a.timeout_ms ?? 0)], (a.timeout_ms ?? 0) + 60000));}, true);

tool("click_text", "Click text on screen",
  "Find text with OCR and click the centre of its box (keeps looking for timeout_ms). Refuses and clicks nothing when " +
  "the text matches more than once and nth is not given. Defaults to complete OCR lines, so Next in a tutorial paragraph is not a button. exact_line:false permits text within a longer line. Explicit exact:true or whole_word:false retain their legacy matching unless exact_line:true is requested. Verify the result with wait_text / capture.",
  { pid, ...textMatch, text: textMatch.text.describe("Text to click; defaults to a complete case-insensitive OCR line, whitespace ignored. exact_line:false permits a word within longer text. exact:true also requires case; fuzzy applies to the complete eligible label."),
    whole_word: z.boolean().default(true).describe("Require complete OCR-word boundaries independently of case (default true). false explicitly restores legacy substring matching. exact:true always requires word boundaries. Default mode preflights the CLI capability before input."),
    timeout_ms: timeoutMs(5000), button: button.optional(),
    optional: z.boolean().optional().describe("Succeed with skipped:true if the text never appears (for dialogs that may not show)."),
    ...actionCaptureArgs, region: captureImageArgs.region.describe("Client-pixel OCR search region; also bounds an enabled post-action image.") },
  async (a) => {
    const captureError = actionCaptureError({ ...a, region: a.capture_after === undefined || a.capture_after === false ? undefined : a.region });
    if (captureError) return fail(captureError);
    const observation = actionObservation(a);
    const exactLine=a.exact_line??(!a.exact&&a.whole_word!==false);
    const unsupported = await requireTextBoundary(a.whole_word, a.exact,exactLine);
    if (unsupported) return reply(unsupported);
    if ((captureSessionEpoch.get(a.pid) ?? 0) !== observation.attachment_epoch)
      return fail("attachment changed during text capability preflight; no click was sent");
    const r = await runVinput([...at(a.pid), "click-text", ...textArgs({...a,exact_line:exactLine}), ...opt("--timeout", a.timeout_ms), ...opt("--button", a.button),
      ...opt("--optional", a.optional)], (a.timeout_ms ?? 5000) + 60000);
    return look(a.pid, reply(r), a.capture_after, observation);
  });

tool("wait_text", "Wait for text",
  "Wait until visible OCR text matches one string or ANY of a list, e.g. text:['VICTORY','DEFEAT'], in an optional region. All alternatives share each native capture/OCR poll; matched_text identifies the matching query. gone:true waits until ALL listed alternatives are absent. Times out with timed_out:true and ocr_saw. Use observe_settle separately to wait for selected pixels to become stable.",
  { pid, text: z.union([textMatch.text,z.array(z.string().min(1).max(256)).min(1).max(16)]).describe('One text string or any-of alternatives (1..16); list entries cannot contain |.'), region: regionSchema.optional(),context_region:regionSchema.optional().describe('Also OCR this client-pixel rectangle (e.g. level title) from the SAME full captured frame that matched. Native receipt includes capture/frame QPC and Present index; old clients or OCR failure produce an explicit context warning.'),gone: z.boolean().optional().describe("Wait until every requested text is NOT on screen."),
    exact_line:textMatch.exact_line,
    timeout_ms: timeoutMs(15000), optional: z.boolean().optional().describe("Succeed with skipped:true on timeout.") },
  async (a) => {const query=waitTextQuery(a.text);if(query.error)return fail(query.error);const unsupported=await requireTextBoundary(false,false,a.exact_line);if(unsupported)return reply(unsupported);const r=presentWaitTextAny(await runVinput([...at(a.pid), "wait-text", ...query.argv, ...region(a.region),...(a.context_region?['--context-region',`${a.context_region.x},${a.context_region.y},${a.context_region.w},${a.context_region.h}`]:[]), ...opt("--gone", a.gone),...(a.exact_line===undefined?[]:[`--exact-line=${a.exact_line}`]),
    ...opt("--timeout", a.timeout_ms), ...opt("--optional", a.optional)], (a.timeout_ms ?? 15000) + 60000),query.queries);const j=r.json as Record<string,unknown>,context=j?.context as Record<string,unknown>|undefined;
    return reply(r,a.context_region?{wait_text_context_available:context?.ok===true&&context.same_capture===true&&typeof j.capture_qpc_ticks==='string'&&context.capture_qpc_ticks===j.capture_qpc_ticks,...(context?.ok===true&&context.same_capture===true&&typeof j.capture_qpc_ticks==='string'&&context.capture_qpc_ticks===j.capture_qpc_ticks?{}:{context_warning:'Outcome observation retained; same-frame context OCR is unavailable or unconfirmed. Do not infer a level/title from a later capture.'})}:undefined);}, true);

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
  "with capture output support in the game. Oversized PNG sheets get a smaller attachment-only preview when possible; " +
  "the saved original and native tile rectangles/timestamps stay unchanged.",
  {
    pid,
    path: z.string().optional().describe("Where to save the sheet (default: a new file in the captures folder)."),
    save_path:captureImageArgs.save_path,out_dir:captureImageArgs.out_dir,
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
    const destination=destinationError(a);if(destination)return fail(destination);
    if (a.max_width !== undefined && a.scale !== undefined) return fail("give only one of max_width, scale");
    if (a.quality !== undefined && a.format !== "jpeg") return fail("quality applies only to format jpeg");
    if (a.columns !== undefined && a.columns > (a.frames ?? 8)) return fail("columns cannot exceed frames");
    const jpeg = a.format === "jpeg";
    const path = captureDestination(a,a.pid,jpeg?'jpg':'png',()=>defaultCapturePath(a.pid,jpeg?'jpg':'png','_burst'));
    const r = await runVinput([...at(a.pid), "capture-burst", path, ...opt("--frames", a.frames), ...opt("--interval-ms", a.interval_ms),
      ...opt("--columns", a.columns), ...opt("--max-width", a.max_width), ...opt("--scale", a.scale), ...opt("--format", a.format),
      ...opt("--quality", a.quality), ...region(a.region)], 120000);
    let out = reply(r);
    if (r.ok && a.include_image !== false) {
      try {
        const img = readFileSync((r.json as { path?: string }).path || path);
        const preview = burstImage(img, jpeg, MAX_IMAGE_BYTES);
        out = reply(r, { attachment: preview.metadata });
        if (preview.bytes) out.content.push({ type: "image", data: preview.bytes.toString("base64"), mimeType: jpeg ? "image/jpeg" : "image/png" });
        else out.content.push({ type: "text", text: `image not attached: ${preview.metadata.reason}` });
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
const mappingSchema = z.object({ scale: z.number().positive(),scale_y:z.number().positive().optional(), offset_x: z.number(), offset_y: z.number() })
  .describe("Image-to-client transform as returned by capture. An explicit transform gets a fresh observation-only window scope; click/move require a matching native pointer-scope enforcement build.");
const coordArgs = {
  coord_space: z.enum(["client", "capture"]).optional().describe(
    "client (default): x / y are game client pixels. capture: x / y are pixels of the image you were shown by capture (after " +
    "max_width / scale / zoom / region), converted with the last capture's mapping, or with 'mapping' when given."),
  mapping: mappingSchema.optional(),
};
type CoordIn = { coord_space?: "client" | "capture"; mapping?: Mapping; norm?: boolean; capture_space?: boolean };
/** Convert image-space points to client points when coord_space is "capture"; an error string when that is impossible. */
async function mapPoints(pid: number, a: CoordIn, pts: [number, number][],expectedScope?:unknown) {
  const epoch=captureSessionEpoch.get(pid)??0;const mapped=await lastMapping.resolve(pid,a,pts,epoch,runVinput,at(pid),expectedScope);
  return (captureSessionEpoch.get(pid)??0)===epoch?mapped:{error:'attachment changed while resolving capture coordinates; no input was sent'};
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
const mouseModifiers={modifiers:z.array(z.enum(['ctrl','shift','alt'])).max(3).optional().describe('Optional temporary ctrl/shift/alt keys. A bounded native job owns new modifiers; exact-ID cancellation preserves newer/manual key holds. Pre-held virtual aliases stay held.'),modifier_hold_ms:z.number().int().min(100).max(118000).optional().describe('Only with modifiers: native hold deadline, default 60000ms. Releases even if MCP exits; expiry or supersession is reported as unconfirmed coverage.')};
tool("click", "Click",
  "Click a mouse button at a position (or at the current virtual cursor when x/y are omitted). The cursor hovers there " +
  "first (hover_ms) and the button is held for hold_ms, both long enough for games that poll input once a frame. Old menus: hold_ms 300. " +
  "Placement / drop-in-world clicks: hold_ms ~900. ok:true = the click was delivered; verify with capture / wait_text.",
  { pid, x: xy.x.optional(), y: xy.y.optional(), norm: xy.norm, capture_space: xy.capture_space, button: button.optional(),
    count: z.number().int().min(1).max(10).optional().describe("2 = double click."),
    verify_change:z.object({region:regionSchema,timeout_ms:z.number().int().min(50).max(5000).optional(),poll_ms:z.number().int().min(20).max(1000).optional()}).optional().describe('Observe a scoped region before and after this one click. No pixel change returns an explicit observation failure retaining the delivery receipt; change may be animation and does not prove game acceptance. Never retries.'),
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
    ...coordArgs, ...edgeArgs, ...parkArg, ...actionCaptureArgs, ...mouseModifiers },
  async (a) => {
    if(a.modifier_hold_ms!==undefined&&!a.modifiers?.length)return fail('modifier_hold_ms requires nonempty modifiers; no input was sent');
    const captureError = actionCaptureError(a);
    if (captureError) return fail(captureError);
    const observation = actionObservation(a);
    if ((a.x === undefined) !== (a.y === undefined)) return fail("give both x and y, or neither");
    if(a.verify_change&&a.x===undefined)return fail('verify_change requires a positioned click with x and y; no input was sent');
    let pos: string[] = a.x === undefined ? [] : [String(a.x), String(a.y)];
    let mappedInfo: Record<string, unknown> | undefined, pointerScope:string|undefined;
    if (a.x !== undefined) {
      const m = await mapPoints(a.pid, a, [[a.x, a.y as number]]);
      if ("error" in m) return fail(m.error);
      pos = [String(m.pts[0][0]), String(m.pts[0][1])];
      pointerScope=m.scope;
      if (m.mapped) mappedInfo = { mapped: { x: m.pts[0][0], y: m.pts[0][1] },mapping_used:m.mapping_used };
    }
    const warnings = a.x === undefined ? [] : await edgeWarnings(a.pid, [{ label: "click target", x: Number(pos[0]), y: Number(pos[1]) }], a.edge_margin, a.norm);
    const verificationEpoch=captureSessionEpoch.get(a.pid)??0;
    const capability=a.verify_change?await clickScopeCapability(a.pid,runVinput,at(a.pid)):undefined;
    if(capability&&!capability.ok)return reply(capability);
    if(a.verify_change&&(captureSessionEpoch.get(a.pid)??0)!==verificationEpoch)return fail('attachment changed during click preflight; no click or temporary modifier was sent');
    return withMouseModifiers(a.pid,a.modifiers,async()=>{
    const frameworkTiming=frameworkClickTiming(a.hold_ms,a.hover_ms,a.x!==undefined);
    const sendClick=(scope?:string)=>runVinput([...at(a.pid), "click", a.button ?? "left", ...pos, ...opt("--norm", a.norm), ...opt("--capture-space", a.capture_space),...opt('--scope-json',scope??pointerScope),
      ...(a.x === undefined ? [] : ["--settle-ms", String(frameworkTiming.hover)]),
      ...opt("--count", a.count), "--ms", String(frameworkTiming.hold),...frameworkTiming.argv,
      ...(a.di_poll_timeout_ms !== undefined ? ["--di-poll-timeout-ms", String(a.di_poll_timeout_ms)] : []),
      ...(a.di_release_timeout_ms !== undefined ? ["--di-release-timeout-ms", String(a.di_release_timeout_ms)] : [])],
      60000 + (a.hold_ms ?? 0) * (a.count ?? 1) + (a.hover_ms ?? 0) + ((a.di_poll_timeout_ms ?? 1000) + (a.di_release_timeout_ms ?? 1000)) * (a.count ?? 1));
    const cr=a.verify_change?await verifiedClick(a.pid,a.verify_change,runVinput,at(a.pid),()=>(captureSessionEpoch.get(a.pid)??0)===verificationEpoch,sendClick,pointerScope,capability):await sendClick();
    const parked = a.park_cursor && cr.ok ? await parkCursor(a.pid) : undefined;
    const evidence = inputEvidence(cr.json);
    return look(a.pid, reply(cr, { ...mappedInfo, ...(evidence ? { evidence } : {}), ...(warnings.length ? { warnings } : {}), ...(parked ? { parked } : {}) }), a.capture_after, observation);
    },runVinput,at,a.modifier_hold_ms);
  });

tool("move", "Move the cursor",
  "Move the game's virtual cursor to an absolute position (hover). The real cursor is not touched.",
  { pid, ...xy, steps: z.number().int().min(1).max(200).optional().describe("Intermediate moves (default 1)."), ...coordArgs, ...edgeArgs, ...actionCaptureArgs },
  async (a) => {
    const captureError = actionCaptureError(a);
    if (captureError) return fail(captureError);
    const observation = actionObservation(a);
    const m = await mapPoints(a.pid, a, [[a.x, a.y]]);
    if ("error" in m) return fail(m.error);
    const warnings = await edgeWarnings(a.pid, [{ label: "move target", x: m.pts[0][0], y: m.pts[0][1] }], a.edge_margin, a.norm);
    const mr = await runVinput([...at(a.pid), "mouse", String(m.pts[0][0]), String(m.pts[0][1]), ...opt("--norm", a.norm),
      ...opt("--capture-space", a.capture_space), ...opt("--steps", a.steps),...opt('--scope-json',m.scope)], 60000);
    const evidence = inputEvidence(mr.json);
    return look(a.pid, reply(mr, { ...(m.mapped ? { mapped: { x: m.pts[0][0], y: m.pts[0][1] },mapping_used:m.mapping_used } : {}), ...(evidence ? { evidence } : {}), ...(warnings.length ? { warnings } : {}) }), a.capture_after, observation);
  });

tool("move_relative", "Relative mouse motion",
  "Relative mouse motion (raw input / DirectInput deltas) for mouse-look / camera control in 3D games.",
  { pid, dx: z.number().int(), dy: z.number().int(), steps: z.number().int().min(1).max(200).optional(),
    raw: z.boolean().optional().describe("Do not clamp the virtual cursor to the window (games that re-centre the cursor)."), ...actionCaptureArgs },
  async (a) => {
    const captureError = actionCaptureError(a);
    if (captureError) return fail(captureError);
    const observation = actionObservation(a);
    const r = await runVinput([...at(a.pid), "move", String(a.dx), String(a.dy), ...opt("--steps", a.steps), ...opt("--raw", a.raw)], 60000);
    return look(a.pid, reply(r), a.capture_after, observation);
  });

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
    ...coordArgs, ...edgeArgs, ...parkArg, ...actionCaptureArgs, ...mouseModifiers },
  async (a) => {
    const captureError=actionCaptureError(a);if(captureError)return fail(captureError);const observation=actionObservation(a);
    if(a.modifier_hold_ms!==undefined&&!a.modifiers?.length)return fail('modifier_hold_ms requires nonempty modifiers; no input was sent');
    const m = await mapPoints(a.pid, a, [[a.x1, a.y1], [a.x2, a.y2]]);
    if ("error" in m) return fail(m.error);
    const [[x1, y1], [x2, y2]] = m.pts;
    const warnings = await edgeWarnings(a.pid, [{ label: "drag start", x: x1, y: y1 }, { label: "drag destination", x: x2, y: y2 }], a.edge_margin);
    const btn = a.button ?? "left", ms = a.ms ?? 300;
    const steps = a.steps ?? Math.min(60, Math.max(3, Math.round(ms / 20)));
    const holdStart = a.hold_start_ms ?? DEFAULT_DRAG_HOLD_START_MS, holdEnd = a.hold_end_ms ?? DEFAULT_DRAG_HOLD_END_MS;
    const where = { ...(m.mapped ? { mapped: { x1, y1, x2, y2 },mapping_used:m.mapping_used } : {}), ...(warnings.length ? { warnings } : {}) };
    return withMouseModifiers(a.pid,a.modifiers,async()=>{
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
          ...(evidence ? { evidence } : {}), ...where, ...(parked ? { parked } : {}) }), a.capture_after,observation);
      }
      placementFallback = notSeen
        ? `${notSeen}: this game does not read mouse buttons through DirectInput (Unity-style window messages), so the placement drag could not confirm the press; used the plain press / move / release drag instead`
        : `${(pr.json as { error?: string }).error ?? "placement drag unsupported"}: used press / move / release instead`;
    }
    const down = await runVinput([...at(a.pid), "mousedown", btn, String(x1), String(y1)], 60000);
    if (!down.ok) return reply({ ...down, ok: false, json: { ok: false, error: "drag: mouse down failed", down: down.json } },where);
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
      { ...(placementFallback ? { placement_fallback: placementFallback } : {}), ...(Object.keys(evidence).length ? { evidence } : {}), ...where, ...(parked ? { parked } : {}) }), a.capture_after,observation);
    },runVinput,at,a.modifier_hold_ms);
  });

tool("scroll", "Scroll the mouse wheel",
  "Scroll N wheel notches (positive = up / away, negative = down) at the virtual cursor, after moving it to x,y if given.",
  { pid, notches: z.number().int().min(-100).max(100).optional().describe('Wheel notches: positive up, negative down. Give exactly one of notches/delta/amount/clicks.'),delta:z.number().int().min(-100).max(100).optional(),amount:z.number().int().min(-100).max(100).optional(),clicks:z.number().int().min(-100).max(100).optional(),x: z.number().optional(), y: z.number().optional(),
    horizontal: z.boolean().optional(), ...edgeArgs, ...actionCaptureArgs, ...mouseModifiers },
  async (a) => {
    const wheel=[a.notches,a.delta,a.amount,a.clicks].filter((v):v is number=>v!==undefined);if(wheel.length!==1)return fail('scroll expects {pid, notches: N} (positive up, negative down), or exactly one alias delta/amount/clicks; no input was sent');
    const captureError=actionCaptureError(a);if(captureError)return fail(captureError);const observation=actionObservation(a);
    if(a.modifier_hold_ms!==undefined&&!a.modifiers?.length)return fail('modifier_hold_ms requires nonempty modifiers; no input was sent');
    if ((a.x === undefined) !== (a.y === undefined)) return fail("give both x and y, or neither");
    const pos = a.x === undefined ? [] : [String(a.x), String(a.y)];
    const warnings = a.x === undefined ? [] : await edgeWarnings(a.pid, [{ label: "scroll position", x: a.x, y: a.y as number }], a.edge_margin);
    return withMouseModifiers(a.pid,a.modifiers,async()=>look(a.pid,reply(await runVinput([...at(a.pid), "scroll", String(wheel[0]), ...pos, ...opt("--horizontal", a.horizontal)], 60000), warnings.length ? { warnings } : undefined),a.capture_after,observation),runVinput,at,a.modifier_hold_ms);
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
  if (r.ok && isCompletedJobOutcome(r.json)) return undefined;
  const id = (r.json as { job_id?: unknown })?.job_id;
  return r.ok && typeof id === "string" ? { note: `running in the background as job ${id}: job_status to follow it, job_cancel to stop it` } : undefined;
}
tool("key", "Press a key",
  "Press and release a key or chord in the game (delivered on every channel the game may read: messages, raw input, " +
  "DirectInput, GetAsyncKeyState). hold_ms keeps it down longer (e.g. walking).",
  { pid, key: keyCombo, hold_ms: z.number().int().min(1).max(60000).optional().describe("How long the key stays down (default ~80)."),
    measure_region:regionSchema.optional().describe('Optional client-pixel RGB region difference before/after this completed timed key. Disallows async. Reports pixel change and per-key reader receipts, without inferring avatar movement or distance.'),
    count: z.number().int().min(1).max(100).optional(), gap_ms: z.number().int().min(0).max(10000).optional().describe("Pause between repeats."),
    async: asyncArg, replace: replaceArg, ...actionCaptureArgs },
  async (a) => {
    const captureError = actionCaptureError(a);
    if (captureError) return fail(captureError);
    const observation = actionObservation(a);
    if (a.async && !a.hold_ms) return fail("async needs hold_ms (a bounded hold)");
    if (a.async && (a.count ?? 1) > 1) return fail("async holds one press; count is not supported with async");
    if(a.async&&a.measure_region)return fail('measure_region requires a completed key; omit async or use hold_until_change');
    const inputBudget=a.async?60000:(a.hold_ms??0)*(a.count??1)+(a.gap_ms??0)*(a.count??1)+60000;
    const r = a.measure_region?await measureTimedInput(at(a.pid),a.measure_region,()=>captureSessionEpoch.get(a.pid)??0,scope=>runVinput([...at(a.pid),'json',JSON.stringify({cmd:'key',key:a.key,ms:a.hold_ms??80,count:a.count??1,gap:a.gap_ms??80,expected_capture_scope:scope,...(a.replace?{replace:true}:{})})],inputBudget)):await runVinput([...at(a.pid), "key", a.key, ...(a.hold_ms ? [String(a.hold_ms)] : []), ...opt("--count", a.count), ...opt("--gap", a.gap_ms),
      ...opt("--async", a.async), ...opt("--replace", a.replace)],inputBudget);
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after, observation);
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
    let diReader:boolean|null=null;
    if(!a.as_keys){
      const status=await runVinput([...at(a.pid),'status'],1500).catch(()=>undefined);
      const counters=(status?.json as {counters?:{di_kbd?:unknown;di_data_kbd?:unknown}}|undefined)?.counters;
      if(status?.ok&&typeof counters?.di_kbd==='number'&&typeof counters.di_data_kbd==='number'&&Number.isSafeInteger(counters.di_kbd)&&Number.isSafeInteger(counters.di_data_kbd)&&counters.di_kbd>=0&&counters.di_data_kbd>=0)diReader=counters.di_kbd>0||counters.di_data_kbd>0;
    }
    const r=await runVinput([...at(a.pid), "json", JSON.stringify(cmd)], (a.delay_ms ?? 30) * a.text.length + 60000);
    return reply(r,!a.as_keys?{text_input_observation:{backend:'WM_CHAR',directinput_keyboard_reader_observed:diReader,application_response:null,as_keys_hint:diReader?'This process has polled a DirectInput keyboard. If the field stays empty, use as_keys:true on your next deliberate text entry. WM_CHAR admission does not prove the field accepted text; no automatic fallback or replay was sent.':'WM_CHAR admission does not prove text appeared. If the field stays empty, as_keys:true is available for key-driven fields; no automatic fallback or replay was sent.'}}:undefined);
  });

const padCaptureArgs = actionCaptureArgs;
const padCaptureError = actionCaptureError;
const padObservation = actionObservation;

tool("pad", "Gamepad (XInput)",
  "Drive virtual XInput pad 0. buttons: '+'-joined names from a b x y lb rb start back up down left right ls rs guide, " +
  "or 'none'. Sticks -32768..32767, triggers 0..255. Held for ms (default 100), or until changed with hold:true. off:true " +
  "stops overriding the pad.",
  { pid, buttons: z.string().optional().describe("e.g. 'a', 'a+b', 'down', 'none'."),
    ms: z.number().int().min(1).max(60000).optional(),
    lx: z.number().int().min(-32768).max(32767).optional().describe("Left stick X: signed integer -32768..32767; 0 is neutral."), ly: z.number().int().min(-32768).max(32767).optional().describe("Left stick Y: signed integer -32768..32767; 0 is neutral."),
    rx: z.number().int().min(-32768).max(32767).optional().describe("Right stick X: signed integer -32768..32767; 0 is neutral."), ry: z.number().int().min(-32768).max(32767).optional().describe("Right stick Y: signed integer -32768..32767; 0 is neutral."),
    lt: z.number().int().min(0).max(255).optional().describe("Left trigger: integer 0..255, from released (0) to fully pressed (255); not a 16-bit stick value."), rt: z.number().int().min(0).max(255).optional().describe("Right trigger: integer 0..255, from released (0) to fully pressed (255); not a 16-bit stick value."),
    hold: z.boolean().optional(), off: z.boolean().optional(), async: asyncArg, replace: replaceArg, ...padCaptureArgs },
  async (a) => {
    const captureError = padCaptureError(a);
    if (captureError) return fail(captureError);
    const observation = padObservation(a);
    if (a.off) return look(a.pid, reply(await runVinput([...at(a.pid), "pad", "off"], 30000)), a.capture_after, { ...observation, action_state: "pad_override_off_reply" });
    if (a.async && (a.hold || !a.ms)) return fail("async needs ms (a bounded duration) and cannot be combined with hold");
    const r = await runVinput([...at(a.pid), "pad", a.buttons ?? "none", ...(a.ms ? [String(a.ms)] : []),
      ...opt("--lx", a.lx), ...opt("--ly", a.ly), ...opt("--rx", a.rx), ...opt("--ry", a.ry), ...opt("--lt", a.lt), ...opt("--rt", a.rt),
      ...opt("--hold", a.hold), ...opt("--async", a.async), ...opt("--replace", a.replace)], a.async ? 60000 : (a.ms ?? 0) + 60000);
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after, observation);
  });

const axis = z.enum(["lx", "ly", "rx", "ry", "lt", "rt"]);
const stickV = z.number().int().min(-32768).max(32767).describe("Stick axis: signed integer -32768..32767; 0 is neutral."), trigV = z.number().int().min(0).max(255).describe("Trigger: integer 0..255, from released (0) to fully pressed (255); not a 16-bit stick value.");
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
  { pid, axis, from: z.number().int().min(-32768).max(32767).describe("Starting value: lt/rt triggers use 0..255; lx/ly/rx/ry sticks use -32768..32767."), to: z.number().int().min(-32768).max(32767).describe("Ending value: lt/rt triggers use 0..255; lx/ly/rx/ry sticks use -32768..32767."),
    ms: z.number().int().min(1).max(118000).describe("Ramp duration."),
    ...padConst, buttons: z.string().optional().describe("Buttons held during the ramp, e.g. 'a' or 'a+rb'."),
    end_hold_ms: z.number().int().min(0).max(10000).optional().describe("Hold the final value this long before releasing (default 0)."),
    pacing: pacingArg, async: asyncArg, replace: replaceArg, ...padCaptureArgs },
  async (a) => {
    const captureError = padCaptureError(a);
    if (captureError) return fail(captureError);
    const observation = padObservation(a);
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
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after, observation);
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
    pacing: pacingArg, strict_frames: z.boolean().optional(), async: asyncArg, replace: replaceArg, ...padCaptureArgs },
  async (a) => {
    const captureError = padCaptureError(a);
    if (captureError) return fail(captureError);
    const observation = padObservation(a);
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
    return look(a.pid, reply(r, a.async ? jobExtra(r) : undefined), a.capture_after, observation);
  });


tool("pad_move", "Move with the left stick",
  "Move for ms using a named stick direction and walk 20000/run 32767 magnitude. Up=+LY and right=+LX; diagonals are normalized. " +
  "Use screen_rotation_deg to compensate a calibrated camera mapping (+90 maps up to stick-right); camera changes require rechecking. " +
  "These are screen-intent labels, not verified screen motion. Verify and calibrate with captures. " +
  "Other pad controls are neutral; the finite action releases to neutral. smooth:true uses one time-paced ease-in/out timeline (ms >= 80); " +
  "default false keeps a rectangular move. Native skipped-state/job receipts remain authoritative; smoothing cannot overcome a game's deadzone.",
  { pid, direction: z.enum(MOVE_DIRECTIONS), ms: z.number().int().min(1).max(60000),
    speed: z.enum(["walk", "run"]).optional().describe("walk=20000 (default); run=32767. These are stick magnitudes, not measured character speeds."),
    screen_rotation_deg: z.number().min(-360).max(360).optional().describe("Clockwise rotation from intended screen direction to stick axes (default 0). +90 maps up to stick-right; 180 compensates inversion. Calibrate per camera view; no auto-detection."),
    smooth: z.boolean().optional(), async: asyncArg, replace: replaceArg, ...padCaptureArgs,
    capture_after: captureAfterSchema.default(0).describe("Default 0: return an image after the action reply and optional settle, without a Present wait. false disables it; true/numeric use the usual capture_after semantics."),
    capture_settle_ms: padCaptureArgs.capture_settle_ms.describe("Extra nonblocking delay after the CLI reply before capture (default 100 ms for pad_move). Requires an enabled capture_after; async capture does not prove completion."), },
  async (a) => {
    const captureError = padCaptureError(a);
    if (captureError) return fail(captureError);
    if (a.smooth && a.ms < 80) return fail("smooth moves need ms >= 80; use an unsmoothed short pulse or a longer move");
    const observation = padObservation({ ...a, capture_settle_ms: a.capture_settle_ms ?? 100 }), speed = a.speed ?? "walk", vector = moveVector(a.direction, speed, a.screen_rotation_deg ?? 0);
    let argv: string[], curve: Record<string, unknown>;
    if (a.smooth) {
      const { states, rampMs } = smoothMoveStates(vector, a.ms);
      const cmd: Record<string, unknown> = { cmd: "pad-timeline", states_json: JSON.stringify(states), ms: a.ms, pacing: "time" };
      if (a.async) cmd.async = true;
      if (a.replace) cmd.replace = true;
      argv = [...at(a.pid), "json", JSON.stringify(cmd)];
      curve = { kind: "ease_in_out_samples", ramp_ms: rampMs, requested_states: states.length, pacing: "time" };
    } else {
      argv = [...at(a.pid), "pad", "none", String(a.ms), "--lx", String(vector.lx), "--ly", String(vector.ly),
        "--rx", "0", "--ry", "0", "--lt", "0", "--rt", "0", ...opt("--async", a.async), ...opt("--replace", a.replace)];
      curve = { kind: "rectangular" };
    }
    const r = await runVinput(argv, a.async ? 60000 : a.ms + 60000);
    return look(a.pid, reply(r, { ...(a.async ? jobExtra(r) : {}), pad_move: {
      direction: a.direction, speed, screen_rotation_deg: a.screen_rotation_deg ?? 0, magnitude: vector.magnitude, lx: vector.lx, ly: vector.ly, duration_ms: a.ms, curve,
      screen_motion_verified: false, scope: "requested XInput left-stick convention; game movement depends on camera/control mapping" } }), a.capture_after, observation);
  });

tool("pad_look", "Turn using an angular calibration",
  "Predict a horizontal camera turn from an explicit caller-measured angular calibration. calibration.rx is the signed stick value " +
  "that produced positive/rightward rotation at degrees_per_second; negative degrees uses its opposite and assumes symmetric response. " +
  "Requires real angular measurement, not pixels. Uses one finite right-stick action, all other controls neutral, then optional capture. " +
  "Returns predicted angle with angle_verified:false; no auto calibration or guaranteed game rotation.",
  { pid, degrees: z.number().finite().min(-360).max(360).refine((v) => v !== 0, "degrees must be nonzero"),
    calibration: z.object({
      rx: z.number().int().min(-32768).max(32767).refine((v) => v !== 0, "calibration rx must be nonzero"),
      degrees_per_second: z.number().positive().finite().describe("Measured positive/rightward angular rate at exactly calibration.rx, in degrees per second; never pass pixels here."),
    }).strict(),
    async: asyncArg, replace: replaceArg, ...padCaptureArgs,
    capture_after: captureAfterSchema.default(0).describe("Default 0: return a backend image after the reply/settle without a Present wait. false disables it; true/numeric keep usual semantics."),
    capture_settle_ms: padCaptureArgs.capture_settle_ms.describe("Post-reply image delay, default 100ms for pad_look. Async image is an observation, not job completion."), },
  async (a) => {
    const captureError = padCaptureError(a);
    if (captureError) return fail(captureError);
    const turn = calibratedLook(a.degrees, a.calibration);
    if (turn.error !== undefined) return fail(turn.error);
    const observation = padObservation({ ...a, capture_settle_ms: a.capture_settle_ms ?? 100 });
    const r = await runVinput([...at(a.pid), "pad", "none", String(turn.ms), "--lx", "0", "--ly", "0", "--rx", String(turn.rx),
      "--ry", "0", "--lt", "0", "--rt", "0", ...opt("--async", a.async), ...opt("--replace", a.replace)], a.async ? 60000 : turn.ms + 60000);
    return look(a.pid, reply(r, { ...(a.async ? jobExtra(r) : {}), pad_look: {
      requested_degrees: a.degrees, predicted_degrees: turn.predicted_degrees, angle_verified: false,
      calibration: a.calibration, applied_rx: turn.rx, duration_ms: turn.ms, unrounded_duration_ms: turn.raw_ms,
      opposite_rate_assumed: a.degrees < 0, scope: "caller angular rate times quantized duration; calibration and game response are unverified" } }), a.capture_after, observation);
  });

const drivePhase = z.object({ ms: z.number().int().min(1).max(60000),
  throttle: trigV.optional().describe("RT throttle0..255; omitted=0."),
  brake: trigV.optional().describe("LT0..255; often brake/reverse, game-specific. Omitted=0."),
  steer: stickV.optional().describe("LX steering -32768..32767; omitted=0. Calibrate direction/strength in the game."),
}).strict();
tool("pad_drive", "Drive through bounded control phases",
  "One time-paced owned XInput timeline for throttle(RT), steering(LX) and brake/reverse(LT). Give ms plus controls, OR phases " +
  "with per-phase ms/controls, up to16 phases and60000ms total including any brake_after tail. Every phase is a complete snapshot, " +
  "all other controls neutral. No process-start gaps between phases; native completion/cancel clears the job. This is requested input, " +
  "not autopilot, collision detection, measured heading/speed or proof of stopping. Inspect a fresh image and use short calibrated legs.",
  { pid, ms: drivePhase.shape.ms.optional(), throttle: drivePhase.shape.throttle, steer: drivePhase.shape.steer, brake: drivePhase.shape.brake,
    phases: z.array(drivePhase).min(1).max(16).optional(),
    brake_after: z.union([z.boolean(), z.object({ amount: trigV, ms: z.number().int().min(1).max(5000) }).strict()]).optional()
      .describe("Default false. true explicitly requests LT255 for250ms after motion; or give {amount,ms}. LT may reverse instead of stopping."),
    async: asyncArg, replace: replaceArg, ...padCaptureArgs,
    capture_after: captureAfterSchema.default(0),
    capture_settle_ms: padCaptureArgs.capture_settle_ms.describe("Post-reply observation delay, default100ms; async image does not prove job completion."), },
  async (a) => {
    const captureError = padCaptureError(a);
    if (captureError) return fail(captureError);
    if (a.phases && [a.ms, a.throttle, a.steer, a.brake].some((v) => v !== undefined)) return fail("give phases OR top-level ms/controls; nothing was sent");
    if (!a.phases && a.ms === undefined) return fail("give bounded ms/controls or phases; nothing was sent");
    const plan = driveStates(a.phases ?? [{ ms: a.ms!, throttle: a.throttle, steer: a.steer, brake: a.brake }], a.brake_after);
    if (plan.error !== undefined) return fail(plan.error);
    const cmd = { cmd: "pad-timeline", states_json: JSON.stringify(plan.states), ms: plan.total_ms, end_hold_ms: 0, pacing: "time",
      ...(a.async ? { async: true } : {}), ...(a.replace ? { replace: true } : {}) };
    const r = await runVinput([...at(a.pid), "json", JSON.stringify(cmd)], a.async ? 60000 : plan.total_ms! + 60000);
    return look(a.pid, reply(r, { ...(a.async ? jobExtra(r) : {}), pad_drive: { total_ms: plan.total_ms,
      phase_count: a.phases?.length ?? 1, brake_after: plan.brake_after, game_movement_verified: false, stopped_verified: false } }),
      a.capture_after, padObservation({ ...a, capture_settle_ms: a.capture_settle_ms ?? 100 }));
  });

tool("pad_rotate", "Rotate a stick for a prompt",
  "Rotate one stick using a single time-paced timeline: 0 deg=up, 90 deg=right; clockwise increases the angle. About 24 segments per turn " +
  "(25 points for a full circle), constant normalized radius, all other controls neutral. ms is rotation time; end_dwell_ms holds the " +
  "final point before finite native cleanup. Inspect skipped states/lateness; requested dwell is not proof a game polled or accepted the prompt.",
  { pid, stick: z.enum(["left", "right"]), start_deg: z.number().finite().min(-360).max(360), direction: z.enum(["clockwise", "counterclockwise"]),
    turns: z.number().finite().min(0.125).max(4).describe("Fractional turns supported, 1/8..4; a full turn uses 24 segments."),
    ms: z.number().int().min(1).max(60000).describe("Rotation duration only, excluding final dwell. Must allow strictly increasing integer point times."),
    radius: z.number().int().min(1).max(32767).optional().describe("Stick-vector magnitude, default 32767; not separate full-strength X/Y corners."),
    end_dwell_ms: z.number().int().min(1).max(1000).optional().describe("Requested hold of the final non-neutral point, default 50 ms, then native cleanup to neutral."),
    async: asyncArg, replace: replaceArg, ...padCaptureArgs,
    capture_after: captureAfterSchema.default(0).describe("Default 0: backend image after action reply/settle without a Present wait. false disables it; true/numeric keep normal semantics."),
    capture_settle_ms: padCaptureArgs.capture_settle_ms.describe("Post-reply image delay, default 100 ms for pad_rotate. Async capture is an observation, not job completion."), },
  async (a) => {
    const captureError = padCaptureError(a);
    if (captureError) return fail(captureError);
    const radius = a.radius ?? 32767, dwell = a.end_dwell_ms ?? 50;
    const rotation = rotateStick(a.stick, a.start_deg, a.direction, a.turns, a.ms, radius, dwell);
    if (rotation.error !== undefined) return fail(rotation.error);
    const cmd: Record<string, unknown> = { cmd: "pad-timeline", states_json: JSON.stringify(rotation.states), ms: a.ms, end_hold_ms: dwell, pacing: "time" };
    if (a.async) cmd.async = true;
    if (a.replace) cmd.replace = true;
    const request = JSON.stringify(cmd);
    if (Buffer.byteLength(request, "utf8") > 60000) return fail("rotation request exceeds bounded JSON budget; nothing was sent");
    const observation = padObservation({ ...a, capture_settle_ms: a.capture_settle_ms ?? 100 });
    const r = await runVinput([...at(a.pid), "json", request], a.async ? 60000 : rotation.total_ms + 60000);
    return look(a.pid, reply(r, { ...(a.async ? jobExtra(r) : {}), pad_rotate: {
      stick: a.stick, start_deg: a.start_deg, direction: a.direction, turns: a.turns, radius,
      rotation_ms: a.ms, end_dwell_ms: dwell, total_ms: rotation.total_ms, requested_points: rotation.states.length,
      end_deg_unwrapped: rotation.end_deg_unwrapped, game_prompt_verified: false,
      scope: "requested time-paced stick trajectory; native readback and game recognition are not implied" } }), a.capture_after, observation);
  });

const seqStep = z.object({
  action: z.enum(["hold", "key", "wait", "retap"]).describe("hold / key: press chord for ms; wait: no input; retap: keep the chord held while re-pressing only retap_key at each every_ms interval."),
  key: keyCombo.optional().describe("Key or chord (hold / key only), e.g. 'w' or 'w+space'."),
  ms: z.number().int().min(1).max(118000),
  retap_key: keyCombo.optional().describe("retap only: one exact member of key to release/repress; other chord keys stay down."),
  every_ms: z.number().int().min(2).max(118000).optional().describe("retap only: anchored re-tap period, strictly larger than gap_ms."),
  gap_ms: z.number().int().min(1).max(1000).optional().describe("retap only: requested release gap, default 10ms. A short gap may not be read by the game."),
}).strict();
const seqCondition = z.object({kind:z.enum(['frame_change','region_change','text','image']),region:regionRect.optional(),text:z.string().min(1).max(512).optional(),exact:z.boolean().optional(),fuzzy:z.number().min(.01).max(1).optional(),template:z.string().min(1).max(32768).optional(),threshold:z.number().min(.01).max(1).optional(),grace_ms:z.number().int().min(0).max(30000).optional(),
  timeout_ms:z.number().int().min(100).max(60000).optional(),poll_ms:z.number().int().min(20).max(1000).optional(),after_frames:z.number().int().min(1).max(3600).optional(),present_timeout_ms:z.number().int().min(100).max(60000).optional()}).strict();
tool("key_sequence", "Run a keyboard sequence",
  "Run a validated keyboard sequence as ONE background job inside the game: each step presses a key or chord for ms (hold / key) " +
  "or waits (wait), e.g. [{action:'hold',key:'w+space',ms:1500},{action:'wait',ms:100},{action:'key',key:'d',ms:300}]. Returns a job_id at once; " +
  "follow it with job_status (current step) or stop it with job_cancel, which releases the active chord. Keys of later steps are reserved, " +
  "so pressing one of them by hand cancels the rest. Updated DLLs use one QPC anchor for every deadline and expose cumulative_drift_ms and absolute QPC receipts. Wait-only sequences inject no input. Alternatively give schedule:{at_ms:[0,405,810],key:'space',hold_ms:20} for absolute offsets (requires sequence_timing_version>=1). Limits: 1024 steps, 118000 ms of declared timing. Needs a vinput DLL with key sequences. Default is asynchronous; wait:true observes this exact native job until completed or another terminal outcome. capture_after follows successful completion when waiting, otherwise it observes the queued/running job. Native completion does not prove game consumption.",
  { pid, steps: z.array(seqStep).min(1).max(1024).optional(),
    schedule: z.object({at_ms:z.array(z.number().int().min(0).max(118000)).min(1).max(1024),key:keyCombo,hold_ms:z.number().int().min(1).max(118000).optional()}).strict().optional(), replace: replaceArg, ...actionCaptureArgs,
    start_on:seqCondition.optional().describe('Bounded scoped capture/OCR event before admission; frame/region compares against first observation, text matches visible normalized text. Optional after_frames:1..3600 anchors deadlines at that future native Present hook index after the matched native capture. present_timeout_ms defaults5000; wait plus declared duration must fit118000ms. An already missed frame refuses/cancels without starting late. PrintWindow cannot supply this anchor. No display scanout, audio onset or exact game-event alignment.'),
    abort_on:seqCondition.omit({timeout_ms:true,after_frames:true,present_timeout_ms:true}).optional().describe('Requires wait:true. Stop the exact job on a scoped hash change, text (optional fuzzy score), or PNG template. Prefer a small region and image/hash for faster observation without OCR. grace_ms arms after the native job starts and rebaselines hash changes after grace. The matched frame is retained when supported. Polling latency and native observer expiry remain bounded, not hard real-time.'),
    progress_check:z.object({region:regionRect.optional(),max_unchanged_ms:z.number().int().min(100).max(20000)}).strict().optional().describe('Requires wait:true. Stop if the selected frame/region hash remains unchanged; choose a meaningful region. This does not establish gameplay success.'),
    max_lateness_ms:z.number().int().min(1).max(5000).optional().describe('Abort the entire native sequence before an overdue key edge exceeds this bound. start_on defaults1000ms. No input edge is silently skipped or replayed.'),
    capture_every_ms:z.number().int().min(250).max(30000).optional().describe('Requires wait:true. Retain scoped PNGs during THIS same observed native sequence; guarded expiry stays active. First running witness then this interval; missed intervals skipped. Returns frame QPC/job-step brackets and a review sheet, not precise step-to-frame or game-response proof.'),
    capture_max_frames:z.number().int().min(1).max(32).optional().describe('Periodic retained-image cap, default16; recording stops at the cap while the sequence observer continues.'),
    capture_region:regionRect.optional().describe('Periodic recording crop in client pixels; separate from final capture_after region.'),
    capture_max_width:z.number().int().min(16).max(960).optional().describe('Periodic PNG width cap, default640. Per-frame pixel/file and total saved-file budgets also apply.'),
    capture_out_dir:z.string().optional().describe('Directory for this sequence recording and its review sheet; default capture directory. No duplicate default files.'),
    wait: z.boolean().optional().describe("Wait for this exact generation-bound native sequence job. Default false; does not replay input or prove game consumption."),
    wait_timeout_ms: z.number().int().min(1).max(120000).optional().describe("Only with wait:true: observation budget AFTER scheduling, at most120000ms. Default native requested_ms+5000 capped120000. Unguarded timeout leaves the job running; guarded timeout requests one exact abort. Native guard expiry remains active after observer loss. Long waits require sufficient MCP client timeout.") },
  async (a,runtime) => {
    const captureError = actionCaptureError(a);
    if (captureError) return fail(captureError);
    let capturePlan;try{capturePlan=prepareCapturePlan(a);}catch(e){return fail(String(e));}
    if(capturePlan&&!a.wait)return fail('capture_every_ms requires wait:true; no sequence was sent');
    if (a.wait_timeout_ms !== undefined && !a.wait) return fail("wait_timeout_ms requires wait:true");
    if ((a.abort_on||a.progress_check)&&!a.wait) return fail('abort_on/progress_check require wait:true; no sequence was sent');
    if ((a.steps!==undefined)===(a.schedule!==undefined)) return fail("give exactly one of steps or schedule; no sequence was sent");
    const observation = actionObservation({ ...a, async: true });
    const context = { pid: a.pid, owner: OWNER, prefix: at(a.pid), epoch: observation.attachment_epoch,
      currentEpoch: () => captureSessionEpoch.get(a.pid) ?? 0,signal:runtime?.signal };
    let lines: string[] = [];
    if(a.schedule){const built=keyScheduleLines(a.schedule);if(built.error!==undefined)return fail(built.error);lines=built.lines;}
    for (const [i, st] of (a.steps??[]).entries()) {
      if(st.action!=="retap"&&(st.retap_key!==undefined||st.every_ms!==undefined||st.gap_ms!==undefined))return fail(`step ${i+1}: retap options require action:retap; no input was sent`);
      if (st.action === "wait") { if (st.key !== undefined) return fail(`step ${i + 1}: wait takes no key`); lines.push(`wait ${st.ms}`); continue; }
      if (!st.key) return fail(`step ${i + 1}: ${st.action} needs a key`);
      if (/\s/.test(st.key)) return fail(`step ${i + 1}: key names cannot contain spaces`);
      if(st.action==="retap"){
        if(!st.retap_key||/\s/.test(st.retap_key)||st.every_ms===undefined||st.every_ms<=(st.gap_ms??10)||st.every_ms+(st.gap_ms??10)>=st.ms)return fail(`step ${i+1}: retap needs retap_key, every_ms > gap_ms and one complete gap before ms; no input was sent`);
        lines.push(`retap ${st.key} ${st.retap_key} ${st.ms} ${st.every_ms} ${st.gap_ms??10}`);continue;
      }
      lines.push(`${st.action} ${st.key} ${st.ms}`);
    }
    if (a.wait) {
      const unsupported = await requireSequenceWait(context);
      if (unsupported) return reply(unsupported);
    }
    let conditions:PreparedConditions|undefined;
    if(a.start_on||a.abort_on||a.progress_check||capturePlan){const prepared=await prepareSequenceConditions(context,{start_on:a.start_on,abort_on:a.abort_on,progress_check:a.progress_check,capture_plan:capturePlan});
      if('ok' in prepared)return reply(prepared);conditions=prepared;}
    const guarded=!!(a.abort_on||a.progress_check||capturePlan);
    const startExtra=conditions?.start?{sequence_start:{observed:true,observations:conditions.observations,observed_capture_qpc_ticks:conditions.start.qpc,qpc_frequency:conditions.start.frequency,capture_scope:JSON.parse(conditions.start.scope),native_deadlines_anchor:conditions.presentTarget?'native_present_hook':'observed_capture',present_anchor_target_index:conditions.presentTarget??null,game_event_onset_measured:false,scope:'capture/OCR observation time or explicit future native Present hook; neither measures display scanout/game-event onset; missed frame is never rebased'}}:{};
    const dir = mkdtempSync(join(tmpdir(), "vinput-seq-"));
    const file = join(dir, "sequence.vin");
    try {
      writeFileSync(file, lines.join("\n") + "\n");
      if(runtime?.signal?.aborted)return fail('sequence request cancelled before admission; no sequence was sent');
      const r = await runVinput([...at(a.pid), "seq", file, ...(a.replace ? ["--replace=true"] : []),...(conditions?.scope?['--scope-json',conditions.scope]:[]),...(guarded?['--guard-ms','4000']:[]),
        ...(conditions?.start?[...(conditions.presentTarget?['--start-hook-frame',conditions.presentTarget,...opt('--start-hook-timeout-ms',a.start_on?.present_timeout_ms)]:['--start-qpc',conditions.start.qpc]),'--start-frequency',conditions.start.frequency]:[]),...opt('--max-lateness-ms',a.max_lateness_ms??(conditions?.start?1000:undefined))], 60000);
      if(conditions?.start&&r.ok){const j=sequenceJob(r.json,context,true);if(!j||j.qpc_frequency!==conditions.start.frequency||j.capture_scope_checked!==true||(conditions.presentTarget?(j.sequence_frame_anchor_version!==1||j.present_anchor_target_index!==conditions.presentTarget||!['pending_present_hook','native_present_hook'].includes(String(j.anchor_source))):j.anchor_source!=='observed_capture'||j.anchor_qpc_ticks!==conditions.start.qpc))
        return reply(sequenceWaitError(r,'event_anchor_ack_unavailable','sequence event-anchor acknowledgement is inconsistent; no input or control was retried',0,a.wait_timeout_ms),startExtra);}
      if (!a.wait) {const next=sequenceFollowAction(r.json,a.pid);return look(a.pid, reply(r, {...jobExtra(r),...startExtra,...(next?{next_action:next}:{})}), a.capture_after, observation);}
      if (!r.ok) {
        const input = r.json as { input_injected?: unknown; error?: unknown } | null;
        const refused = input?.input_injected === false;
        return reply(sequenceWaitError(r, refused ? "scheduling_refused" : "ack_unavailable",
          `sequence scheduling ${refused ? "refused" : "unconfirmed"}; no input was retried: ${String(input?.error ?? "scheduling response unavailable")}`, 0, a.wait_timeout_ms));
      }
      let observed: CliResult;
      try { observed = guarded&&conditions?await guardSequence(r,context,conditions,a.wait_timeout_ms):await waitSequence(r, context, a.wait_timeout_ms); }
      catch (error) { observed = sequenceWaitError(r, "observation_error", `sequence observation failed; no input was retried: ${String(error)}`, 0, a.wait_timeout_ms); }
      const out=reply(observed,startExtra),matchedPath=(observed.json as {sequence_guard?:{matched_frame_path?:unknown}}|undefined)?.sequence_guard?.matched_frame_path;
      const sheetPath=(observed.json as {sequence_captures?:{contact_sheet?:{path?:unknown}}}|undefined)?.sequence_captures?.contact_sheet?.path;
      if(typeof sheetPath==='string')try{const info=statSync(sheetPath);if(info.size<=MAX_IMAGE_BYTES)out.content.push({type:'text',text:'Sequence review sheet: row-major tiles match sequence_captures.frames; these are not client coordinates.'},{type:'image',data:readFileSync(sheetPath).toString('base64'),mimeType:'image/png'});else out.content.push({type:'text',text:`Sequence review sheet saved at ${sheetPath}; attachment exceeds image limit.`});}catch(e){out.content.push({type:'text',text:`Retained sequence frames are available; review sheet could not be attached: ${String(e)}`});}
      if(typeof matchedPath==='string')try{const info=statSync(matchedPath);if(info.size<=MAX_IMAGE_BYTES)out.content.push({type:'text',text:'Matched sequence abort observation frame:'},{type:'image',data:readFileSync(matchedPath).toString('base64'),mimeType:'image/png'});else out.content.push({type:'text',text:`Matched observation frame saved at ${matchedPath}; attachment exceeds image limit.`});}catch(e){out.content.push({type:'text',text:`Matched observation frame could not be attached: ${String(e)}`});}
      return look(a.pid, out, a.capture_after,
        { ...observation, action_state: "native_sequence_completed_game_consumption_unmeasured" });
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

tool("wait", "Wait without input",
  "Pause for 0..30000 ms using a nonblocking MCP host timer. No target, CLI command, input, claim renewal or frame " +
  "wait is performed. Other tool calls may run meanwhile; elapsed_ms reports actual host elapsed time, not game progress.",
  { ms: z.number().int().min(0).max(30000).describe("Host timer duration in integer milliseconds (0..30000).") },
  async (a) => {
    const started = performance.now();
    if (a.ms > 0) await new Promise<void>((resolve) => setTimeout(resolve, a.ms));
    return { content: [{ type: "text", text: JSON.stringify({ ok: true, requested_ms: a.ms,
      elapsed_ms: Math.max(0, Math.round(performance.now() - started)), scope: "MCP host timer only; no target, input, CLI or frame wait" }) }], isError: false };
  }, true);

tool("run_script", "Run a macro script",
  "Run a UTF-8 .vin file: one ordinary vinput command per line, without --pid/--owner; quoted arguments work, blank lines and lines starting # are ignored. Special lines: sleep <milliseconds>, echo <text>. Example: key w 300\nmove 120 0 --raw\nsleep 150\ncapture shot.png. Relative paths resolve beside the script. Stops at the first failing line unless continue_on_error; use bounded key holds so a failed later command cannot leave an indefinite key down. Nested script/launch/inject/list/foreground commands are refused. This is serial CLI work; use key_sequence for target-native timed chords and wait:true/abort_on observations.",
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

registerCameraHelpers(tool, { owner: OWNER, at, run: runVinput, epoch: p => captureSessionEpoch.get(p) ?? 0,
  captureError: actionCaptureError, finish: (p, out, options, epoch) => look(p, out, options.capture_after, { ...actionObservation(options), attachment_epoch: epoch }) },
  actionCaptureArgs, regionSchema, keyCombo);

registerNavigationMotion(tool, { owner: OWNER, at, run: runVinput, epoch: p => captureSessionEpoch.get(p) ?? 0,
  captureError: actionCaptureError, finish: (p, out, options, epoch) => look(p, out, options.capture_after, { ...actionObservation(options), attachment_epoch: epoch }) },
  actionCaptureArgs, regionSchema, keyCombo);

registerAgentLoops(tool,{owner:OWNER,at,run:runVinput,epoch:p=>captureSessionEpoch.get(p)??0,
  map:(p,a,points,expectedScope)=>mapPoints(p,a,points,expectedScope),qualifyInput:r=>presentMouseDiagnostics(presentInputReads(r)),captureError:actionCaptureError,
  finish:(p,out,options,epoch)=>look(p,out,options.capture_after,{...actionObservation(options),attachment_epoch:epoch})},actionCaptureArgs,regionSchema,coordArgs);

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  process.stderr.write(`llm-game-automation-mcp ${VERSION} ready (vinput: ${resolveVinputExe()}, owner: ${OWNER})\n`);
}
main().catch((e) => { process.stderr.write(`fatal: ${(e as Error).stack ?? e}\n`); process.exit(1); });
