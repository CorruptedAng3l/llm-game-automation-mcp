import { z } from "zod";
import { resolve } from "node:path";
import { type CliResult } from "./vinput.js";

type Obj = Record<string, unknown>;
type Content = { type: "text"; text: string } | { type: "image"; data: string; mimeType: string };
type Reply = { content: Content[]; isError?: boolean };
type Rect = { x: number; y: number; w: number; h: number };
type CaptureOptions = { pid: number; capture_after?: boolean | number; capture_settle_ms?: number; region?: Rect; max_width?: number; scale?: number; format?: "png" | "jpeg"; quality?: number };
type Register = <S extends z.ZodRawShape>(name: string, title: string, description: string, shape: S,
  run: (a: z.objectOutputType<S, z.ZodTypeAny>) => Promise<Reply>, readOnly?: boolean) => void;
export type CameraContext = {
  owner: string;
  at: (pid: number) => string[];
  run: (argv: string[], budget: number) => Promise<CliResult>;
  epoch: (pid: number) => number;
  captureError: (options: CaptureOptions) => string | undefined;
  finish: (pid: number, out: Reply, options: CaptureOptions, epoch: number) => Promise<Reply>;
};
const object = (v: unknown): Obj | undefined => v && typeof v === "object" && !Array.isArray(v) ? v as Obj : undefined;
const response = (json: Obj): Reply => ({ content: [{ type: "text", text: JSON.stringify(json) }], isError: json.ok !== true });
const sleep = (ms: number) => new Promise<void>(resolve => setTimeout(resolve, ms));
const uint = (v: unknown, positive = false): v is number => typeof v === "number" && Number.isSafeInteger(v) && v >= (positive ? 1 : 0);
const u64 = (v: unknown, positive = false) => typeof v === "string" && /^(0|[1-9][0-9]{0,19})$/.test(v) &&
  BigInt(v) <= 18446744073709551615n && (!positive || BigInt(v) > 0n);
