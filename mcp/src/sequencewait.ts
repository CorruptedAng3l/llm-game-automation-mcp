// Sequence-only observer. Native IDs are PID:unscaled-first-job-epoch:serial.
// This is cooperative protocol identity, not OS process-creation proof.
import { type CliResult, runVinput } from "./vinput.js";

export type SequenceWaitContext = { pid: number; owner: string; prefix: string[]; epoch: number; currentEpoch: () => number; signal?: AbortSignal };
export type Job = Record<string, unknown> & { job_id: string; job_state: string; job_complete: boolean; requested_ms: number };
const object = (value: unknown): Record<string, unknown> | undefined => value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : undefined;
const delay = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));
const terminal = new Set(["completed", "cancelled", "failed"]);
const positiveQpc=(value:unknown):value is string=>typeof value==='string'&&/^[1-9][0-9]{0,19}$/.test(value)&&BigInt(value)<=18446744073709551615n;
function captureScopeKey(value:unknown):string|undefined{
 const s=object(value),nonnegative=(v:unknown)=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=18446744073709551615n;
 if(!s||typeof s.hwnd!=='string'||!/^0x[0-9a-f]+$/i.test(s.hwnd)||BigInt(s.hwnd)===0n||BigInt(s.hwnd)>18446744073709551615n||!Number.isSafeInteger(s.thread)||Number(s.thread)<1||Number(s.thread)>4294967295||!positiveQpc(s.adoption_generation)||!nonnegative(s.geometry_epoch)||!positiveQpc(s.lifetime_cookie)||!Number.isInteger(s.client_w)||Number(s.client_w)<1||Number(s.client_w)>32768||!Number.isInteger(s.client_h)||Number(s.client_h)<1||Number(s.client_h)>32768||typeof s.geometry_witness!=='string')return;
 return JSON.stringify([s.hwnd.toLowerCase(),s.thread,s.adoption_generation,s.geometry_epoch,s.lifetime_cookie,s.client_w,s.client_h,s.geometry_witness]);
}
/** A final image's native timestamps, separate from input completion and from
 * rendered content/game acceptance. The caller retains the original receipt. */
