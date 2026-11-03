// (J) owned input jobs: async key / pad, job_status / job_cancel, pulse, pad_ramp, pad_timeline, hold_while.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const t = await startFake({ replies: { key: { ok: true, job_id: "j1", job_state: "running" }, pad: { ok: true, job_id: "j2" },
  json: { ok: true, job_id: "j3" }, pulse: { ok: true, job_id: "j4" } } });
const last = (name) => cmdOf(t.callsOf(name).at(-1) ?? []);
const jsonPayload = () => { const a = last("json"); try { return JSON.parse(a[1]); } catch { return null; } };
try {
  const tools = (await t.client.listTools()).tools;
  const names = new Set(tools.map((x) => x.name));
  for (const n of ["job_status", "job_cancel", "pulse", "pad_ramp", "pad_timeline", "hold_while"]) check(names.has(n), `tool ${n} is registered`);
  const keyProps = tools.find((x) => x.name === "key")?.inputSchema?.properties ?? {};
  check(!!keyProps.async && !!keyProps.replace, "key schema has async and replace");

  // ---- key async
  let r = await t.call("key", { pid, key: "w", hold_ms: 2000, async: true });
  check(!r.isError && last("key").join(" ") === "key w 2000 --async", "key async passes --async with the hold", JSON.stringify(last("key")));
  check(r.json?.job_id === "j1" && /job_status/.test(String(r.json?.note)), "async reply surfaces job_id and a job_status hint", r.text.slice(0, 300));
  r = await t.call("key", { pid, key: "w", hold_ms: 500 });
  check(!r.isError && last("key").join(" ") === "key w 500" && r.json?.note === undefined, "without async: unchanged argv, no job note", JSON.stringify(last("key")));
  t.clearCalls();
  r = await t.call("key", { pid, key: "w", async: true });
  check(r.isError && t.callsOf("key").length === 0, "async without hold_ms is refused before anything is sent", r.text.slice(0, 200));
  r = await t.call("key", { pid, key: "w", hold_ms: 100, count: 3, async: true });
  check(r.isError && t.callsOf("key").length === 0, "async with count > 1 is refused", r.text.slice(0, 200));
  r = await t.call("key", { pid, key: "w", hold_ms: 100, async: true, replace: true });
  check(last("key").includes("--replace"), "replace is passed through", JSON.stringify(last("key")));

  // ---- pad async
  r = await t.call("pad", { pid, buttons: "a", ms: 800, lx: 1000, async: true });
  check(!r.isError && last("pad").join(" ") === "pad a 800 --lx 1000 --async" && r.json?.job_id === "j2", "pad async", JSON.stringify(last("pad")));
  t.clearCalls();
  r = await t.call("pad", { pid, buttons: "a", hold: true, async: true });
  check(r.isError && t.callsOf("pad").length === 0, "pad async with hold is refused", r.text.slice(0, 200));

  // ---- job_status / job_cancel
  r = await t.call("job_status", { pid, id: "j1" });
  check(!r.isError && last("job-status").join(" ") === "job-status j1", "job_status id -> job-status ID");
  r = await t.call("job_status", { pid });
  check(!r.isError && last("jobs").join(" ") === "jobs", "job_status without id -> jobs");
  r = await t.call("job_cancel", { pid, id: "j1" });
  check(!r.isError && last("job-cancel").join(" ") === "job-cancel j1", "job_cancel -> job-cancel ID");

  // ---- pulse
  r = await t.call("pulse", { pid, key: "a", ms: 3000, duty: 30, period_ms: 200, async: true });
  check(!r.isError && last("pulse").join(" ") === "pulse a 3000 --duty 30 --period-ms 200 --async" && r.json?.job_id === "j4", "pulse argv", JSON.stringify(last("pulse")));

  // ---- pad_ramp (sent as one json command)
  r = await t.call("pad_ramp", { pid, axis: "lx", from: 0, to: 16000, ms: 1000, ly: 20000, buttons: "rt", end_hold_ms: 100, pacing: "time", async: true });
  let p = jsonPayload();
  check(!r.isError && p?.cmd === "pad-ramp" && p.axis === "lx" && p.from === 0 && p.to === 16000 && p.ms === 1000 && p.ly === 20000 &&
    p.buttons === "rt" && p.end_hold_ms === 100 && p.pacing === "time" && p.async === true && p.lx === undefined, "pad_ramp json payload", JSON.stringify(p));
  t.clearCalls();
  r = await t.call("pad_ramp", { pid, axis: "lt", from: 0, to: 300, ms: 100 });
  check(r.isError && t.callsOf("json").length === 0, "trigger ramp out of 0..255 is refused", r.text.slice(0, 200));
  r = await t.call("pad_ramp", { pid, axis: "lx", from: 0, to: 100, ms: 100, lx: 5 });
  check(r.isError && t.callsOf("json").length === 0, "the ramped axis given as a constant is refused", r.text.slice(0, 200));

  // ---- pad_timeline
  const states = [{ at_ms: 0, ly: 20000 }, { at_ms: 400, lx: 16000, ly: 20000 }, { at_ms: 900 }];
  r = await t.call("pad_timeline", { pid, states, strict_frames: true, end_hold_ms: 50 });
  p = jsonPayload();
  check(!r.isError && p?.cmd === "pad-timeline" && JSON.stringify(JSON.parse(p.states_json)) === JSON.stringify(states) && p.strict_frames === true && p.end_hold_ms === 50,
    "pad_timeline sends states_json", JSON.stringify(p));
  t.clearCalls();
  r = await t.call("pad_timeline", { pid, states: [{ at_ms: 10 }] });
  check(r.isError && t.callsOf("json").length === 0, "first state not at 0 is refused", r.text.slice(0, 200));
  r = await t.call("pad_timeline", { pid, states: [{ at_ms: 0 }, { at_ms: 0 }] });
  check(r.isError && t.callsOf("json").length === 0, "non-increasing at_ms is refused", r.text.slice(0, 200));
  r = await t.call("pad_timeline", { pid, states: [{ at_ms: 0 }], strict_frames: true, pacing: "time" });
  check(r.isError && t.callsOf("json").length === 0, "strict_frames with time pacing is refused", r.text.slice(0, 200));

  // ---- hold_while
  r = await t.call("hold_while", { pid, key: "w", until_text: "Village", timeout_ms: 4000, region: { x: 0, y: 0, w: 100, h: 50 } });
  check(!r.isError && last("hold-while").join(" ") === "hold-while w --until-text Village --timeout-ms 4000 --region 0,0,100,50", "hold_while key argv", JSON.stringify(last("hold-while")));
  r = await t.call("hold_while", { pid, pad_buttons: "a", ly: 20000, until_text: "Done" });
  check(!r.isError && last("pad-hold-while").join(" ") === "pad-hold-while a --until-text Done --ly 20000", "hold_while pad argv", JSON.stringify(last("pad-hold-while")));
  t.clearCalls();
  r = await t.call("hold_while", { pid, until_text: "x" });
  check(r.isError && t.calls().length === 0, "hold_while with neither key nor pad input is refused", r.text.slice(0, 200));
  r = await t.call("hold_while", { pid, key: "w", pad_buttons: "a", until_text: "x" });
  check(r.isError && t.calls().length === 0, "hold_while with both key and pad input is refused", r.text.slice(0, 200));

  // ---- an older DLL: the CLI's refusal is passed through as an error
  t.setScenario({ replies: { key: { ok: false, input_injected: false, job_support_checked: true, error: "target does not support input jobs; restart the owned target with the matching updated DLL" } } });
  r = await t.call("key", { pid, key: "w", hold_ms: 1000, async: true });
  check(r.isError && /input jobs/.test(r.text) && r.json?.note === undefined, "unsupported jobs: error passed through, no job note", r.text.slice(0, 300));
} finally { await t.close(); }
console.log(state.failures ? `jobs: FAIL (${state.failures})` : `jobs: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