function scopeKey(value: unknown): string | undefined {
  const s = object(value);
  if (!s || typeof s.hwnd !== "string" || !/^0x[0-9a-f]+$/i.test(s.hwnd) || BigInt(s.hwnd) === 0n || BigInt(s.hwnd) > 18446744073709551615n ||
      !uint(s.thread, true) || s.thread > 4294967295 || !u64(s.adoption_generation, true) || !u64(s.geometry_epoch) ||
      !u64(s.lifetime_cookie, true) || !uint(s.client_w, true) || !uint(s.client_h, true) || s.client_w > 8192 || s.client_h > 8192) return;
  return JSON.stringify([s.hwnd.toLowerCase(), s.thread, s.adoption_generation, s.geometry_epoch, s.lifetime_cookie, s.client_w, s.client_h]);
}
export type HashSample = { hash: string; scope: string; receipt: Obj };
async function hashFrame(ctx: CameraContext, pid: number, region: Rect | undefined, budget: number): Promise<HashSample> {
  const request = { cmd: "frame-hash", ...(region ? { region: `${region.x},${region.y},${region.w},${region.h}` } : {}), timeout: Math.max(1, Math.min(750, Math.ceil(budget))), preserve_map: true };
  const r = await ctx.run([...ctx.at(pid), "json", JSON.stringify(request)], Math.max(1, Math.min(1500, Math.ceil(budget))));
  const j = object(r.json), scope = scopeKey(j?.capture_scope);
  if (!r.ok || typeof j?.hash !== "string" || !/^[0-9a-f]{16}$/i.test(j.hash) || !scope)
    throw new Error(`scoped frame observation unavailable: ${String(j?.error ?? "missing typed hash/capture scope")}`);
  return { hash: j.hash.toUpperCase(), scope, receipt: j };
}
function sameScope(ctx: CameraContext, pid: number, epoch: number, baseline: HashSample, sample?: HashSample) {
  if (ctx.epoch(pid) !== epoch || (sample && baseline.scope !== sample.scope)) throw new Error("target attachment, adopted window, lifetime, or geometry changed; no further input was sent");
}
function jobId(value: unknown, pid: number): value is string {
  if (typeof value !== "string") return false;
  const parts = /^([1-9][0-9]{0,9}):([1-9][0-9]{0,19}):([1-9][0-9]{0,19})$/.exec(value);
  return !!parts && BigInt(parts[1]) === BigInt(pid) && BigInt(parts[2]) <= 18446744073709551615n && BigInt(parts[3]) <= 18446744073709551615n;
}
const terminalStates = new Set(["completed", "cancelled", "failed"]);
type HoldJob = Obj & { job_id: string; job_state: string; job_complete: boolean; hold_ready: boolean };
function holdJob(value: unknown, ctx: CameraContext, pid: number, duration: number, accepted: boolean, id?: string): HoldJob | undefined {
  const j = object(value);
  if (!j || !jobId(j.job_id, pid) || (id !== undefined && id !== j.job_id) || j.owner !== ctx.owner || j.accepted !== accepted || j.capture_scope_checked !== true ||
      typeof j.job_state !== "string" || !["queued", "running", ...terminalStates].includes(j.job_state) ||
      typeof j.job_complete !== "boolean" || j.job_complete !== terminalStates.has(j.job_state) || typeof j.hold_ready !== "boolean" ||
      (j.job_complete && j.hold_ready) || typeof j.requested_ms !== "number" || j.requested_ms < duration || j.requested_ms > duration + 450) return;
  return j as HoldJob;
}
async function preflight(ctx: CameraContext, pid: number, epoch: number, mode: "hold" | "observe" | "motion" = "hold") {
  const r = await ctx.run([...ctx.at(pid), "status"], 2000), s = object(r.json);
  if (ctx.epoch(pid) !== epoch) throw new Error("attachment changed before input; nothing was sent");
  if (!r.ok || s?.pid !== pid || !uint(s.capture_scope_version, true) ||
      (mode === "hold" && (!uint(s.key_sequence_version, true) || !uint(s.input_scope_version, true))) ||
      (mode === "motion" && !uint(s.relative_scope_version, true)))
    throw new Error(`this helper requires the matching target's scoped capture${mode === "hold" ? " and scope-fenced bounded input-job" : mode === "motion" ? " and scope-fenced relative-motion" : ""} support; no input was sent`);
  if (mode !== "observe" && (s.owner_active === true || object(s.cursor_guard)?.owner_active === true))
    throw new Error("the user currently controls this target; no helper input was sent");
  return s;
}
export type HoldUntilChangeOptions = CaptureOptions & { key: string; timeout_ms?: number; poll_ms?: number };
export async function holdUntilChange(ctx: CameraContext, a: HoldUntilChangeOptions): Promise<Reply> {
  const validation = ctx.captureError({ ...a, region: a.capture_after === undefined || a.capture_after === false ? undefined : a.region });
  if (validation) return response({ ok: false, error: validation, input_injected: false });
  const duration = a.timeout_ms ?? 5000, poll = a.poll_ms ?? 100, epoch = ctx.epoch(a.pid);
  let baseline: HashSample | undefined, admitted: HoldJob | undefined, last: HoldJob | undefined, initial: CliResult | undefined;
  let changed = false, error: string | undefined, reason = "observation_timeout", samples = 0, finalHash: string | undefined, finalReceipt: Obj | undefined;
  let cleanup: Obj = { attempted: false }, releaseConfirmed = false, admissionAttempted = false, explicitlyRefused = false;
  const started = performance.now(); let deadline = started + duration;
  try {
    await preflight(ctx, a.pid, epoch);
    baseline = await hashFrame(ctx, a.pid, a.region, 1500); sameScope(ctx, a.pid, epoch, baseline);
    const jobs = await ctx.run([...ctx.at(a.pid), "jobs"], 1500);
    if (!jobs.ok || !Array.isArray(object(jobs.json)?.jobs)) throw new Error("native job observation unavailable; no hold was sent");
    sameScope(ctx, a.pid, epoch, baseline);
    const request = { cmd: "key", key: a.key, ms: duration, async: true, expected_capture_scope: JSON.stringify(baseline.receipt.capture_scope) };
    admissionAttempted = true;
    initial = await ctx.run([...ctx.at(a.pid), "json", JSON.stringify(request)], 2000);
    explicitlyRefused = !initial.ok && object(initial.json)?.ok === false && object(initial.json)?.input_injected === false;
    if (!initial.ok) throw new Error(`native hold refused: ${String(object(initial.json)?.error ?? "transport failure")}`);
    admitted = holdJob(initial.json, ctx, a.pid, duration, true);
    if (!admitted) throw new Error("hold may have started, but its exact generation/owner acknowledgement is invalid; no input was retried or globally released");
    last = admitted; deadline = performance.now() + duration;
    for (;;) {
      sameScope(ctx, a.pid, epoch, baseline);
      if (performance.now() >= deadline) break;
      const status = await ctx.run([...ctx.at(a.pid), "job-status", admitted.job_id], Math.max(1, Math.min(1500, Math.ceil(deadline - performance.now()))));
      const observed = holdJob(object(status.json)?.job, ctx, a.pid, duration, false, admitted.job_id);
      if (!status.ok || !observed) throw new Error("exact hold job status unavailable or inconsistent; observation stopped");
      last = observed;
      if (last.job_complete) { releaseConfirmed = true; reason = `native_${last.job_state}`; break; }
      sameScope(ctx, a.pid, epoch, baseline);
      if (performance.now() >= deadline) break;
      if (last.hold_ready) {
        const sample = await hashFrame(ctx, a.pid, a.region, deadline - performance.now());
        sameScope(ctx, a.pid, epoch, baseline, sample);if (performance.now() > deadline) break;
        ++samples; finalHash = sample.hash; finalReceipt = sample.receipt;
        if (sample.hash !== baseline.hash) { changed = true; reason = "region_pixels_changed"; break; }
      }
      await sleep(Math.max(0, Math.min(poll, deadline - performance.now())));
    }
  } catch (e) { error = e instanceof Error ? e.message : String(e); reason = "observation_failed"; }
  finally {
    if (admitted && !last?.job_complete && ctx.epoch(a.pid) === epoch) {
      const cancelled = await ctx.run([...ctx.at(a.pid), "job-cancel", admitted.job_id], 2000).catch(() => undefined);
      const cancelledJob = holdJob(object(cancelled?.json)?.job, ctx, a.pid, duration, false, admitted.job_id);
      releaseConfirmed = cancelled?.ok === true && cancelledJob?.job_complete === true;
      cleanup = { attempted: true, job_id: admitted.job_id, confirmed: releaseConfirmed, receipt: cancelled?.json ?? null };
    } else if (admitted && !last?.job_complete) cleanup = { attempted: false, reason: "attachment_changed; native timeout remains the hold bound" };
  }
  const ok = changed && releaseConfirmed && !error;
  const out = response({ ok, pid: a.pid, changed, reason, ...(error ? { error } : !changed ? { error: "selected pixels did not change during the observed hold" } : !releaseConfirmed ? { error: "pixels changed, but exact-job cancellation was not confirmed" } : {}),
    hold_input: initial?.json ?? null, job_id: admitted?.job_id ?? null, native_timeout_ms_requested: duration, release_confirmed: releaseConfirmed,
    admission_attempted: admissionAttempted, admission_explicitly_refused: explicitlyRefused,
    hold_may_still_be_active: admissionAttempted && !explicitlyRefused && !releaseConfirmed, cleanup, samples, elapsed_ms: Math.round(performance.now() - started),
    before_hash: baseline?.hash ?? null, after_hash: finalHash ?? null,
    baseline_observation: baseline?.receipt ?? null, final_observation: finalReceipt ?? null,
    scope: "bounded native virtual hold plus scoped pixel-change observation; pixel change does not prove the hold caused it or gameplay progressed" });
  return ctx.finish(a.pid, out, a, epoch);
}

