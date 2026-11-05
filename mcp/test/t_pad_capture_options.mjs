// SAME old/new fake-SDK contract. No game, native input or installed-hook evidence.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
import { fileURLToPath } from "node:url";
const { decodePng } = await import("../build/png.js");
const { state, check } = makeChecker(), pid = process.pid;
const flag = (a, key) => { const i = (a ?? []).indexOf(key); return i < 0 ? undefined : a[i + 1]; };
const clickPoint = (a) => { const rest = cmdOf(a ?? []).slice(1), i = rest.findIndex((s) => s.startsWith("--")); return (i < 0 ? rest : rest.slice(0, i)).filter((s) => !["left", "right", "middle", "x1", "x2"].includes(s)); };
const decode = (r) => { try { return decodePng(Buffer.from(r.image.data, "base64")); } catch { return null; } };
const rgb = (im, x, y) => im ? [...im.data.subarray((y * im.width + x) * 4, (y * im.width + x) * 4 + 3)] : null;
const rect = { x: 100, y: 40, w: 320, h: 180 };
const completed = { ok: true, job_id: "observed-job", job_state: "completed", accepted: false, input_injected: true };
const scenario = (extra = {}) => ({ png: { w: 1280, h: 720, core_capture: true }, replies: { pad: completed, json: completed }, ...extra });
const families = [
  ["pad", { buttons: "a", ms: 150 }],
  ["pad_move", { direction: "up", ms: 150 }],
  ["pad_look", { degrees: 90, calibration: { rx: 20000, degrees_per_second: 600 } }],
  ["pad_rotate", { stick: "left", start_deg: 0, direction: "clockwise", turns: 0.25, ms: 300 }],
  ["pad_ramp", { axis: "lx", from: 0, to: 20000, ms: 150, pacing: "time" }],
  ["pad_timeline", { states: [{ at_ms: 0 }, { at_ms: 100, lx: 20000 }], ms: 150, pacing: "time" }],
];
const t = await startFake(scenario());
const actionCount = () => t.callsOf("pad").length + t.callsOf("json").length;
try {
  const tools = (await t.client.listTools()).tools;
  for (const [name] of families) {
    const p = tools.find((x) => x.name === name)?.inputSchema?.properties ?? {};
    check(["max_width", "scale", "region", "format", "quality"].every((key) => !!p[key]), `${name} advertises all shared image options`);
  }

  for (const [name, args] of families) {
    t.clearCalls();
    const r = await t.call(name, { pid, ...args, capture_after: 0, capture_settle_ms: 0 });
    const cap = t.callsOf("capture")[0], im = decode(r);
    check(!r.isError && actionCount() === 1 && t.callsOf("capture").length === 1 && flag(cap, "--max-width") === "960",
      `${name} uses one action and a 960px default image cap`, JSON.stringify(t.calls()));
    check(im?.width === 960 && im.height === 540 && r.json?.capture_after?.image?.scale === 0.75 && r.json.capture_after.image.w === 960,
      `${name} returns actual reduced PNG dimensions plus its client mapping`, r.text);
  }

  t.clearCalls();
  let r = await t.call("pad_move", { pid, direction: "right", ms: 200, capture_settle_ms: 0, region: rect, max_width: 160 });
  let im = decode(r), cap = t.callsOf("capture").at(-1);
  check(!r.isError && im?.width === 160 && im.height === 90 && flag(cap, "--region") === "100,40,320,180" && flag(cap, "--max-width") === "160",
    "client ROI and explicit size reach the native capture argv", JSON.stringify(cap));
  check(JSON.stringify(rgb(im, 10, 20)) === "[120,80,77]" && r.json?.capture_after?.image?.scale === 0.5 &&
    r.json.capture_after.image.offset_x === 100 && r.json.capture_after.image.offset_y === 40,
    "independent PNG client-coordinate colour oracle agrees with the returned crop mapping", r.text);
  t.clearCalls();
  await t.call("click", { pid, x: 10, y: 20, coord_space: "capture" });
  check(JSON.stringify(clickPoint(t.callsOf("click").at(-1))) === '["120","80"]', "later image-space click maps (10,20) to client (120,80)", JSON.stringify(t.calls()));

  t.clearCalls();
  r = await t.call("pad", { pid, ms: 150, capture_after: 0, scale: 0.5, region: rect });
  im = decode(r); cap = t.callsOf("capture").at(-1);
  check(!r.isError && im?.width === 160 && im.height === 90 && flag(cap, "--scale") === "0.5" && flag(cap, "--max-width") === undefined,
    "explicit scale suppresses the default width cap", JSON.stringify(cap));
  t.clearCalls();
  r = await t.call("pad_rotate", { pid, ...families[3][1], capture_settle_ms: 0, region: { x: 100, y: 40, w: 80, h: 60 }, scale: 2 });
  im = decode(r); cap = t.callsOf("capture").at(-1);
  check(!r.isError && im?.width === 160 && im.height === 120 && r.json?.capture_after?.image?.scaled_by === "mcp" &&
    r.json.capture_after.image.scale === 2 && flag(cap, "--scale") === undefined && flag(cap, "--max-width") === undefined,
    "PNG enlargement reuses the existing MCP path without a hidden 960 cap", r.text);

  t.clearCalls();
  r = await t.call("pad_look", { pid, ...families[2][1], capture_settle_ms: 0, format: "jpeg", quality: 70, max_width: 320 });
  cap = t.callsOf("capture").at(-1);
  check(!r.isError && r.image?.mimeType === "image/jpeg" && r.json?.capture_after?.image?.format === "jpeg" &&
    flag(cap, "--format") === "jpeg" && flag(cap, "--quality") === "70" && flag(cap, "--max-width") === "320",
    "JPEG quality and explicit size are forwarded and labelled honestly", r.text);

  // Every family must run the compatibility check BEFORE its own input call.
  for (const [name, args] of families) for (const options of [
    { capture_after: 0, max_width: 320, scale: 0.5 },
    { capture_after: 0, quality: 70 },
    { capture_after: false, max_width: 320 },
  ]) {
    t.clearCalls();
    r = await t.call(name, { pid, ...args, ...options });
    check(r.isError && actionCount() === 0 && t.callsOf("capture").length === 0, `${name} refuses incompatible options before input: ${JSON.stringify(options)}`, r.text);
  }
  for (const options of [
    { max_width: 63 }, { max_width: 8193 }, { scale: 0.01 }, { scale: 8.1 }, { format: "webp" },
    { region: { x: -1, y: 0, w: 10, h: 10 } }, { region: { x: 0, y: 0, w: 0, h: 10 } },
    { format: "jpeg", quality: 0 }, { format: "jpeg", quality: 101 }, { format: "jpeg", scale: 2 },
  ]) {
    t.clearCalls(); r = await t.call("pad", { pid, ms: 150, capture_after: 0, ...options });
    check(r.isError && actionCount() === 0 && t.callsOf("capture").length === 0, `static invalid image options never act: ${JSON.stringify(options)}`, r.text);
  }
  t.clearCalls(); r = await t.call("pad", { pid, ms: 150, region: rect });
  check(r.isError && actionCount() === 0, "plain pad image options need explicit enabled capture_after");
  t.clearCalls(); r = await t.call("pad", { pid, ms: 150 });
  check(!r.isError && actionCount() === 1 && !t.callsOf("capture").length, "plain pad still has no implicit image");
  t.clearCalls(); r = await t.call("pad_move", { pid, direction: "up", ms: 150, capture_after: false });
  check(!r.isError && actionCount() === 1 && !t.callsOf("capture").length, "capture_after false still disables a helper's default observation");
  t.clearCalls(); r = await t.call("capture", { pid }); im = decode(r);
  check(!r.isError && im?.width === 1280 && flag(t.callsOf("capture")[0], "--max-width") === undefined, "standalone capture keeps its full-size default");
  t.clearCalls(); r = await t.call("key", { pid, key: "return", capture_after: 0 }); im = decode(r);
  check(!r.isError && im?.width === 960 && flag(t.callsOf("capture")[0], "--max-width") === "960", "key action images share the bounded 960px observation default");

  t.setScenario(scenario({ png: { w: 1280, h: 720 }, replies: { pad: completed } }));
  t.clearCalls();
  r = await t.call("pad", { pid, ms: 150, capture_after: 0, max_width: 160, region: rect }); im = decode(r);
  check(!r.isError && im?.width === 160 && im.height === 90 && actionCount() === 1 && r.json?.capture_after?.image?.scaled_by === "mcp" &&
    r.json.capture_after.image.scale === 0.5 && r.json.capture_after.w === 320,
    "old core without transform proof is resized by MCP with separate saved and returned dimensions", r.text);

  t.setScenario(scenario({ replies: { pad: completed, capture: { ok: false, error: "modeled capture backend failure" } } })); t.clearCalls();
  r = await t.call("pad", { pid, ms: 150, capture_after: 0, max_width: 160 });
  check(!r.isError && r.json?.job_id === "observed-job" && r.json.job_state === "completed" && r.json.input_injected === true &&
    /modeled capture backend failure/.test(r.json.capture_after_error ?? "") && actionCount() === 1 && t.callsOf("capture").length === 1 && !r.image,
    "dynamic capture failure preserves the action outcome and never replays it", r.text);
  t.clearCalls(); await t.call("click", { pid, x: 10, y: 20, coord_space: "capture" });
  check(JSON.stringify(clickPoint(t.callsOf("click").at(-1))) === '["120","80"]', "failed capture leaves the prior successful coordinate mapping intact");

  t.setScenario(scenario({ replies: { pad: { ok: false, error: "modeled action refusal" } } })); t.clearCalls();
  r = await t.call("pad", { pid, ms: 150, capture_after: 0, region: rect, max_width: 160 });
  check(r.isError && /modeled action refusal/.test(r.text) && actionCount() === 1 && !t.callsOf("capture").length,
    "failed action has no observation attempt or action retry", r.text);

  t.setScenario(scenario({ replies: { pad: { ok: true, job_id: "async-pad", job_state: "running", accepted: true } } })); t.clearCalls();
  r = await t.call("pad_move", { pid, direction: "up", ms: 5000, async: true, capture_settle_ms: 0, max_width: 320 });
  check(!r.isError && r.json?.job_state === "running" && r.json.capture_after?.observation?.action_state === "async_job_may_be_running" && actionCount() === 1,
    "resized async observation retains the running job rather than inventing completion", r.text);
  await t.call("job_cancel", { pid, id: "async-pad" });
  check(cmdOf(t.callsOf("job-cancel").at(-1) ?? [])[1] === "async-pad", "existing cancellation route is unchanged");
  t.clearCalls();
  r = await t.call("pad", { pid, ly: 20000, hold: true, capture_after: 0, max_width: 320 });
  check(!r.isError && r.json?.capture_after?.observation?.action_state === "held_until_changed" && !t.callsOf("release").length,
    "resized held observation does not silently release or claim completion", r.text);
  await t.call("pad", { pid, off: true });
  check(cmdOf(t.callsOf("pad").at(-1) ?? [])[1] === "off", "coordinate-free pad-off cleanup remains available");

  t.setScenario(scenario()); t.clearCalls();
  const pending = t.call("pad_move", { pid, direction: "up", ms: 150, capture_settle_ms: 400, region: rect, max_width: 160 });
  for (let i = 0; i < 200 && !actionCount(); i++) await new Promise((resolve) => setTimeout(resolve, 10));
  await t.call("detach", { pid }); r = await pending;
  check(!r.isError && /attachment changed/.test(r.json?.capture_after_error ?? "") && actionCount() === 1 && !t.callsOf("capture").length,
    "observed detach during settle suppresses transformed capture with one original action", r.text);
} finally { await t.close(); }

