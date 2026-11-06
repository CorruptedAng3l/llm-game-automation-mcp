// SAME fake-only SDK controls against unchanged and candidate servers.
// Synthetic PNGs/argv prove MCP wiring, not native input, OCR or game response.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
import { fileURLToPath } from "node:url";
import { decodePng } from "../build/png.js";
const { state, check } = makeChecker(), pid = process.pid;
const flag = (a, k) => { const i = (a ?? []).indexOf(k); return i < 0 ? undefined : a[i + 1]; };
const decode = (r) => { try { return decodePng(Buffer.from(r.image.data, "base64")); } catch { return null; } };
const rgb = (im, x, y) => im ? [...im.data.subarray((y * im.width + x) * 4, (y * im.width + x) * 4 + 3)] : null;
const rectangle = { x: 100, y: 40, w: 320, h: 180 };
const done = { ok: true, operation: "modeled-action", input_injected: true };
const queued = { ...done, job_id: "seq-owned", job_state: "queued", accepted: true };
const names = ["key", "click", "click-text", "mouse", "move", "seq"];
const families = [
  ["key", { key: "return", hold_ms: 150 }, "key"],
  ["click", { x: 20, y: 30 }, "click"],
  ["click_text", { text: "Play" }, "click-text"],
  ["move", { x: 20, y: 30 }, "mouse"],
  ["move_relative", { dx: -120, dy: 17, raw: true }, "move"],
  ["key_sequence", { steps: [{ action: "key", key: "w", ms: 150 }, { action: "wait", ms: 10 }] }, "seq"],
];
const scenario = (extra = {}) => ({ png: { w: 1280, h: 720, core_capture: true },
  replies: Object.fromEntries(names.map((n) => [n, n === "seq" ? queued : done])), ...extra });
