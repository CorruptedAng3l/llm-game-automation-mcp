// launch warns when the window's client size differs from the size arguments it was given (the game ignored them).
import { makeChecker, startFake } from "./lib.mjs";
import { requestedSize } from "../build/launchsize.js";
const { state, check } = makeChecker();
const st = (w, h) => ({ ok: true, client_w: w, client_h: h, hwnd: "0x1234", counters: { present_ours: 50 } });
const t = await startFake({ replies: { launch: { ok: true, pid: process.pid, injected: true }, status: st(1920, 1080) } });
try {
  let r = await t.call("launch", { exe: "C:/Games/X/x.exe", args: "-screen-fullscreen 0 -screen-width 1600 -screen-height 900" });
  check(!r.isError && r.json?.requested_size_ignored === true && /1600x900/.test(String(r.json?.warnings)) && /1920x1080/.test(String(r.json?.warnings)),
    "Unity size args ignored (1920x1080 popup): warning names both sizes", r.text.slice(0, 400));
  check(/own options or INI/.test(String(r.json?.warnings)), "the warning names the fix (set the size in the game's options / INI)");

  t.setScenario({ replies: { launch: { ok: true, pid: process.pid, injected: true }, status: st(1600, 900) } });
  r = await t.call("launch", { exe: "C:/Games/X/x.exe", args: "-screen-width 1600 -screen-height 900" });
  check(!r.isError && !r.json?.requested_size_ignored && !/requested_size/.test(r.text), "matching size: no warning", r.text.slice(0, 300));
  r = await t.call("launch", { exe: "C:/Games/X/x.exe" });
  check(!/requested_size/.test(r.text), "no size args: no warning");
  r = await t.call("launch", { exe: "C:/Games/X/x.exe", args: "-windowed -nolog" });
  check(!/requested_size/.test(r.text), "args without a size: no warning");

  t.setScenario({ replies: { launch: { ok: true, pid: process.pid, injected: true }, status: st(1280, 960) } });
  r = await t.call("launch", { exe: "C:/Games/X/x.exe", args: "-w -r800x600" });
  check(r.json?.requested_size_ignored === true && /800x600/.test(String(r.json?.warnings)), "-w -r800x600 (old DirectDraw games) is understood", r.text.slice(0, 300));
} finally { await t.close(); }
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
check(eq(requestedSize("-width 1280 -height 720")?.w, 1280) && requestedSize("-width 1280 -height 720")?.h === 720, "parse -width/-height");
check(requestedSize("-ResX=1024 -ResY=768")?.h === 768, "parse -ResX=/-ResY=");
check(requestedSize("-w 1280 -h 720")?.w === 1280, "parse -w N -h N");
check(requestedSize("-w")?.w === undefined && requestedSize("-screen-width 800")?.w === undefined, "a lone -w or a single dimension is not a size");
check(requestedSize("-r1024x768")?.w === 1024 && requestedSize("-resolution 1920x1080")?.h === 1080, "parse -rWxH and -resolution WxH");
console.log(state.failures ? `launch size: FAIL (${state.failures})` : `launch size: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