type SettleOptions = CaptureOptions & { timeout_ms?: number; poll_ms?: number; stable_samples?: number; settle_timeout_ms?: number };
type SettleResult = { stable: boolean; samples: number; changes: number; elapsed_ms: number; last_change_ms: number; stable_window_ms: number; sample: HashSample };
const smoothingHint = "A settled pixel region is not a raw-input drain measurement or a camera-angle measurement. Game camera smoothing, animated overlays, a paused scene, pitch clamps and input rejection can affect observations; inspect the game's camera smoothing settings.";
async function settle(ctx: CameraContext, a: SettleOptions, epoch: number, baseline: HashSample, deadline: number): Promise<SettleResult> {
  const started = performance.now(), stableSamples = a.stable_samples ?? 3, poll = a.poll_ms ?? 100;
  let last = baseline, consecutive = 0, samples = 0, changes = 0, changedAt = started;
  while (performance.now() < deadline) {
    sameScope(ctx, a.pid, epoch, baseline);
    await sleep(Math.max(0, Math.min(poll, deadline - performance.now())));
    if (performance.now() >= deadline) break;
    const sample = await hashFrame(ctx, a.pid, a.region, deadline - performance.now());sameScope(ctx, a.pid, epoch, baseline, sample);
    if (performance.now() > deadline) break;
    samples++;
    if (sample.hash === last.hash) consecutive++;else { changes++;consecutive = 0;changedAt = performance.now(); }
    last = sample;
    if (consecutive >= stableSamples) return { stable: true, samples, changes, elapsed_ms: Math.round(performance.now() - started), last_change_ms: Math.round(changedAt - started), stable_window_ms: Math.round(performance.now() - changedAt), sample: last };
  }
  return { stable: false, samples, changes, elapsed_ms: Math.round(performance.now() - started), last_change_ms: Math.round(changedAt - started), stable_window_ms: Math.round(performance.now() - changedAt), sample: last };
}
const settleView = (r: SettleResult) => ({ ...r, sample: r.sample.receipt, queue_drain_measured: false, camera_angle_measured: false, raw_queue_remaining_counts: null, scope: "exact selected-region hash stability; no causal camera/gameplay claim" });
function captureValidation(ctx: CameraContext, a: CaptureOptions) { return ctx.captureError({ ...a, region: a.capture_after === undefined || a.capture_after === false ? undefined : a.region }); }
export async function observeSettle(ctx: CameraContext, a: SettleOptions): Promise<Reply> {
  const invalid = captureValidation(ctx, a);if (invalid) return response({ ok: false, error: invalid });
  const epoch = ctx.epoch(a.pid), started = performance.now();
  try {
    await preflight(ctx, a.pid, epoch, "observe");const baseline = await hashFrame(ctx, a.pid, a.region, 1500);
    const observed = await settle(ctx, a, epoch, baseline, performance.now() + (a.timeout_ms ?? 5000));
    return ctx.finish(a.pid, response({ ok: observed.stable, pid: a.pid, settled: observed.stable, observation: settleView(observed),
      ...(!observed.stable ? { error: "region did not remain hash-stable within the observation deadline" } : {}),
      elapsed_ms: Math.round(performance.now() - started), hint: smoothingHint }), a, epoch);
  } catch (e) { return ctx.finish(a.pid, response({ ok: false, pid: a.pid, settled: false, error: e instanceof Error ? e.message : String(e), hint: smoothingHint }), a, epoch); }
}
type TargetOptions = SettleOptions & { template: string; threshold?: number };
type Point = { x: number; y: number; receipt: Obj };
class MotionObservationError extends Error {
  constructor(message: string, readonly receipt: unknown, readonly mayHaveApplied: boolean) { super(message); }
}
const motionFailure = (e: unknown): Obj => e instanceof MotionObservationError ? { unconfirmed_motion: e.receipt, motion_may_have_been_applied: e.mayHaveApplied } : {};
async function target(ctx: CameraContext, a: TargetOptions, epoch: number, baseline: HashSample, deadline: number): Promise<Point> {
  sameScope(ctx, a.pid, epoch, baseline);
  const help = await ctx.run(["help"], Math.max(1, Math.min(1500, Math.ceil(deadline - performance.now()))));
  if (!help.ok || !uint(object(help.json)?.image_scope_version, true)) throw new Error("CLI lacks scoped map-preserving visual observations; no camera motion was sent");
  sameScope(ctx, a.pid, epoch, baseline);
  const args = [...ctx.at(a.pid), "find-image", resolve(a.template), "--threshold", String(a.threshold ?? 0.9),
    "--preserve-map", ...(a.region ? ["--region", `${a.region.x},${a.region.y},${a.region.w},${a.region.h}`] : [])];
  const r = await ctx.run(args, Math.max(1, Math.min(2000, Math.ceil(deadline - performance.now())))), j = object(r.json);
  if (scopeKey(j?.capture_scope) !== baseline.scope || j?.capture_space_preserved !== true) throw new Error("visual observation scope/map-preservation was unavailable or changed; no further camera motion was sent");
  if (!r.ok || j?.match_count !== 1 || !Array.isArray(j.matches) || j.matches.length !== 1) throw new Error("visual target is missing or ambiguous; no further camera motion was sent");
  const match = object(j.matches[0]), center = match?.center;
  if (!Array.isArray(center) || center.length !== 2 || !center.every(v => typeof v === "number" && Number.isFinite(v) && v >= 0) ||
      typeof match?.score !== "number" || !Number.isFinite(match.score) || match.score < (a.threshold ?? 0.9) || match.score > 1 ||
      center[0] >= (object(baseline.receipt.capture_scope)!.client_w as number) || center[1] >= (object(baseline.receipt.capture_scope)!.client_h as number)) throw new Error("visual target observation is malformed; no further camera motion was sent");
  const current = await hashFrame(ctx, a.pid, a.region, deadline - performance.now());sameScope(ctx, a.pid, epoch, baseline, current);
  if (performance.now() > deadline) throw new Error("camera helper deadline expired before motion; no further motion was sent");
  return { x: center[0] as number, y: center[1] as number, receipt: j };
}
async function scopedMove(ctx: CameraContext, a: TargetOptions, epoch: number, baseline: HashSample, dx: number, dy: number, deadline: number): Promise<Obj> {
  await preflight(ctx, a.pid, epoch, "motion");sameScope(ctx, a.pid, epoch, baseline);
  const current = await hashFrame(ctx, a.pid, a.region, deadline - performance.now());sameScope(ctx, a.pid, epoch, baseline, current);
  if (performance.now() >= deadline) throw new Error("camera helper deadline expired before motion; no input was sent");
  const request = { cmd: "move", dx, dy, raw: true, steps: 1, step_ms: 0, expected_capture_scope: JSON.stringify(current.receipt.capture_scope) };
  let r: CliResult;
  try { r = await ctx.run([...ctx.at(a.pid), "json", JSON.stringify(request)], Math.max(1, Math.min(2000, Math.ceil(deadline - performance.now())))); }
  catch (e) { throw new MotionObservationError(`camera-motion acknowledgement unavailable; input was not replayed: ${String(e)}`, null, true); }
  const j = object(r.json), refused = !r.ok && j?.ok === false && j.input_injected === false;
  if (ctx.epoch(a.pid) !== epoch) throw new MotionObservationError("attachment changed after camera-motion submission; input was not replayed", r.json, !refused);
  if (!r.ok || j?.capture_scope_checked !== true || j.unbounded !== true || j.raw_dx !== dx || j.raw_dy !== dy)
    throw new MotionObservationError(`scoped relative motion refused or unconfirmed; input was not replayed: ${String(j?.error ?? "missing exact scope/delta acknowledgement")}`, r.json, !refused);
  return j;
}
type Matrix = { xx: number; xy: number; yx: number; yy: number };
type Calibration = { matrix: Matrix; capture_scope: unknown };
export type LookCalibrateOptions = TargetOptions & { counts?: number };
export async function lookCalibrate(ctx: CameraContext, a: LookCalibrateOptions): Promise<Reply> {
  const invalid = captureValidation(ctx, a);if (invalid) return response({ ok: false, error: invalid });
  const epoch = ctx.epoch(a.pid), started = performance.now(), observations: Obj[] = [], motions: Obj[] = [];
  const deadline = started + (a.timeout_ms ?? 15000), count = a.counts ?? 80;
  try {
    await preflight(ctx, a.pid, epoch, "motion");const baseline = await hashFrame(ctx, a.pid, a.region, 1500);
    const initial = await settle(ctx, a, epoch, baseline, Math.min(deadline, performance.now() + (a.settle_timeout_ms ?? 1500)));observations.push(settleView(initial));
    if (!initial.stable) throw new Error("region was not stable before calibration; no calibration motion was sent");
    let last = await target(ctx, a, epoch, baseline, deadline);const origin = { x: last.x, y: last.y }, responses: { x: number; y: number }[] = [];
    for (const axis of ["x", "y"] as const) {
      const before = last;motions.push(await scopedMove(ctx, a, epoch, baseline, axis === "x" ? count : 0, axis === "y" ? count : 0, deadline));
      const after = await settle(ctx, a, epoch, baseline, Math.min(deadline, performance.now() + (a.settle_timeout_ms ?? 1500)));observations.push(settleView(after));
      if (!after.stable) throw new Error("region kept changing after the calibration probe; camera smoothing or other animation may still be active");
      last = await target(ctx, a, epoch, baseline, deadline);responses.push({ x: (last.x - before.x) / count, y: (last.y - before.y) / count });
    }
    const matrix = { xx: responses[0].x, xy: responses[1].x, yx: responses[0].y, yy: responses[1].y }, determinant = matrix.xx * matrix.yy - matrix.xy * matrix.yx;
    if (!Number.isFinite(determinant) || Math.abs(determinant) < 0.000001) throw new Error("visual response was absent, clamped or singular; no usable two-axis calibration was established");
    return ctx.finish(a.pid, response({ ok: true, pid: a.pid, calibration: { matrix, capture_scope: baseline.receipt.capture_scope },
      probe_counts_per_axis: count, initial_target: origin, final_target: { x: last.x, y: last.y }, observations, motions,
      degrees_per_count: null, queue_drain_measured: false, elapsed_ms: Math.round(performance.now() - started), hint: smoothingHint,
      scope: "local signed visual-template pixel displacement per submitted mouse count; assumes a static unique landmark; not a camera-angle or input-consumption measurement" }), a, epoch);
  } catch (e) { return ctx.finish(a.pid, response({ ok: false, pid: a.pid, calibration: null, observations, motions, ...motionFailure(e),
    error: e instanceof Error ? e.message : String(e), elapsed_ms: Math.round(performance.now() - started), hint: smoothingHint }), a, epoch); }
}
export type LookAtOptions = TargetOptions & { calibration: Calibration; center_x?: number; center_y?: number; tolerance_px?: number; max_steps?: number; max_counts_per_step?: number };
export async function lookAt(ctx: CameraContext, a: LookAtOptions): Promise<Reply> {
  const invalid = captureValidation(ctx, a);if (invalid) return response({ ok: false, error: invalid });
  const epoch = ctx.epoch(a.pid), started = performance.now(), deadline = started + (a.timeout_ms ?? 15000), steps: Obj[] = [];
  try {
    await preflight(ctx, a.pid, epoch, "motion");const baseline = await hashFrame(ctx, a.pid, a.region, 1500);
    const m = a.calibration.matrix, determinant = m.xx * m.yy - m.xy * m.yx;
    if (scopeKey(a.calibration.capture_scope) !== baseline.scope || !Object.values(m).every(Number.isFinite) || !Number.isFinite(determinant) || Math.abs(determinant) < 0.000001)
      throw new Error("calibration is invalid or belongs to another target window/geometry; no motion was sent");
    const ownScope = object(baseline.receipt.capture_scope)!, desired = { x: a.center_x ?? (ownScope.client_w as number) / 2, y: a.center_y ?? (ownScope.client_h as number) / 2 };
    if (desired.x >= (ownScope.client_w as number) || desired.y >= (ownScope.client_h as number)) throw new Error("requested center lies outside the current client; no motion was sent");
    const tolerance = a.tolerance_px ?? 6, limit = a.max_counts_per_step ?? 150;
    const initial = await settle(ctx, a, epoch, baseline, Math.min(deadline, performance.now() + (a.settle_timeout_ms ?? 1500)));
    if (!initial.stable) throw new Error("region was not stable before camera correction; no motion was sent");
    let last = await target(ctx, a, epoch, baseline, deadline), distance = Math.hypot(desired.x - last.x, desired.y - last.y);
    for (let index = 0;distance > tolerance && index < (a.max_steps ?? 8);++index) {
      const ex = desired.x - last.x, ey = desired.y - last.y;
      let dx = (m.yy * ex - m.xy * ey) / determinant, dy = (-m.yx * ex + m.xx * ey) / determinant;
      if (!Number.isFinite(dx) || !Number.isFinite(dy)) throw new Error("calibration correction overflowed; no further motion was sent");
      const scale = Math.max(1, Math.abs(dx) / limit, Math.abs(dy) / limit);dx = Math.round(dx / scale);dy = Math.round(dy / scale);
      if (!dx && !dy) throw new Error("requested visual error is below calibrated count resolution; no further motion was sent");
      const move = await scopedMove(ctx, a, epoch, baseline, dx, dy, deadline), record: Obj = { dx, dy, move };steps.push(record);
      const observed = await settle(ctx, a, epoch, baseline, Math.min(deadline, performance.now() + (a.settle_timeout_ms ?? 1500)));record.observation = settleView(observed);
      if (!observed.stable) throw new Error("camera region did not settle before its deadline; no additional motion was sent");
      const next = await target(ctx, a, epoch, baseline, deadline), nextDistance = Math.hypot(desired.x - next.x, desired.y - next.y);record.target = { x: next.x, y: next.y };record.error_px = nextDistance;
      if (nextDistance > tolerance && nextDistance >= distance - 0.5) throw new Error("no observed visual progress with this calibration; input was not repeated blindly");
      last = next;distance = nextDistance;
    }
    const centered = distance <= tolerance;
    return ctx.finish(a.pid, response({ ok: centered, pid: a.pid, centered, target: { x: last.x, y: last.y }, desired, error_px: distance, initial_observation: settleView(initial), steps,
      ...(!centered ? { error: "camera correction reached its step bound" } : {}), elapsed_ms: Math.round(performance.now() - started), hint: smoothingHint,
      scope: "reidentified visual-template center within screen-pixel tolerance; no aim, camera-angle or gameplay-progress guarantee" }), a, epoch);
  } catch (e) { return ctx.finish(a.pid, response({ ok: false, pid: a.pid, centered: false, steps, ...motionFailure(e), error: e instanceof Error ? e.message : String(e), elapsed_ms: Math.round(performance.now() - started), hint: smoothingHint }), a, epoch); }
}

