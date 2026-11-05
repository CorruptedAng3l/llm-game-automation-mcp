import { type CliResult, runVinput } from "./vinput.js";

/** A CLI feature: no target query, capture, claim or input. No stale capability cache. */
export async function requireTextBoundary(wholeWord: boolean, exact?: boolean, exactLine=false): Promise<CliResult | undefined> {
  if ((!wholeWord || exact)&&!exactLine) return; // Existing --exact already enforces word boundaries.
  const result = await runVinput(["help"], 2000);
  const value = result.json;
  const record = value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : undefined;
  const version = record?.text_word_boundary_version;
  const lineVersion=record?.text_line_match_version;
  if (result.ok && (!wholeWord||exact||version===1)&&(!exactLine||lineVersion===1)) return;
  if(result.ok&&(!wholeWord||exact||version===1)&&exactLine)return {...result,ok:false,json:{ok:false,input_injected:false,error:'exact_line:true requires an updated vinput CLI with text_line_match_version 1; no click was sent. Update VINPUT_EXE or explicitly use exact_line:false to select text within a longer OCR line.',text_line_capability:{reported_version:typeof lineVersion==='string'?lineVersion.slice(0,64):typeof lineVersion==='number'&&Number.isFinite(lineVersion)?lineVersion:null,cli_ok:result.ok}}};
  return { ...result, ok: false, json: { ok: false, input_injected: false,
    error: "whole_word:true requires an updated vinput CLI with text_word_boundary_version 1; no click was sent. Update VINPUT_EXE, use exact:true for the existing case-sensitive whole-word mode, or explicitly use whole_word:false for legacy substring matching.",
    text_boundary_capability: {
      reported_version: typeof version === "string" ? version.slice(0, 64) : typeof version === "number" && Number.isFinite(version) ? version : null,
      cli_ok: result.ok, cli_exit_code: result.exitCode, timed_out: result.timedOut,
      ...(typeof record?.error === "string" ? { error: record.error.slice(0, 500) } : {}),
    } } };
}
