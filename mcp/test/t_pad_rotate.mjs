// Protocol/trajectory construction only. No native XInput, game or desktop input.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ replies: { json: { ok: true, job_state: "completed" } } });
const call = async (a) => { try { return await t.call("pad_rotate", { pid, ...a }); } catch (e) { return { isError: true, json: null, text: String(e) }; } };
const payload = () => { try { return JSON.parse(cmdOf(t.callsOf("json").at(-1) ?? [])[1]); } catch { return null; } };
const points = (p) => { try { return JSON.parse(p?.states_json ?? "[]"); } catch { return []; } };
const base = { stick: "left", start_deg: 0, direction: "clockwise", turns: 1, ms: 1200, capture_after: false };
try {
  check((await t.client.listTools()).tools.some((x) => x.name === "pad_rotate"), "bounded stick-rotation tool is advertised");
  for (const args of [base, { ...base, stick: "right", direction: "counterclockwise", start_deg: 90, radius: 20000 }]) {
    t.clearCalls(); const r = await call(args), p = payload(), states = points(p), radius = args.radius ?? 32767;
    const [x, y, ox, oy] = args.stick === "left" ? ["lx", "ly", "rx", "ry"] : ["rx", "ry", "lx", "ly"];
    const sign = args.direction === "clockwise" ? -1 : 1;
    const winding = states.slice(1).every((s, i) => Math.sign(states[i][x] * s[y] - states[i][y] * s[x]) === sign);
    check(!r.isError && t.callsOf("json").length === 1 && !t.callsOf("pad").length && p?.cmd === "pad-timeline" && p.pacing === "time" && p.ms === 1200 && p.end_hold_ms === 50 &&
      states.length === 25 && states[0].at_ms === 0 && states.at(-1).at_ms === 1200 && states.every((s, i) => i === 0 || s.at_ms > states[i - 1].at_ms) &&
      states.every((s) => Math.abs(Math.hypot(s[x], s[y]) - radius) <= 2 && s[ox] === 0 && s[oy] === 0 && s.lt === 0 && s.rt === 0 && s.buttons === "none" && !Object.hasOwn(s, "phase")) && winding &&
      !Object.hasOwn(p, "stagger_ms") && r.json?.pad_rotate?.rotation_ms === 1200 && r.json.pad_rotate.end_dwell_ms === 50 && r.json.pad_rotate.total_ms === 1250 && r.json.pad_rotate.game_prompt_verified === false,
      `${args.stick} ${args.direction}: one normalized, correctly wound, finite 25-point timeline with explicit final dwell`, JSON.stringify(p));
    // Cardinal positions are independent fixed expectations, not the production trig formula.
    const expected = args.direction === "clockwise" ? [[0, radius], [radius, 0], [0, -radius], [-radius, 0], [0, radius]] : [[radius, 0], [0, radius], [-radius, 0], [0, -radius], [radius, 0]];
    check(expected.every(([ex, ey], i) => states[i * 6]?.[x] === ex && states[i * 6]?.[y] === ey), "start and quarter-turn axes match 0-up/90-right conventions", JSON.stringify(states));
    check(!!states.length && Math.hypot(states.at(-1)[x], states.at(-1)[y]) > 0 && !t.callsOf("release").length,
      "endpoint remains a non-neutral point for requested dwell; native job owns later cleanup");
  }
  t.clearCalls(); let r = await call({ ...base, turns: 0.25, ms: 300, end_dwell_ms: 100 });
  let p = payload(), states = points(p);
  check(!r.isError && states.length === 7 && states.at(-1)?.lx === 32767 && states.at(-1)?.ly === 0 && p?.end_hold_ms === 100 && r.json?.pad_rotate?.total_ms === 400,
    "fractional quarter-turn ends at right and reports rotation plus custom dwell", r.text);
  t.clearCalls(); r = await call({ ...base, turns: 4, ms: 60000, end_dwell_ms: 1000 }); p = payload(); states = points(p);
  check(!r.isError && states.length === 97 && Buffer.byteLength(JSON.stringify(p), "utf8") < 60000 && r.json?.pad_rotate?.total_ms === 61000,
    "maximum trajectory has bounded point/JSON/total-time size", r.text);
  for (const change of [
    { stick: "both" }, { direction: "up" }, { start_deg: 361 }, { turns: 0.1 }, { turns: 4.01 },
    { ms: 0 }, { ms: 60001 }, { ms: 23 }, { radius: 0 }, { radius: 32768 },
    { end_dwell_ms: 0 }, { end_dwell_ms: 1001 }, { capture_settle_ms: 0 },
  ]) {
    t.clearCalls(); const r = await call({ ...base, ...change });
    check(r.isError && !t.callsOf("json").length && !t.callsOf("pad").length && !t.callsOf("capture").length,
      `invalid trajectory refused before input: ${JSON.stringify(change)}`, r.text);
  }
  t.clearCalls(); const { capture_after: ignored, ...observe } = base; r = await call(observe);
  const cap = t.callsOf("capture").at(-1) ?? [], ci = cap.indexOf("--after-frames");
  check(!r.isError && !!r.image && t.callsOf("json").length === 1 && t.callsOf("capture").length === 1 && ci >= 0 && cap[ci + 1] === "0" &&
    r.json?.capture_after?.observation?.settle_ms_requested === 100, "rotation defaults to one post-reply image after100ms settle without a Present wait", r.text);
  t.setScenario({ replies: { json: { ok: true, job_id: "rotation-job", job_state: "running" } } }); t.clearCalls();
  r = await call({ ...observe, async: true, replace: true }); p = payload();
  check(!r.isError && !!r.image && p?.async === true && p.replace === true && r.json?.job_id === "rotation-job" && r.json.job_state === "running" &&
    r.json.capture_after?.observation?.action_state === "async_job_may_be_running", "async rotation keeps running job/replacement semantics; image is an observation", r.text);
  await t.call("job_cancel", { pid, id: "rotation-job" });
  check(cmdOf(t.callsOf("job-cancel").at(-1) ?? [])[1] === "rotation-job", "rotation uses existing cancellation route");
  t.setScenario({ replies: { json: { ok: false, error: "modeled native refusal" } } }); t.clearCalls(); r = await call(observe);
  check(r.isError && t.callsOf("json").length === 1 && !t.callsOf("capture").length, "failed timeline is not retried or followed by a misleading image");
} finally { await t.close(); }
console.log(state.failures ? `pad rotate: FAIL (${state.failures})` : `pad rotate: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
