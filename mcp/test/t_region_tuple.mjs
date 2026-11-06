// ROI aliases exercise actual MCP parsers/argv, no game or native capture.
import { makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ png: { w: 8, h: 8 }, replies: { status: { ok: true } } });
const tuple = [1, 2, 4, 5], object = { x: 1, y: 2, w: 4, h: 5 };
const families = [
  ["capture", {}], ["ocr", {}], ["wait_change", { timeout_s: 0.001 }],
  ["capture_burst", { frames: 1 }], ["pad", { ms: 1, capture_after: 'immediate', capture_settle_ms: 0 }],
  ["pad_move", { direction: "up", ms: 1, capture_settle_ms: 0 }],
];
const recordedRegion = () => t.calls().flatMap((a) => { const i = a.indexOf("--region"); return i >= 0 ? [a[i + 1]] : []; });
try {
  for (const [name, args] of families) {
    t.clearCalls(); let r = await t.call(name, { pid, ...args, region: tuple });
    check(!r.isError && recordedRegion().includes("1,2,4,5"), `${name} accepts tuple and forwards normalized client rectangle`);
    t.clearCalls(); r = await t.call(name, { pid, ...args, region: object });
    check(!r.isError && recordedRegion().includes("1,2,4,5"), `${name} keeps existing object rectangle behavior`);
  }
  for (const bad of [[], [0, 0, 1], [0, 0, 1, 1, 2], [-1, 0, 1, 1], [0, -1, 1, 1],
    [0, 0, 0, 1], [0, 0, 1, 0], [0.5, 0, 1, 1], [0, 0, 1.5, 1], [0, 0, "1", 1], [false, 0, 1, 1], null]) {
    t.clearCalls(); const r = await t.call("pad", { pid, ms: 1, capture_after: 'immediate', region: bad });
    check(r.isError && t.calls().length === 0, `malformed tuple fails before action or capture: ${JSON.stringify(bad)}`);
  }
  t.clearCalls(); const r = await t.call("capture", { pid, region: [0, 0, 1, 1] });
  check(!r.isError && recordedRegion().includes("0,0,1,1"), "zero origin and positive one-pixel tuple accepted");
} finally { await t.close(); }
console.log(`region tuple: ${state.passes} PASS / ${state.failures} FAIL`);
process.exit(state.failures ? 1 : 0);
