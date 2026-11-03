// Thin, explicit wrapper around the vinput CLI. Every call returns the CLI's own JSON reply; nothing is swallowed.
import { spawn } from "node:child_process";
import { existsSync, mkdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export interface CliResult {
  /** true only when vinput replied "ok":true (or a JSON array, for `list`) AND exited with code 0 */
  ok: boolean;
  /** the parsed JSON reply (object or array), or an error object built here when there was none */
  json: unknown;
  exitCode: number | null;
  stderr: string;
  timedOut: boolean;
  argv: string[];
}

const here = dirname(fileURLToPath(import.meta.url));

/** vinput.exe: $VINPUT_EXE, else <repo>/dist/vinput.exe (this file lives in <repo>/mcp/build). */
export function resolveVinputExe(): string {
  const env = process.env.VINPUT_EXE;
  if (env && env.trim()) return resolve(env.trim());
  return resolve(here, "..", "..", "dist", "vinput.exe");
}

/** Folder for captures when the caller gives no path: $VINPUT_CAPTURE_DIR, else <repo>/captures. */
export function captureDir(): string {
  const env = process.env.VINPUT_CAPTURE_DIR;
  const dir = env && env.trim() ? resolve(env.trim()) : resolve(dirname(resolveVinputExe()), "..", "captures");
  if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
  return dir;
}

export function defaultCapturePath(pid: number): string {
  const stamp = new Date().toISOString().replace(/[-:]/g, "").replace(/\..*$/, "").replace("T", "_");
  return join(captureDir(), `pid${pid}_${stamp}_${Math.floor(Math.random() * 1e6)}.png`);
}

/** Last stdout line that parses as JSON (vinput prints exactly one JSON reply line; progress goes to stderr). */
export function lastJson(stdout: string): unknown {
  const lines = stdout.split(/\r?\n/).map((l) => l.trim()).filter((l) => l.startsWith("{") || l.startsWith("["));
  for (let i = lines.length - 1; i >= 0; i--) {
    try { return JSON.parse(lines[i]); } catch { /* try the previous line */ }
  }
  return undefined;
}

/** VINPUT_EXE may be a .js / .mjs script (the tests use a fake vinput); it is run with this Node. */
export function spawnSpec(exe: string, argv: string[]): [string, string[]] {
  return /\.(m|c)?js$/i.test(exe) ? [process.execPath, [exe, ...argv]] : [exe, argv];
}

export async function runVinput(args: string[], timeoutMs: number): Promise<CliResult> {
  const exe = resolveVinputExe();
  const argv = ["--json", ...args];
  if (!existsSync(exe)) {
    return { ok: false, json: { ok: false, error: `vinput.exe not found at ${exe}. Build it (build.bat) or set VINPUT_EXE.` }, exitCode: null, stderr: "", timedOut: false, argv };
  }
  return await new Promise<CliResult>((resolvePromise) => {
    const [cmd, cmdArgs] = spawnSpec(exe, argv);
    const child = spawn(cmd, cmdArgs, { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "", stderr = "", timedOut = false, settled = false;
    child.stdout.setEncoding("utf8"); child.stderr.setEncoding("utf8");
    child.stdout.on("data", (d: string) => { stdout += d; });
    child.stderr.on("data", (d: string) => { if (stderr.length < 64 * 1024) stderr += d; });
    const timer = setTimeout(() => { timedOut = true; child.kill(); }, timeoutMs);
    const finish = (exitCode: number | null, spawnError?: Error) => {
      if (settled) return; settled = true; clearTimeout(timer);
      let json = lastJson(stdout);
      let ok = false;
      if (spawnError) json = { ok: false, error: `cannot start vinput.exe: ${spawnError.message}` };
      else if (timedOut) json = { ok: false, error: `vinput did not answer within ${timeoutMs} ms (process killed)`, partial_reply: json ?? null };
      else if (json === undefined) json = { ok: false, error: `vinput exited with code ${exitCode} and printed no JSON`, stdout: stdout.slice(-2000) };
      else if (Array.isArray(json)) ok = exitCode === 0;
      else ok = (json as { ok?: unknown }).ok === true && exitCode === 0;
      resolvePromise({ ok, json, exitCode, stderr: stderr.trim(), timedOut, argv });
    };
    child.on("error", (e) => finish(null, e));
    child.on("close", (code) => finish(code));
  });
}
