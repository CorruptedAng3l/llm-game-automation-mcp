// A fake vinput CLI for MCP-layer tests (no game, no DLL). The MCP server runs it when VINPUT_EXE points at this file.
//   VINPUT_FAKE_LOG       append one JSON line per call: {argv:[...]}
//   VINPUT_FAKE_SCENARIO  JSON file: { "replies": { "<command>": <json reply> | {"_exit":1,"_stderr":"..","_delay_ms":N,...} },
//                                       "exit_code": N }        (exit code `waitexit` reports once the pid has died)
// Anything not in the scenario gets {"ok":true}. `capture` also writes a 1x1 PNG to its path argument.
// Like the real CLI, a call for a pid that is not running fails with "cannot connect ... process gone".
import { appendFileSync, existsSync, readFileSync, writeFileSync } from "node:fs";

const argv = process.argv.slice(2);
const log = process.env.VINPUT_FAKE_LOG;
if (log) appendFileSync(log, JSON.stringify({ argv, t: Date.now() }) + "\n");
const scen = process.env.VINPUT_FAKE_SCENARIO && existsSync(process.env.VINPUT_FAKE_SCENARIO)
  ? JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO, "utf8")) : {};

// strip global flags
const rest = []; let pid = 0;
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === "--json" || argv[i] === "--force-claim") continue;
  if (argv[i] === "--owner") { i++; continue; }
  if (argv[i] === "--pid") { pid = Number(argv[++i]); continue; }
  rest.push(argv[i]);
}
const cmd = rest[0];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const alive = (p) => { try { process.kill(p, 0); return true; } catch (e) { return e.code === "EPERM"; } };
const say = (obj, code = 0) => { process.stdout.write(JSON.stringify(obj) + "\n"); process.exit(code); };

if (cmd === "waitexit") {
  if (!alive(pid)) say({ ok: false, pid, exited: true, error: "no such process" }, 1);
  while (alive(pid)) await sleep(40);
  say({ ok: true, pid, exited: true, exit_code: scen.exit_code ?? 0 });
}
if (pid && cmd !== "launch" && cmd !== "inject" && !alive(pid))
  say({ ok: false, error: `cannot connect to \\\\.\\pipe\\vinput_${pid} (not injected / process gone)` }, 1);

if (cmd === "capture" && rest[1]) {
  if (scen.png) {
    // synthetic frame: pixel (cx,cy) in CLIENT coordinates = rgb(cx&255, cy&255, 77); honours --region like vinput
    const { encodePng } = await import("../build/png.js");
    let [ox, oy, w, h] = [0, 0, scen.png.w, scen.png.h];
    const ri = rest.indexOf("--region");
    if (ri > 0) [ox, oy, w, h] = rest[ri + 1].split(",").map(Number);
    const data = Buffer.alloc(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const o = (y * w + x) * 4; data[o] = (ox + x) & 255; data[o + 1] = (oy + y) & 255; data[o + 2] = 77; data[o + 3] = 255; }
    writeFileSync(rest[1], scen.png.garbage ? Buffer.from("this is not a png") : encodePng({ width: w, height: h, data }));
    scen.replies = { ...scen.replies, capture: { ok: true, method: "swapchain", capture_path: "d3d11", w, h, frame: 1, ...(scen.replies?.capture ?? {}) } };
  } else {
    writeFileSync(rest[1], Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64"));
  }
}
let rep = scen.replies?.[cmd] ?? { ok: true };
if (cmd === "capture" && rep.ok !== false && rest[1] && rep.path === undefined) rep = { ...rep, path: rest[1] };
if (rep._delay_ms) await sleep(rep._delay_ms);
if (rep._stderr) process.stderr.write(rep._stderr);
const { _exit, _stderr, _delay_ms, ...out } = rep;
say(out, _exit ?? (out.ok === false ? 1 : 0));
