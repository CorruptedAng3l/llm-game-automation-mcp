// Tracks whether a target game has exited, and its exit code, so tools can say "target exited (pid N, exit code X)"
// instead of a raw pipe error.
import { spawn, type ChildProcess } from "node:child_process";
import { existsSync } from "node:fs";
import { lastJson, resolveVinputExe, spawnSpec } from "./vinput.js";

// A pipe error cannot tell "the game is gone" from "vinput is not loaded", and the exit code is unreadable once the
// process has no handles left. So each target gets one `vinput waitexit` helper that holds a handle and reports the
// exit code when the game ends.
interface Watcher { child: ChildProcess; done: Promise<void> }
const watchers = new Map<number, Watcher>();
const exited = new Map<number, { code: number | null }>();

/** Start watching a target (idempotent). Older vinput builds without `waitexit` simply never report a code. */
export function watchTarget(pid: number): void {
  if (watchers.has(pid) || exited.has(pid)) return;
  const exe = resolveVinputExe();
  if (!existsSync(exe)) return;
  const [cmd, args] = spawnSpec(exe, ["--json", "--pid", String(pid), "waitexit"]);
  let child: ChildProcess;
  try { child = spawn(cmd, args, { windowsHide: true, stdio: ["ignore", "pipe", "ignore"] }); } catch { return; }
  let out = "";
  child.stdout?.setEncoding("utf8");
  child.stdout?.on("data", (d: string) => { if (out.length < 4096) out += d; });
  const done = new Promise<void>((res) => {
    const end = () => {
      watchers.delete(pid);
      const j = lastJson(out) as { exited?: boolean; exit_code?: number } | undefined;
      if (j && j.exited === true) exited.set(pid, { code: typeof j.exit_code === "number" ? j.exit_code : null });
      res();
    };
    child.on("error", end); child.on("close", end);
  });
  watchers.set(pid, { child, done });
}
/** Forget a recorded exit (the pid was launched / attached again). */
export function forgetExit(pid: number): void { exited.delete(pid); }
process.on("exit", () => { for (const w of watchers.values()) { try { w.child.kill(); } catch { /* gone */ } } });

function pidAlive(pid: number): boolean {
  try { process.kill(pid, 0); return true; } catch (e) { return (e as NodeJS.ErrnoException).code === "EPERM"; }
}

/** The recorded exit of a target, if it has ended (code null = ended but code unknown), else undefined. */
export function knownExit(pid: number): { code: number | null } | undefined { return exited.get(pid); }

/** After a failed call: has the target process ended? Waits briefly for the watcher so the exit code is included. */
export async function confirmExit(pid: number): Promise<{ code: number | null } | undefined> {
  const w = watchers.get(pid);
  if (!pidAlive(pid)) {
    if (w) await Promise.race([w.done, new Promise((r) => setTimeout(r, 2000))]);
    return exited.get(pid) ?? { code: null };
  }
  return exited.get(pid);
}

export function exitMessage(pid: number, code: number | null): string {
  if (code === null) return `target exited (pid ${pid}, exit code unknown)`;
  const hex = code > 0xffff ? ` (0x${(code >>> 0).toString(16).toUpperCase().padStart(8, "0")})` : "";
  return `target exited (pid ${pid}, exit code ${code}${hex})`;
}