const canonical = { ok: true, frames: 0, frame_api: "none", fullscreen: { refused: 0, failures: 0, blocked_before_first_present: false } };
const pw = await startFake(scenario({ replies: { pad: completed, status: canonical } }),
  { VINPUT_EXE: fileURLToPath(new URL("./fake_present_zero.mjs", import.meta.url)) });
try {
  let r = await pw.call("pad", { pid, ms: 150, capture_after: true, max_width: 160, region: rect });
  const im = decode(r), caps = pw.callsOf("capture");
  check(!r.isError && im?.width === 160 && caps.length === 2 && caps.every((a) => flag(a, "--region") === "100,40,320,180" && flag(a, "--max-width") === "160") &&
    r.json?.capture_after?.fallback?.present_wait_satisfied === false && pw.callsOf("pad").length === 1,
    "boolean PrintWindow retry keeps all image options without replaying the action", r.text);
  pw.clearCalls(); r = await pw.call("pad", { pid, ms: 150, capture_after: 2, max_width: 160 });
  check(!r.isError && !!r.json?.capture_after_error && pw.callsOf("capture").length === 1 && pw.callsOf("pad").length === 1,
    "numeric Present wait remains strict despite image options", r.text);
} finally { await pw.close(); }

// A private regular file cannot be a capture parent. mkdir fails before a capture CLI call.
const fault = await startFake({ replies: { pad: completed } },
  { VINPUT_CAPTURE_DIR: fileURLToPath(new URL("./lib.mjs", import.meta.url)) + "/blocked-child" });
try {
  const r = await fault.call("pad", { pid, ms: 150, capture_after: 0 });
  check(!r.isError && r.json?.ok === true && r.json.job_id === "observed-job" && r.json.input_injected === true &&
    /post-action capture failed/.test(r.json.capture_after_error ?? "") && fault.callsOf("pad").length === 1 && !fault.callsOf("capture").length,
    "capture setup exception retains the already-performed action receipt", r.text);
} finally { await fault.close(); }
console.log(state.failures ? `pad capture options: FAIL (${state.failures})` : `pad capture options: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
