import { z } from 'zod';
import {MOVE_DIRECTIONS,moveVector} from './controller.js';
import type { CameraContext } from './camerahelpers.js';
import type { CliResult } from './vinput.js';
import { canonicalSequenceScope } from './sequenceconditions.js';
import { motionDifference, motionSample, type MotionSample } from './motion.js';

type Obj = Record<string, unknown>;
type Rect = { x: number; y: number; w: number; h: number };
type Reply = { content: ({ type: 'text'; text: string } | { type: 'image'; data: string; mimeType: string })[]; isError?: boolean };
type Capture = Parameters<CameraContext['captureError']>[0];
export type PadNavigationOptions = Capture & { direction: typeof MOVE_DIRECTIONS[number]; speed?: 'walk'|'run'; screen_rotation_deg?: number; region: Rect; timeout_ms?: number; poll_ms?: number; grace_ms?: number; still_ms?: number; max_change_pct?: number; pixel_tolerance?: number };
type Register = <S extends z.ZodRawShape>(name: string, title: string, description: string, shape: S, run: (a: z.objectOutputType<S, z.ZodTypeAny>) => Promise<Reply>, readOnly?: boolean) => void;
type Sample = { scope: string; qpc: bigint; frequency: bigint; motion: MotionSample; receipt: Obj };
type Job = Obj & { job_id: string; job_state: string; job_complete: boolean; hold_ready: boolean; requested_ms: number };
const obj = (v: unknown): Obj | undefined => v !== null && typeof v === 'object' && !Array.isArray(v) ? v as Obj : undefined;
const reply = (json: Obj): Reply => ({ content: [{ type: 'text', text: JSON.stringify(json) }], isError: json.ok !== true });
const sleep = (ms: number) => new Promise<void>(r => setTimeout(r, ms));
const integer = (v: unknown, min: number, max: number): v is number => typeof v === 'number' && Number.isInteger(v) && v >= min && v <= max;
const u64 = (v: unknown): v is string => typeof v === 'string' && /^[1-9][0-9]{0,19}$/.test(v) && BigInt(v) <= 18446744073709551615n;
type Axes = {lt:number;rt:number;lx:number;ly:number;rx:number;ry:number};
function sameAxes(value:unknown,axes:Axes):boolean{const a=obj(value);return !!a&&Object.keys(a).length===6&&Object.entries(axes).every(([key,v])=>a[key]===v);}
const terminal = new Set(['completed', 'cancelled', 'failed']);
function ownedJob(value: unknown, ctx: CameraContext, pid: number, duration: number, axes: Axes, accepted: boolean, id?: string): Job | undefined {
  const j = obj(value), match = typeof j?.job_id === 'string' ? /^([1-9][0-9]{0,9}):([1-9][0-9]{0,19}):([1-9][0-9]{0,19})$/.exec(j.job_id) : null;
  if (!j || !match || BigInt(match[1]) !== BigInt(pid) || !u64(match[2]) || !u64(match[3]) || (id !== undefined && j.job_id !== id) ||
      j.owner !== ctx.owner || j.job_kind !== 'pad' || j.pad_motion_job_version !== 1 || j.buttons !== 0 || j.axes_set_mask !== 63 || j.hold !== false || j.clamped !== false || !sameAxes(j.requested_axes,axes) || j.accepted !== accepted || j.capture_scope_checked !== true ||
      j.requested_ms !== duration ||
      typeof j.job_state !== 'string' || !['queued', 'running', ...terminal].includes(j.job_state) || typeof j.job_complete !== 'boolean' ||
      j.job_complete !== terminal.has(j.job_state) || typeof j.hold_ready !== 'boolean' || (j.hold_ready && j.job_state !== 'running')) return;
  return j as Job;
}

