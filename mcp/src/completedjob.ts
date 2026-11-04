// MCP presentation only. CliResult/native JSON remain unchanged.
export function isCompletedJobOutcome(value: unknown): value is Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const job = value as Record<string, unknown>;
  return job.ok === true && typeof job.job_id === "string" && job.job_id.length > 0 &&
    job.job_state === "completed" && job.accepted === false;
}

/** This response is a completed outcome, rather than a pending queue-admission ACK.
 *  It does not identify the original request mode or prove application consumption. */
export function completedJobView(value: unknown, transportOk: boolean): unknown {
  if (!transportOk || !isCompletedJobOutcome(value)) return value;
  const view = { ...value };
  delete view.accepted;
  return view;
}