const t = await startFake(scenario());
const actions = () => t.calls().filter((a) => names.includes(cmdOf(a)[0]));
try {
  const tools = (await t.client.listTools()).tools;
  for (const [name] of families) {
    const p = tools.find((x) => x.name === name)?.inputSchema?.properties ?? {};
    check(["capture_after", "max_width", "scale", "region", "format", "quality", "capture_settle_ms"].every((k) => !!p[k]), `${name} advertises bounded action image options`);
  }
  for (const [name, args, command] of families) {
    t.clearCalls();
    const r = await t.call(name, { pid, ...args, capture_after: 'immediate' });
    const cap = t.callsOf("capture")[0], im = decode(r);
    check(!r.isError && actions().length === 1 && cmdOf(actions()[0])[0] === command && t.callsOf("capture").length === 1 &&
      flag(cap, "--max-width") === "960" && im?.width === 960 && im.height === 540,
      `${name} schedules exactly one action then one bounded 960px image`, r.text);
    check(r.json?.operation === "modeled-action" && r.json.input_injected === true && r.json.capture_after?.image?.scale === 0.75,
      `${name} preserves the action receipt and actual image mapping`, r.text);

    t.clearCalls();
    const crop = await t.call(name, { pid, ...args, capture_after: 'immediate', max_width: 160, region: rectangle });
    const ci = decode(crop), cc = t.callsOf("capture")[0], map = crop.json?.capture_after?.image;
    check(!crop.isError && actions().length === 1 && t.callsOf("capture").length === 1 && flag(cc, "--max-width") === "160" &&
      flag(cc, "--region") === "100,40,320,180" && ci?.width === 160 && ci.height === 90,
      `${name} forwards the explicit observation ROI and size`, JSON.stringify(t.calls()));
    check(JSON.stringify(rgb(ci, 10, 20)) === "[120,80,77]" && map?.offset_x === 100 && map.offset_y === 40 && map.scale === 0.5,
      `${name} cropped PNG independently agrees with client (120,80) at image (10,20)`, crop.text);
  }
  t.clearCalls();
  await t.call("click", { pid, x: 10, y: 20, coord_space: "capture" });
  const mapped = cmdOf(t.callsOf("click")[0] ?? []);
  check(mapped[2] === "120" && mapped[3] === "80", "successful action image maps the later capture-space click to client (120,80)", JSON.stringify(mapped));

  t.clearCalls();
  let r = await t.call("move_relative", { pid, dx: -120, dy: 17, raw: true, steps: 5, capture_after: 'immediate', scale: 0.5, region: rectangle });
  let cap = t.callsOf("capture")[0], im = decode(r), relative = cmdOf(t.callsOf("move")[0] ?? []);
  check(!r.isError && relative[1] === "-120" && relative[2] === "17" && flag(relative, "--steps") === "5" && relative.includes("--raw") &&
    flag(cap, "--scale") === "0.5" && flag(cap, "--max-width") === undefined && im?.width === 160,
    "relative deltas stay unscaled while explicit observation scale suppresses the default cap", JSON.stringify(t.calls()));
  t.clearCalls();
  r = await t.call("key", { pid, key: "return", capture_after: 'immediate', scale: 2, region: { x: 100, y: 40, w: 80, h: 60 } }); im = decode(r);
  check(!r.isError && im?.width === 160 && im.height === 120 && r.json?.capture_after?.image?.scale === 2 &&
    r.json.capture_after.image.scaled_by === "mcp", "key PNG enlargement reuses existing MCP transform mapping", r.text);
  t.clearCalls();
  r = await t.call("click_text", { pid, text: "Play", capture_after: 'immediate', max_width: 320, format: "jpeg", quality: 65 }); cap = t.callsOf("capture")[0];
  check(!r.isError && r.image?.mimeType === "image/jpeg" && flag(cap, "--format") === "jpeg" && flag(cap, "--quality") === "65" &&
    flag(cap, "--max-width") === "320", "click_text forwards and labels JPEG observation options (encoding is modeled)", r.text);

  for (const [name, args] of families) for (const options of [
    { capture_after: 'immediate', max_width: 160, scale: 0.5 },
    { capture_after: 'immediate', quality: 70 },
    { capture_after: false, max_width: 160 },
    { capture_after: 'immediate', format: "jpeg", scale: 2 },
    { capture_after: 'immediate', max_width: 63 },
    { capture_after: 'immediate', region: { x: 0, y: 0, w: 0, h: 10 } },
  ]) {
    t.clearCalls(); r = await t.call(name, { pid, ...args, ...options });
    check(r.isError && t.calls().length === 0, `${name} rejects static incompatible options before ANY CLI I/O: ${JSON.stringify(options)}`, r.text);
  }
  for (const [name, args] of families) {
    t.clearCalls(); r = await t.call(name, { pid, ...args, capture_after: false });
    check(!r.isError && actions().length === 1 && !t.callsOf("capture").length && !r.image, `${name} false retains the action without an implicit image`);
  }
  t.clearCalls(); r = await t.call("click_text", { pid, text: "Play", region: rectangle });
  check(!r.isError && t.callsOf("click-text").length === 1 && flag(t.callsOf("click-text")[0], "--region") === "100,40,320,180" && !t.callsOf("capture").length,
    "legacy click_text search-only region remains usable without capture_after");
  t.clearCalls(); r = await t.call("capture", { pid }); im = decode(r);
  check(!r.isError && im?.width === 1280 && flag(t.callsOf("capture")[0], "--max-width") === undefined, "standalone capture retains its full-size default");

  t.clearCalls(); r = await t.call("key_sequence", { pid, steps: [{ action: "key", key: "w", ms: 150 }], capture_after: 'immediate', max_width: 160 });
  check(!r.isError && r.json?.job_id === "seq-owned" && r.json.job_state === "queued" && r.json.accepted === true &&
    r.json.capture_after?.observation?.action_state === "async_job_may_be_running" && /job_status/.test(r.json.note ?? "") && t.lastSeqText() === "key w 150\n",
    "sequence observation preserves scheduling receipt and exact sequence without inventing completion", r.text);
  await t.call("job_cancel", { pid, id: "seq-owned" });
  check(cmdOf(t.callsOf("job-cancel").at(-1) ?? [])[1] === "seq-owned", "sequence cancellation route remains explicit and unchanged");

  t.setScenario(scenario({ replies: { ...scenario().replies, key: { ...done, job_id: "key-owned", job_state: "running", accepted: true } } }));
  t.clearCalls(); r = await t.call("key", { pid, key: "w", hold_ms: 1000, async: true, capture_after: 'immediate', max_width: 160 });
  check(!r.isError && r.json?.job_state === "running" && r.json.capture_after?.observation?.action_state === "async_job_may_be_running" && actions().length === 1,
    "async key observation does not consume or fabricate completion", r.text);

  t.setScenario(scenario({ replies: { ...scenario().replies, capture: { ok: false, error: "modeled backend failure" } } }));
  for (const [name, args] of families) {
    t.clearCalls(); r = await t.call(name, { pid, ...args, capture_after: 'immediate', max_width: 160 });
    check(!r.isError && r.json?.operation === "modeled-action" && r.json.input_injected === true && /modeled backend failure/.test(r.json.capture_after_error ?? "") &&
      actions().length === 1 && t.callsOf("capture").length === 1 && !r.image,
      `${name} dynamic capture failure preserves the original action and never replays it`, r.text);
  }
  t.setScenario(scenario({ replies: Object.fromEntries(names.map((n) => [n, { ok: false, error: "modeled action refusal" }])) }));
  for (const [name, args] of families) {
    t.clearCalls(); r = await t.call(name, { pid, ...args, capture_after: 'immediate' });
    check(r.isError && /modeled action refusal/.test(r.text) && actions().length === 1 && !t.callsOf("capture").length,
      `${name} failed action has no observation or action retry`, r.text);
  }
  t.setScenario(scenario({ replies: { ...scenario().replies, key: { ...done, _delay_ms: 350 } } })); t.clearCalls();
  const pending = t.call("key", { pid, key: "w", capture_after: 'immediate', max_width: 160 });
  for (let i = 0; i < 200 && !t.callsOf("key").length; i++) await new Promise((resolve) => setTimeout(resolve, 10));
  await t.call("detach", { pid }); r = await pending;
  check(!r.isError && /attachment changed/.test(r.json?.capture_after_error ?? "") && actions().length === 1 && !t.callsOf("capture").length,
    "observed detach while action is in flight suppresses the stale observation without replay", r.text);
} finally { await t.close(); }