/** One finite native hold; pixel stability is an observation, never avatar displacement. */
export async function padMoveUntilStuck(ctx: CameraContext, a: PadNavigationOptions): Promise<Reply> {
  const duration = a.timeout_ms ?? 3000, poll = a.poll_ms ?? 100, grace = a.grace_ms ?? 250, still = a.still_ms ?? 500;
  const maxChange = a.max_change_pct ?? 1, tolerance = a.pixel_tolerance ?? 8, region = a.region;
  if (!integer(a.pid, 1, 4294967295) || !MOVE_DIRECTIONS.some(d=>d===a.direction) || (a.speed !== undefined && a.speed !== 'walk' && a.speed !== 'run') || !Number.isFinite(a.screen_rotation_deg??0) || Math.abs(a.screen_rotation_deg??0)>360 || !region ||
      !integer(region.x, 0, 32767) || !integer(region.y, 0, 32767) || !integer(region.w, 1, 32768) || !integer(region.h, 1, 32768) ||
      !integer(duration, 100, 15000) || !integer(poll, 20, 1000) || !integer(grace, 0, 5000) || !integer(still, 100, 5000) ||
      !Number.isFinite(maxChange) || maxChange < 0 || maxChange > 100 || !integer(tolerance, 0, 255))
    return reply({ ok: false, input_injected: false, error: 'pad_move_until_stuck needs a bounded left-stick hold, a client region, and valid motion timing/tolerance options' });
  const speed=a.speed??'walk',vector=moveVector(a.direction,speed,a.screen_rotation_deg??0);
  const axes:Axes=Object.freeze({lt:0,rt:0,lx:vector.lx,ly:vector.ly,rx:0,ry:0});
  const invalid = ctx.captureError({ ...a, region: a.capture_after === undefined || a.capture_after === false ? undefined : a.region });
  if (invalid) return reply({ ok: false, input_injected: false, error: invalid });
  const epoch = ctx.epoch(a.pid), started = performance.now();
  let baseline: Sample | undefined, first: Sample | undefined, lastSample: Sample | undefined, quiet: Sample | undefined;
  let admitted: Job | undefined, lastJob: Job | undefined, initial: CliResult | undefined, finalJob: Job | undefined;
  let admissionAttempted = false, refused = false, releaseConfirmed = false, holdStarted = false;
  let stopped = 'timeout', error: string | undefined, deadline = 0, runningAt = 0, samples = 0, comparisons = 0, quietComparisons = 0;
  let maxStep = 0, quietMs = 0, cleanup: Obj = { attempted: false };
  function current() { if (ctx.epoch(a.pid) !== epoch) throw new Error('target attachment changed; no further input or cleanup was sent to the new attachment'); }
  function ended(job: Job): boolean {
    lastJob = job;
    if (!job.job_complete) return false;
    releaseConfirmed = true; finalJob = job; stopped = `native_${job.job_state}`;
    if (job.job_state !== 'completed') error = `native hold ended ${job.job_state}: ${String(job.job_reason ?? 'reason unavailable')}`;
    return true;
  }
  async function sample(budget: number): Promise<Sample> {
    current(); if (budget <= 0) throw new Error('observation deadline expired');
    const r = await ctx.run([...ctx.at(a.pid), 'json', JSON.stringify({ cmd: 'frame-hash', observation_only: true, preserve_map: true, motion_sample: true,
      timeout_ms: Math.max(1, Math.min(750, Math.ceil(budget))), region: `${region.x},${region.y},${region.w},${region.h}` })], Math.max(1, Math.min(1500, Math.ceil(budget))));
    current(); const j = obj(r.json), scope = canonicalSequenceScope(j?.capture_scope), motion = motionSample(j);
    if (!r.ok || !j || !scope || !motion || j.capture_space_updated !== false || j.timing_clock !== 'QueryPerformanceCounter' || !u64(j.capture_qpc_ticks) || !u64(j.qpc_frequency))
      throw new Error('scoped RGB-grid capture or its map-preservation/QPC receipt is unavailable');
    const s = { scope, motion, qpc: BigInt(j.capture_qpc_ticks), frequency: BigInt(j.qpc_frequency), receipt: j };
    if (baseline && (s.scope !== baseline.scope || s.frequency !== baseline.frequency || s.qpc < baseline.qpc)) throw new Error('capture window, lifetime, geometry or clock changed');
    if (lastSample && s.qpc <= lastSample.qpc) throw new Error('capture clock did not advance; no pixel-stability verdict was made');
    return s;
  }
  try {
    const r = await ctx.run([...ctx.at(a.pid), 'status'], 2000), s = obj(r.json); current();
    if (!r.ok || s?.pid !== a.pid || !integer(s.capture_scope_version, 1, 1000) || s.pad_motion_job_version !== 1 || !integer(s.input_scope_version, 1, 1000))
      throw new Error('matching target lacks scoped capture and scope-fenced native input jobs; no hold was sent');
    if (s.owner_active === true || obj(s.cursor_guard)?.owner_active === true) throw new Error('the user currently controls this target; no hold was sent');
    baseline = await sample(1500);
    const jobs = await ctx.run([...ctx.at(a.pid), 'jobs'], 1500); current();
    if (!jobs.ok || !Array.isArray(obj(jobs.json)?.jobs)) throw new Error('native job observation unavailable; no hold was sent');
    for(const value of obj(jobs.json)!.jobs as unknown[]){const existing=obj(value);
      if(!existing||typeof existing.job_complete!=='boolean'||typeof existing.job_kind!=='string')throw new Error('native job inventory is unrecognized; no pad hold was sent');
      if(!existing.job_complete&&['pad','ramp','timeline'].includes(existing.job_kind))throw new Error('an existing native pad job owns the pad resource; no hold or takeover was sent');
    }
    admissionAttempted = true;
    initial = await ctx.run([...ctx.at(a.pid), 'json', JSON.stringify({ cmd: 'pad', buttons:'none', ...axes, ms: duration, async: true, expected_capture_scope: baseline.scope })], 2000);
    refused = !initial.ok && obj(initial.json)?.ok === false && obj(initial.json)?.input_injected === false;
    if (!initial.ok) throw new Error(`native hold refused or admission is unconfirmed: ${String(obj(initial.json)?.error ?? 'transport failure')}`);
    admitted = ownedJob(initial.json, ctx, a.pid, duration, axes, true);
    if (!admitted) throw new Error('hold admission lacks an exact generation/owner receipt; no input was replayed or globally released');
    lastJob = admitted; deadline = performance.now() + duration;
    for (;;) {
      current(); if (performance.now() >= deadline) break;
      const status = await ctx.run([...ctx.at(a.pid), 'job-status', admitted.job_id], Math.max(1, Math.min(1500, Math.ceil(deadline - performance.now())))); current();
      // A CLI process may exhaust the remaining observation budget. The exact
      // admitted job still authorizes one cleanup; a late reply cannot create
      // either a pixel verdict or a spurious status-contract failure.
      if (performance.now() >= deadline) break;
      const job = ownedJob(obj(status.json)?.job, ctx, a.pid, duration, axes, false, admitted.job_id);
      if (!status.ok || !job) throw new Error('exact held job status is unavailable or inconsistent');
      if (ended(job)) break;
      if (performance.now() >= deadline) break;
      if (job.job_state === 'running' && job.hold_ready) {
        if (!holdStarted) { holdStarted = true; runningAt = performance.now(); }
        let seen: Sample;
        try { seen = await sample(deadline - performance.now()); }
        catch (e) { current(); if (performance.now() >= deadline) break; throw e; }
        if (performance.now() > deadline) break;
        // Bracket the capture with exact running witnesses. A terminal job
        // during capture must not turn its now-still image into no_motion.
        const witness = await ctx.run([...ctx.at(a.pid), 'job-status', admitted.job_id], Math.max(1, Math.min(1500, Math.ceil(deadline - performance.now())))); current();
        if (performance.now() >= deadline) break;
        const after = ownedJob(obj(witness.json)?.job, ctx, a.pid, duration, axes, false, admitted.job_id);
        if (!witness.ok || !after) throw new Error('post-capture exact held job witness is unavailable or inconsistent');
        if (ended(after)) break;
        if (after.job_state !== 'running' || !after.hold_ready) throw new Error('hold stopped running during capture; no pixel-stability verdict was made');
        ++samples; first ??= seen;
        if (lastSample) { const step = motionDifference(lastSample.motion, seen.motion, tolerance); if (!step) throw new Error('motion sample layout changed'); ++comparisons; maxStep = Math.max(maxStep, step.changed_pct); }
        lastSample = seen;
        if (performance.now() - runningAt < grace) { quiet = undefined; quietComparisons = 0; quietMs = 0; }
        else if (!quiet) { quiet = seen; quietComparisons = 0; quietMs = 0; }
        else {
          const diff = motionDifference(quiet.motion, seen.motion, tolerance); if (!diff) throw new Error('motion sample layout changed');
          if (diff.changed_pct > maxChange) { quiet = seen; quietComparisons = 0; quietMs = 0; }
          else {
            ++quietComparisons; const span = seen.qpc - quiet.qpc;
            quietMs = Number(span * 1000n / seen.frequency);
            if (quietComparisons >= 2 && span * 1000n >= BigInt(still) * seen.frequency) { stopped = 'no_motion'; break; }
          }
        }
      }
      await sleep(Math.max(0, Math.min(poll, deadline - performance.now())));
    }
  } catch (e) { error = e instanceof Error ? e.message : String(e); stopped = 'observation_failed'; }
  finally {
    if (admitted && !lastJob?.job_complete && ctx.epoch(a.pid) === epoch) {
      const cancel = await ctx.run([...ctx.at(a.pid), 'job-cancel', admitted.job_id], 2000).catch(() => undefined);
      const job = ownedJob(obj(cancel?.json)?.job, ctx, a.pid, duration, axes, false, admitted.job_id);
      releaseConfirmed = ctx.epoch(a.pid) === epoch && cancel?.ok === true && job?.job_complete === true;
      finalJob = job; cleanup = { attempted: true, job_id: admitted.job_id, confirmed: releaseConfirmed, receipt: cancel?.json ?? null };
      if (releaseConfirmed && job && (job.job_state === 'failed' || job.job_state === 'cancelled' && job.job_reason !== 'explicit_cancel')) {
        stopped = `native_${job.job_state}`;
        error = `native hold ended ${job.job_state}: ${String(job.job_reason ?? 'reason unavailable')}; no successful no-motion result was inferred`;
      }
    } else if (admitted && !lastJob?.job_complete) cleanup = { attempted: false, reason: 'attachment_changed; finite native timeout remains the hold bound' };
  }
  if (admitted && !releaseConfirmed && !error) error = 'exact-job release was not confirmed; native timeout remains the hold bound';
  const total = motionDifference(first?.motion, lastSample?.motion, tolerance), ok = !!admitted && releaseConfirmed && !error;
  const out = reply({ ok, pid: a.pid, stopped, ...(error ? { error } : {}), timed_out: stopped === 'timeout',
    input_ok: !!admitted, admission_attempted: admissionAttempted, admission_explicitly_refused: refused, job_id: admitted?.job_id ?? null,
    hold_started: holdStarted, release_confirmed: releaseConfirmed, hold_may_still_be_active: admissionAttempted && !refused && !releaseConfirmed,
    hold_input: initial?.json ?? null, final_job: finalJob ?? lastJob ?? null, input_read_observation: (finalJob ?? lastJob)?.input_read_observation ?? null,
    region, samples, elapsed_ms: Math.round(performance.now() - started), grace_ms: grace, still_ms: still, max_change_pct: maxChange, pixel_tolerance: tolerance,
    motion_score: { measured: comparisons > 0, metric: 'coarse_grid_area_average_RGB', ...(comparisons > 0 ? total : {}), comparisons, max_step_changed_pct: comparisons ? maxStep : null, quiet_window_ms: quietMs },
    before_capture_qpc_ticks: first?.receipt.capture_qpc_ticks ?? null, after_capture_qpc_ticks: lastSample?.receipt.capture_qpc_ticks ?? null,
    baseline_observation: baseline?.receipt ?? null, final_observation: lastSample?.receipt ?? null, cleanup,
    requested_pad:{direction:a.direction,speed,screen_rotation_deg:a.screen_rotation_deg??0,buttons:0,axes_set_mask:63,axes,duration_ms:duration},
    avatar_movement_measured: false, displacement: null, application_response: null,
    note: 'no_motion means only that selected pixels stayed within tolerance during an observed running hold. Animation, camera motion, a paused scene or ignored input can affect it; no gameplay stuck or displacement measurement is claimed.' });
  return ctx.finish(a.pid, out, a, epoch);
}