export function sequenceCaptureEvidence(completed:unknown,capture:unknown):Record<string,unknown>|undefined{
 const job=object(completed),frame=object(capture);
 if(job?.job_kind!=='sequence'||job.job_state!=='completed'||job.job_complete!==true)return;
 const end=job.finished_qpc_ticks,frequency=job.qpc_frequency;
 const endScope=captureScopeKey(job.finished_capture_scope),sameScope=endScope?captureScopeKey(frame?.capture_scope)===endScope:null;
 const base={version:1,job_id:job.job_id??null,job_end_qpc_ticks:positiveQpc(end)?end:null,qpc_frequency:positiveQpc(frequency)?frequency:null,finished_capture_scope:job.finished_capture_scope??null,final_capture_same_scope:sameScope,
  capture_qpc_ticks:positiveQpc(frame?.capture_qpc_ticks)?frame.capture_qpc_ticks:null,
  frame_qpc_ticks:frame?.method!=='printwindow'&&positiveQpc(frame?.frame_qpc_ticks)?frame.frame_qpc_ticks:null,
  game_consumption_measured:false,rendered_content_freshness_measured:false,
  note:'Capture/request and native frame checkpoints only. PrintWindow has no rendered-frame timestamp; timestamps do not prove game consumption, display scanout or fresh visual content.'};
 const ordered=base.frame_qpc_ticks&&base.capture_qpc_ticks?BigInt(base.frame_qpc_ticks)<=BigInt(base.capture_qpc_ticks):null;
 if(ordered===false)return {...base,ok:false,completion_clock_available:positiveQpc(end)&&positiveQpc(frequency),capture_after_job_end:null,rendered_frame_after_job_end:null,frame_timestamp_order_valid:false,error:'native frame QPC is later than its capture QPC; this unconfirmed image cannot witness completion and input was not replayed'};
 if(sameScope===false)return {...base,ok:false,completion_clock_available:positiveQpc(end)&&positiveQpc(frequency),capture_after_job_end:null,rendered_frame_after_job_end:null,error:'final capture scope differs from the exact completed sequence target; input was not replayed'};
 if(!positiveQpc(end)||!positiveQpc(frequency))return {...base,ok:true,completion_clock_available:false,capture_after_job_end:null,rendered_frame_after_job_end:null,warning:'Completed input retained; this native receipt cannot bind the final image to a job-end QPC.'};
 if(frame?.timing_clock!=='QueryPerformanceCounter'||frame.qpc_frequency!==frequency||!positiveQpc(frame.capture_qpc_ticks))return {...base,ok:false,completion_clock_available:true,capture_after_job_end:null,rendered_frame_after_job_end:null,error:'final capture clock is unavailable or differs from the completed sequence; input was not replayed'};
 const captureAfter=BigInt(frame.capture_qpc_ticks)>=BigInt(end),renderAfter=base.frame_qpc_ticks===null?null:BigInt(base.frame_qpc_ticks)>=BigInt(end);
 return {...base,ok:captureAfter&&renderAfter!==false,completion_clock_available:true,capture_after_job_end:captureAfter,rendered_frame_after_job_end:renderAfter,
  ...(!captureAfter||renderAfter===false?{error:'final capture scope or timestamp does not match the completed sequence; stale/other-scope image is not a post-completion witness and input was not replayed'}:{})};
}
function identity(id: unknown, pid: number): id is string {
  if (typeof id !== "string") return false;
  const m = /^([1-9][0-9]{0,9}):([1-9][0-9]{0,19}):([1-9][0-9]{0,19})$/.exec(id);
  return !!m && BigInt(m[1]) === BigInt(pid) && BigInt(m[1]) <= 4294967295n &&
    BigInt(m[2]) <= 18446744073709551615n && BigInt(m[3]) <= 18446744073709551615n;
}
export function sequenceJob(value: unknown, ctx: SequenceWaitContext, accepted: boolean, id?: string): Job | undefined {
  const j = object(value);
  if (!j || !identity(j.job_id, ctx.pid) || (id !== undefined && j.job_id !== id) || j.owner !== ctx.owner || j.job_kind !== "sequence" ||
      j.accepted !== accepted || typeof j.job_complete !== "boolean" || typeof j.job_state !== "string" ||
      !["queued", "running", ...terminal].includes(j.job_state) || j.job_complete !== terminal.has(j.job_state) ||
      j.ok !== (accepted || j.job_state === "completed") || typeof j.requested_ms !== "number" || !Number.isFinite(j.requested_ms) || j.requested_ms < 1 || j.requested_ms > 120000)
    return undefined;
  return j as Job;
}
function refused(error: string): CliResult {
  return { ok: false, json: { ok: false, input_injected: false, sequence_wait_support_checked: true, error },
    exitCode: null, stderr: "", timedOut: false, argv: [] };
}
export async function requireSequenceWait(ctx: SequenceWaitContext): Promise<CliResult | undefined> {
  const status = await runVinput([...ctx.prefix, "status"], 2000), s = object(status.json);
  if (ctx.currentEpoch() !== ctx.epoch) return refused("attachment changed before sequence scheduling; no input was sent");
  if (!status.ok || s?.pid !== ctx.pid || !Number.isInteger(s?.key_sequence_version) || (s?.key_sequence_version as number) < 1)
    return refused("wait:true requires the matching target's typed key_sequence_version >=1; no sequence was sent");
  const jobs = await runVinput([...ctx.prefix, "jobs"], 2000), j = object(jobs.json);
  if (ctx.currentEpoch() !== ctx.epoch) return refused("attachment changed before sequence scheduling; no input was sent");
  if (!jobs.ok || !Array.isArray(j?.jobs)) return refused("target job observation is unavailable; no sequence was sent");
  return undefined;
}
export function sequenceWaitError(initial: CliResult, outcome: string, error: string, elapsed: number, timeout?: number, last?: Job): CliResult {
  const input = object(initial.json) ?? {}, completed = outcome === "completed";
  return { ...initial, ok: completed, json: { ...input, ...(last ?? {}), ok: completed,
    sequence_input: initial.json, sequence_wait: { requested: true, outcome, completed, elapsed_ms: Math.round(elapsed),
      ...(timeout !== undefined ? { timeout_ms: timeout } : {}), job_id: input.job_id ?? null,
      job_may_still_be_active: last ? !last.job_complete : input.input_injected !== false, game_consumption_measured: false,
      scope: "native scheduler observation only; application consumption is not measured; input was not replayed" },
    ...(completed ? {} : { error }) } };
}
export async function waitSequence(initial: CliResult, ctx: SequenceWaitContext, explicitTimeout?: number): Promise<CliResult> {
  const start = performance.now(), admitted = sequenceJob(initial.json, ctx, true);
  if (!admitted) return sequenceWaitError(initial, "invalid_ack", "sequence may have started, but its generation-bound acknowledgement is invalid; no input was retried", 0, explicitTimeout);
  const frameWait=typeof admitted.present_anchor_timeout_ms==='number'&&Number.isInteger(admitted.present_anchor_timeout_ms)&&admitted.present_anchor_timeout_ms>=100&&admitted.present_anchor_timeout_ms<=60000?admitted.present_anchor_timeout_ms:0;
  const timeout = explicitTimeout ?? Math.min(120000, Math.ceil(admitted.requested_ms) + frameWait + 5000), deadline = start + timeout;
  let last = admitted;
  for (;;) {
    if (ctx.currentEpoch() !== ctx.epoch) return sequenceWaitError(initial, "attachment_changed", "attachment changed while waiting; the sequence was not cancelled or replayed", performance.now() - start, timeout, last);
    const remaining = deadline - performance.now();
    if (remaining <= 0) return sequenceWaitError(initial, "timeout", "sequence completion was not observed before the wait deadline; the job was not cancelled", performance.now() - start, timeout, last);
    const status = await runVinput([...ctx.prefix, "job-status", admitted.job_id], Math.max(1, Math.min(2000, Math.ceil(remaining))));
    if (ctx.currentEpoch() !== ctx.epoch) return sequenceWaitError(initial, "attachment_changed", "attachment changed while waiting; the sequence was not cancelled or replayed", performance.now() - start, timeout, last);
    if (performance.now() > deadline) return sequenceWaitError(initial, "timeout", "sequence completion was not observed within the wait deadline; the job was not cancelled", performance.now() - start, timeout, last);
    const outer = object(status.json);
    if (!status.ok) return sequenceWaitError(initial, "status_unavailable",
      `sequence status unavailable; no input was retried: ${String(outer?.error ?? "transport failure").slice(0, 1000)}`, performance.now() - start, timeout, last);
    const observed = sequenceJob(outer?.job, ctx, false, admitted.job_id);
    if (!observed) return sequenceWaitError(initial, "invalid_status", "status did not identify the exact admitted sequence generation/owner or had inconsistent state; no input was retried", performance.now() - start, timeout, last);
    last = observed;
    if (last.job_complete) {
      const outcome = last.job_state === "cancelled" && last.job_reason === "explicit_replacement" ? "replaced" : last.job_state;
      return sequenceWaitError(initial, outcome, `sequence ${outcome}: ${String(last.job_reason ?? "").slice(0, 1000)}`, performance.now() - start, timeout, last);
    }
    await delay(Math.max(0, Math.min(50, deadline - performance.now())));
  }
}
