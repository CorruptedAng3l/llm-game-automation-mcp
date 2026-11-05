// A fake vinput CLI for MCP-layer tests (no game, no DLL). The MCP server runs it when VINPUT_EXE points at this file.
//   VINPUT_FAKE_LOG       append one JSON line per call: {argv:[...]}
//   VINPUT_FAKE_SCENARIO  JSON file: { "replies": { "<command>": <json reply> | {"_exit":1,"_stderr":"..","_delay_ms":N,...} },
//                                       "exit_code": N }        (exit code `waitexit` reports once the pid has died)
// Anything not in the scenario gets {"ok":true}. `capture` and `capture-burst` also write a 1x1 PNG to their path argument
// (`capture` writes a synthetic frame instead when the scenario has png:{w,h[,core_capture]}).
// Like the real CLI, a call for a pid that is not running fails with "cannot connect ... process gone".
import { appendFileSync, existsSync, readFileSync, writeFileSync } from "node:fs";

const argv = process.argv.slice(2);
const log = process.env.VINPUT_FAKE_LOG;
// `seq <file>`: record the sequence file text too (the MCP server deletes its temp file after the call)
let seqText;
{ const i = argv.indexOf("seq"); if (i >= 0 && argv[i + 1] && existsSync(argv[i + 1])) seqText = readFileSync(argv[i + 1], "utf8"); }
if (log) appendFileSync(log, JSON.stringify({ argv, t: Date.now(), seqText }) + "\n");
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
    // core output transform (scenario core_capture:true, like a DLL with capture_output_version 1): --scale / --max-width
    // shrink (nearest sample), --format jpeg writes JPEG magic bytes; the reply carries the client rectangle the image covers
    const flag = (f) => { const i = rest.indexOf(f); return i > 0 ? rest[i + 1] : undefined; };
    const core = scen.png.core_capture && (flag("--scale") || flag("--max-width") || flag("--format"));
    let f = 1;
    if (core && flag("--scale")) f = Math.min(1, Number(flag("--scale")));
    if (core && flag("--max-width") && w > Number(flag("--max-width"))) f = Number(flag("--max-width")) / w;
    const ow = Math.max(1, Math.round(w * f)), oh = Math.max(1, Math.round(h * f));
    const data = Buffer.alloc(ow * oh * 4);
    for (let y = 0; y < oh; y++) for (let x = 0; x < ow; x++) { const o = (y * ow + x) * 4, cx = Math.floor(x * w / ow), cy = Math.floor(y * h / oh); data[o] = (ox + cx) & 255; data[o + 1] = (oy + cy) & 255; data[o + 2] = 77; data[o + 3] = 255; }
    const fmt = core && flag("--format") === "jpeg" ? "jpeg" : "png";
    writeFileSync(rest[1], scen.png.garbage ? Buffer.from("this is not a png") : fmt === "jpeg" ? Buffer.from([0xff, 0xd8, 0xff, 0xe0, 0, 0x10]) : encodePng({ width: ow, height: oh, data }));
    const coreFields = core ? { format: fmt, origin_x: ox, origin_y: oy, region_client_w: w, region_client_h: h, source_w: w, source_h: h } : {};
    scen.replies = { ...scen.replies, capture: { ok: true, method: "swapchain", capture_path: "d3d11", w: ow, h: oh, frame: 1, ...coreFields, ...(scen.replies?.capture ?? {}) } };
  } else {
    writeFileSync(rest[1], Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64"));
  }
}
if (cmd === "capture-burst" && rest[1] && (scen.replies?.["capture-burst"]?.ok ?? true) !== false)
  writeFileSync(rest[1], Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64"));
let rep = scen.replies?.[cmd] ?? (cmd === "help" ? { ok: true, text_word_boundary_version: 1 } : { ok: true });
if (cmd === "capture" && rep.ok !== false && rest[1] && rep.path === undefined) rep = { ...rep, path: rest[1] };
if (rep._delay_ms) await sleep(rep._delay_ms);
if (rep._stderr) process.stderr.write(rep._stderr);
const { _exit, _stderr, _delay_ms, ...out } = rep;
say(out, _exit ?? (out.ok === false ? 1 : 0));