export function registerCameraHelpers(register: Register, ctx: CameraContext, actionCaptureArgs: z.ZodRawShape,
  regionSchema: z.ZodType<Rect, z.ZodTypeDef, unknown>, keyCombo: z.ZodType<string>) {
  register("hold_until_change", "Hold until pixels change",
    "Run one bounded native key/chord hold and watch a selected region. Stops on a scoped pixel change or timeout and cancels only its exact native job. The native duration releases even if this MCP disconnects. Pick a region without animated overlays; a changed hash is an observation, not proof of gameplay progress. capture_after uses the same image options as other actions.",
    { ...actionCaptureArgs, pid: z.number().int().positive(), key: keyCombo, region: regionSchema.optional(),
      timeout_ms: z.number().int().min(100).max(118000).optional().describe("Native hold duration and observation bound, default 5000 ms; transport/cleanup can add a bounded delay."),
      poll_ms: z.number().int().min(50).max(1000).optional() },
    async a => holdUntilChange(ctx, a as HoldUntilChangeOptions));
  const observe = { ...actionCaptureArgs, pid: z.number().int().positive(), region: regionSchema.optional(),
    timeout_ms: z.number().int().min(100).max(60000).optional(), poll_ms: z.number().int().min(50).max(1000).optional(),
    stable_samples: z.number().int().min(2).max(10).optional().describe("Consecutive identical scoped region hashes, default 3; excludes neither pauses nor animation outside the region.") };
  register("observe_settle", "Observe region stability", "Observe until a selected region has repeated identical hashes or the deadline expires. Reports changes and stable-window timing without moving input. A quiet region does not prove the native queue drained or a camera settled; avoid animated HUD/overlays.",
    observe, async a => observeSettle(ctx, a as SettleOptions), true);
  const targetArgs = { ...observe, template: z.string().min(1).max(32768).describe("PNG template of a unique static visual landmark, reidentified after every probe; ambiguity/missing target stops motion."),
    threshold: z.number().min(0.01).max(1).optional(), settle_timeout_ms: z.number().int().min(100).max(10000).optional().describe("Per-probe pixel-stability observation budget, default 1500 ms; the overall deadline still bounds the helper.") };
  register("look_calibrate", "Calibrate visual mouse response", "Submit two small raw relative mouse probes, wait for selected-region hash stability, and reidentify one unique visual landmark. Returns a local signed 2x2 pixel-per-count matrix and settle observations for look_at. It moves the view; camera angles and raw queue drain are not measured. Requires native relative_scope_version support.",
    { ...targetArgs, counts: z.number().int().min(10).max(200).optional() }, async a => lookCalibrate(ctx, a as LookCalibrateOptions));
  register("look_at", "Center a visual landmark", "Reidentify a unique template and iteratively center it using a same-window look_calibrate matrix. Every raw move is scope-fenced, bounded to max_counts_per_step, followed by settle/target checks, and stops on missing target, no visual progress, timeout or step limit. This centers an image landmark; it does not establish aim or gameplay progress.",
    { ...targetArgs, calibration: z.object({ matrix: z.object({ xx: z.number().finite(), xy: z.number().finite(), yx: z.number().finite(), yy: z.number().finite() }).strict(), capture_scope: z.unknown() }).strict(),
      center_x: z.number().min(0).max(8192).optional(), center_y: z.number().min(0).max(8192).optional(), tolerance_px: z.number().min(1).max(100).optional(),
      max_steps: z.number().int().min(1).max(12).optional(), max_counts_per_step: z.number().int().min(1).max(200).optional() },
    async a => lookAt(ctx, a as LookAtOptions));
}
