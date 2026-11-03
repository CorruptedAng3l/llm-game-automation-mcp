// Live checks of the small CLI glue the MCP relies on, against the real vinput.exe and the synthetic target
// (needs build.bat output; launches only dist\vi_uitarget.exe and stops it by PID):
//   status.log_path + counters.fs_refused, `waitexit` (exit code), `launch --cwd`.
// GLUE_NO_LAUNCH=1 skips the checks that launch a process (used to run the rest against an older vinput.exe).
import { spawn, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { makeChecker } from "./lib.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const dist = resolve(here, "..", "..", "dist");
const exe = process.env.VINPUT_EXE ? resolve(process.env.VINPUT_EXE) : join(dist, "vinput.exe");
const { state, check } = makeChecker();
const run = (...a) => {
  const r = spawnSync(exe, ["--json", ...a], { encoding: "utf8", windowsHide: true, timeout: 120000 });
  const line = (r.stdout ?? "").trim().split(/\r?\n/).filter((l) => l.startsWith("{")).pop();
  let json = null; try { json = JSON.parse(line); } catch { /* null */ }
  return { json, code: r.status, out: r.stdout };
};
if (!existsSync(exe)) { console.log(`FAIL ${exe} missing: run build.bat`); process.exit(1); }
const owned = [];
try {
  // waitexit: a short-lived child that exits with code 7
  const child = spawn(process.execPath, ["-e", "setTimeout(()=>process.exit(7),600)"], { stdio: "ignore", windowsHide: true });
  const w = run("--pid", String(child.pid), "waitexit");
  check(w.json?.ok === true && w.json?.exited === true && w.json?.exit_code === 7, "waitexit reports the exit code of a process that ends while it waits", w.out);
  check(w.json?.exit_code_hex === "0x00000007", "waitexit includes exit_code_hex", w.out);
  const gone = run("--pid", "3999996", "waitexit");   // no such pid (a pid whose handle someone still holds stays readable)
  check(gone.json?.ok === false && gone.json?.exited === true, "waitexit on a pid that is already gone: ok:false, exited:true", gone.out);
  const alive = spawn(process.execPath, ["-e", "setTimeout(()=>{},20000)"], { stdio: "ignore", windowsHide: true }); owned.push(alive.pid);
  const to = run("--pid", String(alive.pid), "waitexit", "--timeout", "300");
  check(to.json?.ok === true && to.json?.exited === false, "waitexit --timeout on a running process: exited:false", to.out);
  const help = run("help");
  check(/--cwd/.test(help.json?.help ?? "") && /waitexit/.test(help.json?.help ?? ""), "help documents --cwd and waitexit");

  if (!process.env.GLUE_NO_LAUNCH) {
    const bad = run("launch", "--exe", join(dist, "vi_uitarget.exe"), "--cwd", join(tmpdir(), "no-such-folder-lgam"));
    check(bad.json?.ok === false && /cwd/.test(bad.json?.error ?? "") && bad.code === 2, "launch --cwd on a missing folder is refused before anything starts", bad.out);
    const dir = mkdtempSync(join(tmpdir(), "lgam-cwd-"));
    const L = run("launch", "--exe", join(dist, "vi_uitarget.exe"), "--args", "rel_cwd.log -28000 600", "--cwd", dir, "--settle", "1", "--timeout", "30", "--fg-guard", "0");
    const pid = L.json?.pid; if (pid) owned.push(pid);
    check(L.json?.ok === true && pid > 0, "launch --cwd starts the target", L.out);
    check(existsSync(join(dir, "rel_cwd.log")), "the game's working directory is the --cwd folder (relative log landed there)");
    const noCwd = existsSync(join(dist, "rel_cwd.log"));
    check(!noCwd, "...and not the exe's folder");
    const st = run("--pid", String(pid), "status");
    check(typeof st.json?.log_path === "string" && /vinput_\d+\.log$/.test(st.json.log_path), "status.log_path names the per-pid log file", st.out.slice(0, 300));
    check(st.json?.counters && st.json.counters.fs_refused === 0, "status.counters.fs_refused exists (0 for a well-behaved game)", st.out.slice(-300));
    check(existsSync(st.json?.log_path ?? ""), "the log file named by log_path exists");
    try { rmSync(dir, { recursive: true, force: true }); rmSync(join(dist, "rel_cwd.log"), { force: true }); } catch { /* ignore */ }
  } else {
    check(true, "launch checks skipped (GLUE_NO_LAUNCH)");
  }
} finally {
  for (const p of owned) { try { process.kill(p); console.log(`stopped owned pid ${p}`); } catch { /* gone */ } }
}
console.log(state.failures ? `glue: FAIL (${state.failures})` : `glue: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
