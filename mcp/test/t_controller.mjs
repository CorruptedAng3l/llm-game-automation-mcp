// MCP protocol/command construction models only; never real input or a game.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
import { fileURLToPath } from "node:url";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ replies: { pad: { ok: true, job_state: "completed" }, json: { ok: true, job_state: "completed" } } });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const flag = (a, k) => { const i = a.indexOf(k); return i < 0 ? undefined : a[i + 1]; };
const payload = () => { try { return JSON.parse(cmdOf(t.callsOf("json").at(-1) ?? [])[1]); } catch { return null; } };
// An absent new tool on the baseline is an ordinary positive-contract failure,
// not an exception that prevents the existing-pad controls from completing.
const moveCall = async (args) => { try { return await t.call("pad_move", args); } catch (e) { return { isError: true, text: String(e), json: null }; } };
try {
  const tools = (await t.client.listTools()).tools;
  const move = tools.find((x) => x.name === "pad_move"), pprops = tools.find((x) => x.name === "pad")?.inputSchema?.properties ?? {};
  check(!!move && !!pprops.capture_settle_ms, "named movement and explicit pad capture settle are advertised");
  await t.call("pad", { pid, buttons: "a" });
  check(JSON.stringify(cmdOf(t.callsOf("pad").at(-1) ?? [])) === JSON.stringify(["pad", "a"]), "existing pad default argv remains unchanged");
  const signs = { up: [0, 1], "up-right": [1, 1], right: [1, 0], "down-right": [1, -1], down: [0, -1], "down-left": [-1, -1], left: [-1, 0], "up-left": [-1, 1] };
  for (const speed of ["walk", "run"]) for (const [direction, [sx, sy]] of Object.entries(signs)) {
    t.clearCalls();
    const r = await moveCall({ pid, direction, speed, ms: 600, capture_after: false });
    const a = t.callsOf("pad").at(-1) ?? [], x = Number(flag(a, "--lx")), y = Number(flag(a, "--ly")), magnitude = speed === "walk" ? 20000 : 32767;
    check(!r.isError && t.callsOf("pad").length === 1 && t.callsOf("json").length === 0 && Math.sign(x) === sx && Math.sign(y) === sy && Math.abs(Math.hypot(x, y) - magnitude) <= 2 &&
      ["--rx", "--ry", "--lt", "--rt"].every((k) => flag(a, k) === "0") && !t.callsOf("capture").length && cmdOf(a)[2] === "600" && r.json?.pad_move?.screen_motion_verified === false,
      `${speed} ${direction}: bounded normalized left-stick request with other controls neutral`, JSON.stringify({ a, receipt: r.json?.pad_move }));
  }
  // Independent quarter-turn/inversion vectors, not the production trig formula.
  for (const [direction, angle, speed, x, y] of [["up", 90, "walk", 20000, 0], ["up", 180, "walk", 0, -20000], ["right", 90, "run", 0, -32767], ["up-left", 180, "walk", 14142, -14142]]) {
    t.clearCalls(); const r = await moveCall({ pid, direction, speed, ms: 600, screen_rotation_deg: angle, capture_after: false });
    const a = t.callsOf("pad").at(-1) ?? [];
    check(!r.isError && Number(flag(a, "--lx")) === x && Number(flag(a, "--ly")) === y && r.json?.pad_move?.screen_rotation_deg === angle,
      `${direction} rotated ${angle}: independently expected compensated axes`, JSON.stringify(a));
  }
  t.clearCalls();
  let defaultMove = await moveCall({ pid, direction: "up", ms: 100 });
  let cap = t.callsOf("capture").at(-1) ?? [];
  check(!defaultMove.isError && !!defaultMove.image && flag(cap, "--after-frames") === "0" && defaultMove.json?.capture_after?.observation?.settle_ms_requested === 100,
    "new pad_move defaults to immediate backend readback after 100ms settle, without a Present wait", defaultMove.text);
  t.clearCalls();
  let r = await moveCall({ pid, direction: "up-left", ms: 600, smooth: true, capture_after: false });
  let p = payload(), states = [];
  try { states = JSON.parse(p?.states_json ?? "[]"); } catch { /* check below */ }
  const nonzero = states.filter((x) => x.lx !== 0 || x.ly !== 0);
  check(!r.isError && t.callsOf("json").length === 1 && !t.callsOf("pad").length && p?.cmd === "pad-timeline" && p.pacing === "time" && p.ms === 600 &&
    states.length >= 5 && states.length <= 256 && states[0].at_ms === 0 && states.at(-1).at_ms === 600 && states[0].lx === 0 && states[0].ly === 0 && states.at(-1).lx === 0 && states.at(-1).ly === 0 &&
    states.every((x, i) => i === 0 || x.at_ms > states[i - 1].at_ms) && nonzero.some((x) => x.lx === -14142 && x.ly === 14142) && new Set(nonzero.map((x) => Math.abs(x.lx))).size >= 3,
    "smooth move is one finite eased timeline with intermediate values, full deflection and neutral endpoints", JSON.stringify(p));
  t.clearCalls(); r = await moveCall({ pid, direction: "up", ms: 79, smooth: true });
  check(r.isError && !t.callsOf("pad").length && !t.callsOf("json").length, "too-short smooth move is refused before input");
  for (const args of [{ direction: "northwest", ms: 100 }, { direction: "up", ms: 60001 }, { direction: "up", ms: 0 }, { direction: "up", ms: 100, speed: "turbo" }, { direction: "up", ms: 100, screen_rotation_deg: 361 }]) {
    t.clearCalls(); r = await moveCall({ pid, ...args });
    check(r.isError && !t.callsOf("pad").length && !t.callsOf("json").length, `invalid named move is refused before input: ${JSON.stringify(args)}`);
  }
  t.clearCalls(); r = await t.call("pad", { pid, buttons: "a", capture_settle_ms: 0 });
  check(r.isError && !t.callsOf("pad").length, "settle without requested capture is refused before the action");
  t.clearCalls(); r = await t.call("pad", { pid, buttons: "a", capture_after: 0, capture_settle_ms: 5001 });
  check(r.isError && !t.callsOf("pad").length, "settle delay bound is validated before the action");

  // Fake records process start times; no assertion of actual rendered-frame freshness.
  t.setScenario({ replies: { pad: { ok: true, job_state: "completed", _delay_ms: 80 } } }); t.clearCalls();
  const pending = t.call("pad", { pid, buttons: "a", ms: 150, capture_after: 0, capture_settle_ms: 240 });
  for (let i = 0; i < 100 && !t.callsOf("pad").length; i++) await sleep(10);
  await sleep(110); const status = await t.call("status", { pid }); r = await pending;
  const calls = t.timedCalls(), pad = calls.find((x) => cmdOf(x.argv)[0] === "pad"), capture = calls.find((x) => cmdOf(x.argv)[0] === "capture"), stat = calls.find((x) => cmdOf(x.argv)[0] === "status");
  check(!r.isError && !!r.image && t.callsOf("pad").length === 1 && !!capture && !!pad && capture.t - pad.t >= 300 &&
    r.json?.capture_after?.observation?.settle_ms_requested === 240 && r.json.capture_after.observation.settle_ms_elapsed >= 220,
    "finite button plus timer returns one image after the action reply without retrying it", r.text);
  check(!status.isError && !!stat && !!capture && stat.t < capture.t, "settle timer leaves the MCP event loop available to another tool");

  t.setScenario({ replies: { json: { ok: true, job_id: "smooth-ongoing", job_state: "running" } } }); t.clearCalls();
  r = await moveCall({ pid, direction: "right", ms: 5000, smooth: true, async: true });
  check(!r.isError && !!r.image && r.json?.job_id === "smooth-ongoing" && r.json?.job_state === "running" && r.json.capture_after?.observation?.action_state === "async_job_may_be_running" && payload()?.async === true,
    "new helper default observation preserves the async native job identity and running state", r.text);
  t.setScenario({ replies: { pad: { ok: true, job_id: "ongoing", job_state: "running" } } }); t.clearCalls();
  r = await t.call("pad", { pid, buttons: "a", ms: 5000, async: true, capture_after: 0, capture_settle_ms: 0 });
  check(!r.isError && r.json?.job_state === "running" && r.json.capture_after?.observation?.action_state === "async_job_may_be_running", "async capture does not turn a running job into a completed action");
  await t.call("job_cancel", { pid, id: "ongoing" });
  check(cmdOf(t.callsOf("job-cancel").at(-1) ?? [])[1] === "ongoing", "async job keeps the existing cancellation route");
  t.setScenario({ replies: { pad: { ok: true } } }); t.clearCalls();
  r = await t.call("pad", { pid, ly: 20000, hold: true, capture_after: 0, capture_settle_ms: 0 });
  check(!r.isError && r.json?.capture_after?.observation?.action_state === "held_until_changed" && (t.callsOf("pad").at(-1) ?? []).includes("--hold") && !t.callsOf("release").length, "held capture leaves the hold active and labels it honestly");

  t.setScenario({ replies: { pad: { ok: true } } }); t.clearCalls();
  const stale = t.call("pad", { pid, buttons: "a", ms: 150, capture_after: 0, capture_settle_ms: 400 });
  for (let i = 0; i < 100 && !t.callsOf("pad").length; i++) await sleep(10);
  await sleep(40); await t.call("detach", { pid }); r = await stale;
  check(!r.isError && /attachment changed/.test(r.json?.capture_after_error ?? "") && !t.callsOf("capture").length && t.callsOf("pad").length === 1,
    "observed attachment change during settle suppresses the capture without retrying the action", r.text);

  t.setScenario({ replies: { pad: { ok: false, error: "owned modeled action refusal" } } }); t.clearCalls();
  r = await t.call("pad", { pid, buttons: "a", capture_after: 0, capture_settle_ms: 10 });
  check(r.isError && !t.callsOf("capture").length && t.callsOf("pad").length === 1, "failed action performs no settle capture and is not retried");
  t.setScenario({ replies: { json: { ok: true, job_state: "completed" } } }); t.clearCalls();
  r = await moveCall({ pid, direction: "right", ms: 600, smooth: true, capture_after: 0, capture_settle_ms: 0 });
  check(!r.isError && !!r.image && t.callsOf("json").length === 1 && t.callsOf("capture").length === 1, "one smooth action can return its post-reply image");
  for (const [name, args] of [["pad_ramp", { axis: "ly", from: 0, to: 20000, ms: 300 }], ["pad_timeline", { states: [{ at_ms: 0 }, { at_ms: 100, ly: 20000 }, { at_ms: 200 }] }]]) {
    t.clearCalls(); r = await t.call(name, { pid, ...args, capture_after: 0, capture_settle_ms: 0 });
    check(!r.isError && !!r.image && r.json?.capture_after?.observation?.settle_ms_requested === 0, `${name} uses the same explicit post-reply capture contract`);
  }
} finally { await t.close(); }

// Existing safe boolean fallback is reused for the pad path; no action retry.
const status = { ok: true, frames: 0, frame_api: "none", fullscreen: { refused: 0, failures: 0, blocked_before_first_present: false } };
const pw = await startFake({ replies: { status, pad: { ok: true }, capture: { ok: true, method: "printwindow", capture_path: "printwindow", frame: 0 } } },
  { VINPUT_EXE: fileURLToPath(new URL("./fake_present_zero.mjs", import.meta.url)) });
try {
  const r = await pw.call("pad", { pid, buttons: "a", ms: 150, capture_after: true, capture_settle_ms: 0 });
  check(!r.isError && !!r.image && pw.callsOf("pad").length === 1 && pw.callsOf("capture").length === 2 && r.json?.capture_after?.fallback?.present_wait_satisfied === false,
    "pad capture retains the reported zero-Present PrintWindow fallback and one action only", r.text);
} finally { await pw.close(); }
console.log(state.failures ? `controller MCP: FAIL (${state.failures})` : `controller MCP: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
