// Caller-calibrated command construction only; fake CLI never emits input.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ replies: { pad: { ok: true, job_state: "completed" } } });
const flag = (a, k) => { const i = a.indexOf(k); return i < 0 ? undefined : a[i + 1]; };
const call = async (a) => { try { return await t.call("pad_look", { pid, ...a }); } catch (e) { return { isError: true, text: String(e), json: null }; } };
try {
  const tool = (await t.client.listTools()).tools.find((x) => x.name === "pad_look");
  check(!!tool?.inputSchema?.properties?.calibration && /angular|degrees/.test(tool?.description ?? ""), "degree wrapper requires an explicit angular calibration");
  // Independent rates/durations and signs, including an inverted controller mapping.
  for (const [degrees, rx, rate, expectedRx, ms, predicted] of [
    [90, 20000, 60, 20000, 1500, 90], [-45, 20000, 90, -20000, 500, -45],
    [90, -18000, 45, -18000, 2000, 90], [-90, -18000, 45, 18000, 2000, -90],
    [1, 20000, 3, 20000, 333, 0.999], [360, 10000, 6, 10000, 60000, 360],
    [1, 10000, 1000, 10000, 1, 1], [90, -32768, 60, -32768, 1500, 90],
  ]) {
    t.clearCalls(); const calibration = { rx, degrees_per_second: rate };
    const r = await call({ degrees, calibration, capture_after: false });
    const a = t.callsOf("pad").at(-1) ?? [], c = r.json?.pad_look;
    check(!r.isError && t.callsOf("pad").length === 1 && cmdOf(a)[1] === "none" && cmdOf(a)[2] === String(ms) && Number(flag(a, "--rx")) === expectedRx &&
      ["--lx", "--ly", "--ry", "--lt", "--rt"].every((k) => flag(a, k) === "0") && !t.callsOf("capture").length &&
      c?.requested_degrees === degrees && c?.angle_verified === false && Math.abs(c.predicted_degrees - predicted) < 1e-8 && JSON.stringify(c.calibration) === JSON.stringify(calibration),
      `${degrees}deg at calibrated rx${rx}/${rate}deg/s: expected finite right-stick request, no verified-angle claim`, JSON.stringify({ a, receipt: c }));
  }
  for (const args of [
    { degrees: 90 }, { degrees: 90, calibration: { rx: 0, degrees_per_second: 60 } },
    { degrees: 90, calibration: { rx: 20000, degrees_per_second: 0 } }, { degrees: 90, calibration: { rx: 20000, degrees_per_second: -1 } },
    { degrees: 90, calibration: { rx: 20000, degrees_per_second: "60" } }, { degrees: 90, calibration: { rx: 32768, degrees_per_second: 60 } },
    { degrees: 0, calibration: { rx: 20000, degrees_per_second: 60 } }, { degrees: 361, calibration: { rx: 20000, degrees_per_second: 60 } },
    { degrees: 360, calibration: { rx: 20000, degrees_per_second: 0.001 } }, { degrees: 1, calibration: { rx: 20000, degrees_per_second: 1e100 } },
    { degrees: -90, calibration: { rx: -32768, degrees_per_second: 60 } },
    { degrees: 90, calibration: { rx: 20000, pixels_per_second: 60 } },
    { degrees: 90, calibration: { rx: 20000, degrees_per_second: 60 }, capture_after: false, capture_settle_ms: 0 },
  ]) {
    t.clearCalls(); const r = await call(args);
    check(r.isError && !t.callsOf("pad").length && !t.callsOf("json").length && !t.callsOf("capture").length,
      `invalid/unsafe calibration or duration refused before input: ${JSON.stringify(args)}`, r.text);
  }
  t.clearCalls(); let r = await call({ degrees: 90, calibration: { rx: 20000, degrees_per_second: 60 } });
  const cap = t.callsOf("capture").at(-1) ?? [];
  check(!r.isError && !!r.image && t.callsOf("pad").length === 1 && t.callsOf("capture").length === 1 && flag(cap, "--after-frames") === "0" &&
    r.json?.capture_after?.observation?.settle_ms_requested === 100 && r.json.pad_look?.angle_verified === false,
    "calibrated turn returns default backend image after100ms settle without retry or rendered-angle claim", r.text);
  t.setScenario({ replies: { pad: { ok: true, job_id: "look-job", job_state: "running" } } }); t.clearCalls();
  r = await call({ degrees: -90, calibration: { rx: 20000, degrees_per_second: 60 }, async: true, replace: true });
  const a = t.callsOf("pad").at(-1) ?? [];
  check(!r.isError && !!r.image && a.includes("--async") && a.includes("--replace") && r.json?.job_id === "look-job" && r.json.job_state === "running" &&
    r.json.capture_after?.observation?.action_state === "async_job_may_be_running", "async calibrated turn preserves running job and replacement flags; image is only an observation", r.text);
  await t.call("job_cancel", { pid, id: "look-job" });
  check(cmdOf(t.callsOf("job-cancel").at(-1) ?? [])[1] === "look-job", "calibrated async job keeps existing cancellation route");
  t.setScenario({ replies: { pad: { ok: false, error: "modeled owned refusal" } } }); t.clearCalls();
  r = await call({ degrees: 90, calibration: { rx: 20000, degrees_per_second: 60 } });
  check(r.isError && t.callsOf("pad").length === 1 && !t.callsOf("capture").length, "refused native action is never retried or followed by a misleading result image");
} finally { await t.close(); }
console.log(state.failures ? `pad look: FAIL (${state.failures})` : `pad look: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
