// One-job driving protocol/cleanup request; fake CLI only, no game physics.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ replies: { json: { ok: true, job_state: "completed" } } });
const call = async (args) => { try { return await t.call("pad_drive", { pid, ...args }); } catch (e) { return { isError: true, text: String(e) }; } };
const payload = () => { try { return JSON.parse(cmdOf(t.callsOf("json").at(-1) ?? [])[1]); } catch { return null; } };
const states = (p) => { try { return JSON.parse(p?.states_json ?? "[]"); } catch { return []; } };
try {
  check((await t.client.listTools()).tools.some((x) => x.name === "pad_drive"), "bounded driving helper advertised");
  let r = await call({ throttle: 170, steer: -12000, ms: 600, capture_after: false }), p = payload(), s = states(p);
  check(!r.isError && t.callsOf("json").length === 1 && p?.cmd === "pad-timeline" && p.pacing === "time" && p.ms === 600 && p.end_hold_ms === 0 && s.length === 2 && s[0]?.rt === 170 && s[0].lx === -12000 && s[0].lt === 0, "single leg becomes one finite time-paced timeline, no Present prerequisite");
  check(s.at(-1)?.at_ms === 600 && s.at(-1)?.rt === 0 && s.at(-1)?.lt === 0 && s.at(-1)?.lx === 0 && !t.callsOf("release").length, "explicit final neutral state and native job cleanup without separate release call");
  check(r.json?.pad_drive?.game_movement_verified === false && r.json.pad_drive.stopped_verified === false, "requested control and actual movement/stopping remain distinct");
  t.clearCalls(); r = await call({ phases: [{ throttle: 100, ms: 500 }, { brake: 200, ms: 300 }, { throttle: 80, steer: 20000, ms: 700 }], brake_after: { amount: 160, ms: 250 }, capture_after: false }); p = payload(); s = states(p);
  check(!r.isError && JSON.stringify(s.map((x) => x.at_ms)) === "[0,500,800,1500,1750]" && p?.ms === 1750 && s[1]?.rt === 0 && s[1]?.lt === 200 && s[2]?.lx === 20000 && s[3]?.lt === 160 && s[3]?.rt === 0 && s[3]?.lx === 0, "multi-phase accelerate/brake/turn/tail has no inter-call neutral gaps or inherited axes");
  check(t.callsOf("json").length === 1 && t.callsOf("pad").length === 0 && s.every((x) => x.buttons === "none" && x.ly === 0 && x.rx === 0 && x.ry === 0), "one owned job changes only steering and trigger controls");
  t.clearCalls(); r = await call({ throttle: 0, ms: 1, brake_after: true, capture_after: false }); p = payload(); s = states(p);
  check(!r.isError && p?.ms === 251 && s[1]?.at_ms === 1 && s[1]?.lt === 255 && s.at(-1)?.at_ms === 251, "explicit true tail requests LT255 for250ms; no automatic default tail");
  for (const bad of [{}, { ms: 0 }, { ms: 60001 }, { ms: 1, throttle: 256 }, { ms: 1, steer: 32768 }, { ms: 1, brake: -1 },
    { phases: [] }, { phases: [{ ms: 1 }], ms: 1 }, { phases: [{ ms: 1 }], throttle: 0 },
    { phases: [{ ms: 40000 }, { ms: 30000 }] }, { ms: 60000, brake_after: true },
    { ms: 1, brake_after: { amount: 255, ms: 0 } }, { ms: 1, brake_after: { amount: 256, ms: 1 } },
    { phases: [{ ms: 1, typo: 2 }] }, { ms: 1, max_width: 320, scale: 0.5 }, { ms: 1, capture_after: false, region: [0, 0, 1, 1] }]) {
    t.clearCalls(); r = await call({ capture_after: false, ...bad });
    check(r.isError && t.calls().length === 0, `invalid driving request fails before input: ${JSON.stringify(bad)}`);
  }
  t.clearCalls(); r = await call({ throttle: 100, ms: 1, capture_settle_ms: 0 });
  check(!r.isError && r.image && t.callsOf("json").length === 1 && t.callsOf("capture").length === 1 && r.json?.capture_after?.observation?.settle_ms_requested === 0, "default one post-action image, explicit zero settle accepted");
  t.setScenario({ replies: { json: { ok: true, job_id: "drive-job", job_state: "running" } } }); t.clearCalls();
  r = await call({ throttle: 100, ms: 1000, async: true, replace: true, capture_settle_ms: 0 }); p = payload();
  check(!r.isError && p?.async === true && p.replace === true && r.json?.job_id === "drive-job" && r.json.capture_after?.observation?.action_state === "async_job_may_be_running", "async drive retains job/replacement and honest concurrent-image semantics");
  await t.call("job_cancel", { pid, id: "drive-job" });
  check(cmdOf(t.callsOf("job-cancel").at(-1) ?? [])[1] === "drive-job", "existing owned cancellation route retained");
  t.setScenario({ replies: { json: { ok: false, error: "modeled busy refusal", _exit: 1 } } }); t.clearCalls();
  r = await call({ ms: 100 });
  check(r.isError && t.callsOf("json").length === 1 && !t.callsOf("capture").length, "native refusal is not retried or followed by a misleading image");
} finally { await t.close(); }
console.log(`pad drive: ${state.passes} PASS / ${state.failures} FAIL`);
process.exit(state.failures ? 1 : 0);
