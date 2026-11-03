// Live end-to-end smoke test THROUGH MCP against the synthetic menu target (test/vi_uitarget.cpp, built by build.bat).
// launch -> capture (image) -> wait_text -> ocr -> find_text -> key -> click_text -> wait_text -> type_text -> capture ->
// release -> detach, then the anti-cheat refusal through attach. The target logs every key/click, so each action is
// checked in the target's own log, not only in vinput's reply. Processes started here are stopped by PID.
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, "..", "..");
const dist = resolve(repo, "dist");
const out = resolve(repo, "build", "test", "mcp_smoke");
mkdirSync(out, { recursive: true });
const log = resolve(out, "uitarget.log"); rmSync(log, { force: true });
for (const f of ["vinput.exe", "vinput64.dll", "vi_uitarget.exe", "vi_actarget.exe", "acdummy/x64/EasyAntiCheat_x64.dll"])
  if (!existsSync(resolve(dist, f))) { console.log(`FAIL missing dist/${f}: run build.bat first`); process.exit(1); }

const transport = new StdioClientTransport({
  command: process.execPath, args: [resolve(here, "..", "build", "index.js")], stderr: "pipe",
  env: { ...process.env, VINPUT_EXE: resolve(dist, "vinput.exe"), VINPUT_CAPTURE_DIR: out, VINPUT_OWNER: "mcp-smoke" },
});
let serverErr = ""; transport.stderr?.on("data", (d) => { serverErr += d; });
const client = new Client({ name: "smoke-test", version: "1.0.0" });
let failures = 0; const owned = [];
const check = (cond, what, detail = "") => { console.log(`${cond ? "PASS" : "FAIL"} ${what}${!cond && detail ? " :: " + detail : ""}`); if (!cond) failures++; return cond; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function call(name, args) {
  const t0 = Date.now();
  const r = await client.callTool({ name, arguments: args }, undefined, { timeout: 600000 });
  const text = r.content.find((c) => c.type === "text")?.text ?? "";
  let json = null; try { json = JSON.parse(text); } catch { /* reported below */ }
  const image = r.content.find((c) => c.type === "image");
  console.log(`  ${name} ${JSON.stringify(args).slice(0, 120)} -> isError=${!!r.isError} ${Date.now() - t0} ms ${text.slice(0, 200)}`);
  return { r, json, image, text };
}
const targetLog = () => (existsSync(log) ? readFileSync(log, "utf8") : "");

try {
  await client.connect(transport);
  // 1. launch the synthetic menu target through MCP
  const L = await call("launch", { exe: resolve(dist, "vi_uitarget.exe"), args: `"${log}" -28000 600 --load-ms 300`,
    settle_s: 1, timeout_s: 30, lock: resolve(out, "launch.lock"), claim: true });
  const pid = L.json?.pid;
  if (pid) owned.push(pid);
  check(!L.r.isError && L.json?.ok === true && L.json?.injected === true && pid > 0, "launch: injected, pid returned", L.text);
  check(L.json?.foreground_restored === true, "launch: the real foreground was handed back", L.text);
  if (!pid) throw new Error("no pid");

  // 2. look
  const C1 = await call("capture", { pid, after_frames: 3 });
  check(!C1.r.isError && C1.json?.w === 800 && C1.json?.h === 450, "capture: 800x450 frame", C1.text);
  check(C1.image?.mimeType === "image/png" && C1.image.data.startsWith("iVBORw0KGgo"), "capture: PNG returned as MCP image content");
  check(existsSync(C1.json?.path ?? ""), "capture: PNG saved at the reported path");
  const W1 = await call("wait_text", { pid, text: "PLAY", timeout_ms: 10000 });
  check(!W1.r.isError && W1.json?.ok === true, "wait_text PLAY: main menu is up", W1.text);
  const O = await call("ocr", { pid });
  check(!O.r.isError && Array.isArray(O.json?.lines) && O.json.lines.some((l) => /PLAY/i.test(l.text)), "ocr: lines include PLAY", O.text.slice(0, 300));
  const F = await call("find_text", { pid, text: "OPTIONS" });
  check(!F.r.isError && F.json?.ok === true && F.json?.dry_run !== false, "find_text OPTIONS: found without clicking", F.text);

  // 3. act + verify in the target's own log
  const K = await call("key", { pid, key: "a" });
  await sleep(300);
  check(!K.r.isError && /CHAR U\+0061/i.test(targetLog()), "key a: target received WM_CHAR 'a'", K.text);
  const CT = await call("click_text", { pid, text: "PLAY" });
  check(!CT.r.isError && CT.json?.ok === true, "click_text PLAY: clicked", CT.text);
  const W2 = await call("wait_text", { pid, text: "Select a game type", timeout_ms: 8000 });
  check(!W2.r.isError && /CLICK \d+ \d+ PLAY/.test(targetLog()), "wait_text: SELECT screen reached, target logged a click on PLAY", W2.text);
  const T = await call("type_text", { pid, text: "hi" });
  await sleep(300);
  check(!T.r.isError && /CHAR U\+0068/i.test(targetLog()) && /CHAR U\+0069/i.test(targetLog()), "type_text hi: target received 'h' and 'i'", T.text);
  const C2 = await call("capture", { pid, include_image: false, region: { x: 0, y: 0, w: 400, h: 200 } });
  check(!C2.r.isError && C2.json?.w === 400 && !C2.image, "capture region without image", C2.text);
  const S = await call("status", { pid });
  check(!S.r.isError && S.json?.really_foreground === false, "status: the game stayed in the background", S.text.slice(0, 300));
  check(S.json?.client?.w === 800 && S.json?.client?.h === 450 && Array.isArray(S.json?.warnings) && S.text.length < 1500, "status: compact reply with client size and warnings[]", S.text.slice(0, 400));
  const SV = await call("status", { pid, verbose: true });
  check(!SV.r.isError && SV.json?.counters && SV.json?.frames_by_api, "status verbose:true: the full reply", SV.text.slice(0, 200));

  // 3b. the diagnostics and precision tools against the real vinput
  const CF = await call("config", { pid });
  check(!CF.r.isError && CF.json?.config?.cursor && typeof CF.json.config.fakefocus === "boolean", "config: effective config with names", CF.text.slice(0, 300));
  const CF2 = await call("config", { pid, raw: "post", cursor: "virtual" });
  check(!CF2.r.isError && CF2.json?.config?.raw === "post" && CF2.json?.config?.cursor === "virtual" && !CF2.json?.warnings, "config: raw=post cursor=virtual took effect", CF2.text.slice(0, 300));
  await call("config", { pid, raw: "auto", cursor: "auto" });
  const LG = await call("log", { pid, lines: 5 });
  check(!LG.r.isError && Array.isArray(LG.json?.lines) && LG.json.lines.length > 0, "log: lines from the game's ring", LG.text.slice(0, 300));
  const LF = await call("log", { pid, lines: 3, source: "file" });
  check(!LF.r.isError && LF.json?.source === "file" && LF.json.lines.length > 0, "log source:file: lines from the log file on disk", LF.text.slice(0, 300));
  const TR = await call("trace", { pid, key: "a" });
  check(!TR.r.isError && TR.json?.ok === true && TR.json?.name, "trace key a", TR.text.slice(0, 200));
  const CG = await call("capture", { pid, grid: 100, max_width: 400 });
  check(!CG.r.isError && !!CG.image && CG.json?.image?.scale === 0.5 && CG.json?.w === 800, "capture grid + max_width: image content, mapping scale 0.5", CG.text.slice(0, 300));
  const CM = await call("click", { pid, x: 200, y: 100, coord_space: "capture" });
  check(!CM.r.isError && CM.json?.mapped?.x === 400 && CM.json?.mapped?.y === 200, "click coord_space capture: image (200,100) -> client (400,200)", CM.text.slice(0, 300));
  const CA = await call("click", { pid, x: 4, y: 4, hold_ms: 150, capture_after: 2, park_cursor: true });
  check(!CA.r.isError && !!CA.image && CA.json?.capture_after?.w === 800 && /edge/.test((CA.json?.warnings ?? []).join(" ")) && CA.json?.parked, "click: capture_after image, edge warning, parked cursor", CA.text.slice(0, 400));
  const DR = await call("drag", { pid, x1: 100, y1: 300, x2: 160, y2: 300, ms: 200, hold_start_ms: 100, hold_end_ms: 100 });
  check(!DR.r.isError && DR.json?.ok === true, "drag: press, hold, stepped move, dwell, release", DR.text.slice(0, 300));

  // 4. explicit errors
  const E1 = await call("click_text", { pid, text: "NO SUCH BUTTON", timeout_ms: 500 });
  check(E1.r.isError === true && E1.json?.ok === false, "click_text missing text: isError + ok:false");
  const E2 = await call("status", { pid: 999999 });
  check(E2.r.isError === true && E2.json?.ok === false, "status on a pid without vinput: isError + ok:false", E2.text);

  // 5. release / detach
  const R = await call("release", { pid });
  check(!R.r.isError, "release", R.text);
  const D = await call("detach", { pid });
  check(!D.r.isError && D.json?.unclaim?.ok === true, "detach: input released and claim dropped", D.text);

  // 6. anti-cheat refusal through MCP: a target holding a module NAMED like EasyAntiCheat (an empty dummy DLL)
  const acLog = resolve(out, "actarget.log");
  const ac = spawn(resolve(dist, "vi_actarget.exe"), [acLog, "--load", resolve(dist, "acdummy", "x64", "EasyAntiCheat_x64.dll")], { windowsHide: true, stdio: "ignore" });
  owned.push(ac.pid);
  for (let i = 0; i < 50 && !(existsSync(acLog) && readFileSync(acLog, "utf8").includes("WINDOW ready")); i++) await sleep(100);
  const A = await call("attach", { pid: ac.pid });
  check(A.r.isError === true && A.json?.anticheat_refused === true && A.json?.anticheat?.product === "EasyAntiCheat", "attach to an anti-cheat target: refused through MCP", A.text);
} catch (e) {
  failures++; console.log(`FAIL exception: ${e.stack ?? e}`);
} finally {
  await client.close().catch(() => {});
  for (const p of owned) { try { process.kill(p); console.log(`stopped owned pid ${p}`); } catch { /* already gone */ } }
}
if (failures) console.log(`server stderr:\n${serverErr}`);
console.log(failures ? `smoke: FAIL (${failures})` : "smoke: PASS");
process.exit(failures ? 1 : 0);
