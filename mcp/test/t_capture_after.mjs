// Fix 6: capture_after (bool | frames) on click, key, click_text, move, drag, pad: the reply carries the post-action frame.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const t = await startFake({ replies: { capture: { ok: true, method: "swapchain", capture_path: "d3d11", w: 1, h: 1, frame: 9 } } });
const cases = [
  ["click", { x: 10, y: 20 }, "click"], ["key", { key: "enter" }, "key"], ["click_text", { text: "Play" }, "click-text"],
  ["move", { x: 5, y: 6 }, "mouse"], ["drag", { x1: 1, y1: 2, x2: 3, y2: 4 }, "drag"], ["pad", { buttons: "a" }, "pad"],
];
try {
  const props = Object.fromEntries((await t.client.listTools()).tools.map((x) => [x.name, x.inputSchema?.properties ?? {}]));
  for (const [tool] of cases) check(!!props[tool]?.capture_after, `${tool} schema has capture_after`);
  for (const [tool, args, cmd] of cases) {
    t.clearCalls();
    const r = await t.call(tool, { pid, ...args, capture_after: true });
    const seq = t.calls().map((a) => cmdOf(a)[0]).filter((c) => c === cmd || c === "capture");
    check(!r.isError && seq.join() === `${cmd},capture`, `${tool}: action then capture`, seq.join());
    const cap = t.callsOf("capture").at(-1) ?? [];
    check(cap.includes("--after-frames") && cap[cap.indexOf("--after-frames") + 1] === "3", `${tool}: capture_after:true waits 3 presented frames (--after-frames)`, JSON.stringify(cap));
    check(!!r.image && r.json?.capture_after?.w === 1, `${tool}: reply has the inline image and a capture_after summary`, r.text.slice(0, 250));
    check(r.json?.ok === true, `${tool}: the action's own JSON is still there`);
  }
  t.clearCalls();
  const n = await t.call("click", { pid, x: 1, y: 1, capture_after: 7 });
  const cap7 = t.callsOf("capture").at(-1) ?? [];
  check(cap7[cap7.indexOf("--after-frames") + 1] === "7", "capture_after:7 = --after-frames 7", JSON.stringify(cap7));
  t.clearCalls();
  await t.call("key", { pid, key: "a", capture_after: 0 });
  const cap0 = t.callsOf("capture").at(-1);
  check(!!cap0 && (!cap0.includes("--after-frames") || cap0[cap0.indexOf("--after-frames") + 1] === "0"), "capture_after:0 = capture right away (no frame wait)", JSON.stringify(cap0));
  t.clearCalls();
  const off = await t.call("key", { pid, key: "a", capture_after: false });
  const none = await t.call("key", { pid, key: "a" });
  check(t.callsOf("capture").length === 0 && !off.image && !none.image, "false / omitted: no capture, no image");
  t.setScenario({ replies: { key: { ok: false, error: "refused" }, capture: { ok: true, w: 1, h: 1 } } });
  t.clearCalls();
  const bad = await t.call("key", { pid, key: "a", capture_after: true });
  check(bad.isError && t.callsOf("capture").length === 0, "a failed action takes no picture");
  t.setScenario({ replies: { capture: { ok: false, error: "no frame" } } });
  const cf = await t.call("key", { pid, key: "a", capture_after: true });
  check(!cf.isError && cf.json?.ok === true && /no frame/.test(JSON.stringify(cf.json?.capture_after_error ?? "")), "a failed capture keeps the action ok and reports capture_after_error", cf.text.slice(0, 250));
} finally { await t.close(); }
console.log(state.failures ? `capture_after: FAIL (${state.failures})` : `capture_after: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