const pw = await startFake(scenario({ replies: { ...scenario().replies,
  status: { ok: true, frames: 0, frame_api: "none", fullscreen: { refused: 0, failures: 0, blocked_before_first_present: false } } } }),
  { VINPUT_EXE: fileURLToPath(new URL("./fake_present_zero.mjs", import.meta.url)) });
try {
  let r = await pw.call("key", { pid, key: "return", capture_after: true, max_width: 160, region: rectangle });
  check(!r.isError && pw.callsOf("key").length === 1 && pw.callsOf("capture").length === 2 &&
    pw.callsOf("capture").every((a) => flag(a, "--max-width") === "160" && flag(a, "--region") === "100,40,320,180") &&
    r.json?.capture_after?.fallback?.present_wait_satisfied === false && decode(r)?.width === 160,
    "boolean PrintWindow fallback keeps action options and does not replay key", r.text);
  pw.clearCalls(); r = await pw.call("key", { pid, key: "return", capture_after: 2, max_width: 160 });
  check(!r.isError && !!r.json?.capture_after_error && pw.callsOf("key").length === 1 && pw.callsOf("capture").length === 1,
    "numeric Present request stays strict with image options", r.text);
} finally { await pw.close(); }

const fault = await startFake(scenario(), { VINPUT_CAPTURE_DIR: fileURLToPath(new URL("./lib.mjs", import.meta.url)) + "/blocked-child" });
try {
  for (const [name, args, command] of families) {
    fault.clearCalls();
    const r = await fault.call(name, { pid, ...args, capture_after: 'immediate', max_width: 160 });
    check(!r.isError && r.json?.operation === "modeled-action" && r.json.input_injected === true && /post-action capture failed/.test(r.json.capture_after_error ?? "") &&
      fault.callsOf(command).length === 1 && !fault.callsOf("capture").length,
      `${name} capture setup exception retains the performed action receipt`, r.text);
  }
} finally { await fault.close(); }
console.log(state.failures ? `action capture options: FAIL (${state.failures}), PASS (${state.passes})` : `action capture options: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
