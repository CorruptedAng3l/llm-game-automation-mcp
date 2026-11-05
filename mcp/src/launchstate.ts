type Obj = Record<string, unknown>;
const object = (v: unknown): Obj | undefined => v && typeof v === "object" && !Array.isArray(v) ? v as Obj : undefined;
/** Explain a historical unready snapshot without changing its verdict. */
export function launchReadinessNote(value:unknown):Obj|undefined{
 const j=object(value),inject=object(j?.inject),readiness=object(inject?.readiness)??object(j?.readiness);
 if(!j||j.injected!==true||typeof j.pid!=='number'||!Number.isSafeInteger(j.pid)||j.pid<1||inject&&inject.pid!==j.pid||
   j.target_exited===true||inject?.target_exited===true||!(j.ready===false||j.input_ready===false||readiness?.queue_ready===false))return;
 return {readiness_state:'unconfirmed_snapshot',readiness_note:'The DLL loaded, but the launch-time input-queue readiness snapshot was unconfirmed. The game may still be loading or may use another reader; this snapshot is not a current gameplay failure. Keep this PID and inspect readiness/status/capture before input. No launch or input was retried.'};
}
/** Loaded is a historical native receipt, not a current process-liveness test. */
export function loadedLaunchFailure(value: unknown, claimRequested: boolean, requestedTtl?: number): Obj | undefined {
  const j = object(value), inject = object(j?.inject);
  const pid = j?.pid;
  if (!j || typeof j.ok !== "boolean" || typeof pid !== "number" || !Number.isSafeInteger(pid) || pid < 1 || pid > 4294967295 ||
      j.injected !== true || !inject || inject.pid !== pid || inject.injected !== true || typeof inject.ok !== "boolean" ||
      (j.ready !== undefined && typeof j.ready !== "boolean") || j.launched === false ||
      [j.target_exited, j.exited, inject.target_exited, inject.exited].some(v => v !== undefined && typeof v !== "boolean") ||
      j.target_exited === true || j.exited === true || inject.target_exited === true || inject.exited === true) return;
  const ttl = requestedTtl ?? 3600;
  return { launched: true, keep_pid: true, launch_ok: false, liveness: "not_verified",
    warning: "The native receipt establishes that the DLL loaded in this PID. Keep the PID and inspect readiness; do not start another copy. Current liveness and application readiness are not established by this receipt.",
    ...(claimRequested ? { claim_pending: true, claim_confirmation: "not_observed",
      ...(Number.isInteger(ttl) && ttl >= 1 && ttl <= 86400 ? { claim_ttl_s_requested: ttl } : {}) } : {}) };
}
