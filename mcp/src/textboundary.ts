import { type CliResult, runVinput } from "./vinput.js";

/** A CLI feature: no target query, capture, claim or input. No stale capability cache. */
export async function requireTextBoundary(wholeWord: boolean, exact?: boolean): Promise<CliResult | undefined> {
  if (!wholeWord || exact) return; // Existing --exact already enforces word boundaries.
  const result = await runVinput(["help"], 2000);
  const value = result.json;
  const record = value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : undefined;
  const version = record?.text_word_boundary_version;
  if (result.ok && version === 1) return;
  return { ...result, ok: false, json: { ok: false, input_injected: false,
    error: "whole_word:true requires an updated vinput CLI with text_word_boundary_version 1; no click was sent. Update VINPUT_EXE, use exact:true for the existing case-sensitive whole-word mode, or explicitly use whole_word:false for legacy substring matching.",
    text_boundary_capability: {
      reported_version: typeof version === "string" ? version.slice(0, 64) : typeof version === "number" && Number.isFinite(version) ? version : null,
      cli_ok: result.ok, cli_exit_code: result.exitCode, timed_out: result.timedOut,
      ...(typeof record?.error === "string" ? { error: record.error.slice(0, 500) } : {}),
    } } };
}