export function registerPadNavigation(register: Register, ctx: CameraContext, captureArgs: z.ZodRawShape, regionSchema: z.ZodType<Rect, z.ZodTypeDef, unknown>) {
  register('pad_move_until_stuck', 'Move with the pad until selected pixels stop changing',
    'Hold one explicit left-stick state for a finite duration, stopping its exact native job when a selected client region stays within RGB tolerance. Returns ok:true stopped:no_motion after confirmed release; this measures pixels, not avatar displacement or gameplay stuck. Avoid animated HUD/overlays. Queued holds never produce a motion verdict.',
    { ...captureArgs, pid: z.number().int().positive().max(4294967295), direction:z.enum(MOVE_DIRECTIONS),speed:z.enum(['walk','run']).optional(),screen_rotation_deg:z.number().finite().min(-360).max(360).optional(),region: regionSchema,
      timeout_ms: z.number().int().min(100).max(15000).optional().describe('Finite hold bound, default3000ms.'),
      poll_ms: z.number().int().min(20).max(1000).optional(), grace_ms: z.number().int().min(0).max(5000).optional().describe('Ignore initial stillness for this many ms after observing a running hold, default250.'),
      still_ms: z.number().int().min(100).max(5000).optional().describe('Required quiet capture-time window, default500ms, with at least two comparisons.'),
      max_change_pct: z.number().min(0).max(100).optional().describe('Maximum percentage of coarse RGB cells differing from the quiet-window anchor, default1.'),
      pixel_tolerance: z.number().int().min(0).max(255).optional().describe('Per-channel RGB noise tolerance, default8.') },
    a => padMoveUntilStuck(ctx, a as PadNavigationOptions));
}
