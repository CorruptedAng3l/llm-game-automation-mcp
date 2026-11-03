// Fix 4b: once the game has exited, every tool says `target exited (pid N, exit code X)` instead of a pipe error.
import { spawn } from "node:child_process";
import { makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// a stand-in "game": lives ~2 s, then exits with code 7
const game = spawn(process.execPath, ["-e", "setTimeout(()=>process.exit(7),2000)"], { stdio: "ignore", windowsHide: true });
const gone = spawn(process.execPath, ["-e", "0"], { stdio: "ignore", windowsHide: true });
await new Promise((r) => gone.on("exit", r));   // a pid that is dead and was never launched by this server
const t = await startFake({ exit_code: 7, replies: { launch: { ok: true, pid: game.pid, injected: true } } });
try {
  const L = await t.call("launch", { exe: "C:/g/game.exe" });
  check(!L.isError && L.json?.pid === game.pid, "launch returns the pid", L.text);
  const live = await t.call("status", { pid: game.pid });
  check(!live.isError, "while the game runs, status works", live.text.slice(0, 200));
  // a real, non-exit failure on a live process is passed through untouched
  t.setScenario({ exit_code: 7, replies: { click: { ok: false, error: "hover timed out" } } });
  const bad = await t.call("click", { pid: game.pid, x: 1, y: 2 });
  check(bad.isError && /hover timed out/.test(bad.text) && !/target exited/.test(bad.text), "an ordinary failure of a running game is not mislabelled", bad.text.slice(0, 200));

  await new Promise((r) => game.on("exit", r));
  await sleep(300);
  for (const [tool, args] of [["status", {}], ["capture", {}], ["key", { key: "enter" }], ["click", { x: 1, y: 2 }], ["click_text", { text: "Play" }], ["wait_text", { text: "x" }], ["pad", { buttons: "a" }], ["release", {}], ["log", {}]]) {
    const r = await t.call(tool, { pid: game.pid, ...args });
    check(r.isError === true && /target exited \(pid \d+, exit code 7\)/.test(r.text) && r.json?.ok === false && r.json?.exit_code === 7,
      `${tool}: "target exited (pid ${game.pid}, exit code 7)"`, r.text.slice(0, 250));
    check(!/cannot connect|pipe/.test(r.text), `${tool}: no raw pipe error`, r.text.slice(0, 250));
  }
  t.clearCalls();
  await t.call("status", { pid: game.pid });
  check(t.calls().filter((c) => c.includes("status")).length === 0, "once known, a dead target is answered without spawning vinput");

  const u = await t.call("status", { pid: gone.pid });
  check(u.isError && u.text.includes(`target exited (pid ${gone.pid}, exit code unknown)`), "a dead pid never launched here: exit code unknown", u.text.slice(0, 250));

  // relaunching the same pid number clears the record
  t.setScenario({ replies: { launch: { ok: true, pid: process.pid } } });
  const alive = await t.call("status", { pid: process.pid });
  check(!alive.isError, "a live pid is unaffected", alive.text.slice(0, 200));
} finally { await t.close(); try { game.kill(); } catch { /* gone */ } }
console.log(state.failures ? `exit: FAIL (${state.failures})` : `exit: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
