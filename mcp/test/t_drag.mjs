// (E) drag for placement: press, short hold, stepped move, dwell at the destination, release (always).
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const t = await startFake({});
const seq = () => t.timedCalls().filter((c) => ["mousedown", "mouse", "mouseup", "drag"].includes(cmdOf(c.argv)[0]));
const val = (argv, flag) => { const i = argv.indexOf(flag); return i < 0 ? undefined : argv[i + 1]; };
try {
  const props = (await t.client.listTools()).tools.find((x) => x.name === "drag")?.inputSchema?.properties ?? {};
  check(!!props.hold_start_ms && !!props.hold_end_ms && !!props.steps, "drag schema has hold_start_ms, hold_end_ms and steps", Object.keys(props).join());

  let r = await t.call("drag", { pid, x1: 10, y1: 20, x2: 300, y2: 200, hold_start_ms: 400, hold_end_ms: 600, steps: 12, ms: 240 });
  let s = seq();
  const words = s.map((c) => cmdOf(c.argv)[0]);
  check(!r.isError && words.join() === "mousedown,mouse,mouseup", "sequence: mousedown, stepped mouse move, mouseup (no single-shot drag)", words.join());
  check(cmdOf(s[0].argv).slice(0, 4).join(" ") === "mousedown left 10 20", "press happens at the start point", JSON.stringify(cmdOf(s[0].argv)));
  check(val(s[1].argv, "--steps") === "12" && val(s[1].argv, "--step-ms") === "20" && cmdOf(s[1].argv).slice(0, 3).join(" ") === "mouse 300 200", "the move is stepped: --steps 12, 20 ms apart (240 ms)", JSON.stringify(s[1].argv));
  check(cmdOf(s[2].argv).slice(0, 4).join(" ") === "mouseup left 300 200", "release happens at the destination", JSON.stringify(cmdOf(s[2].argv)));
  check(s[1].t - s[0].t >= 400, `the press is held before moving (>= 400 ms; saw ${s[1].t - s[0].t})`);
  check(s[2].t - s[1].t >= 600, `the cursor dwells at the destination before release (>= 600 ms; saw ${s[2].t - s[1].t})`);
  check(r.json?.hold_start_ms === 400 && r.json?.hold_end_ms === 600 && r.json?.steps === 12, "reply reports the timings used", r.text.slice(0, 300));

  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4 });
  s = seq();
  check(!r.isError && s.length === 3, "defaults still do the full press / move / release sequence");
  check(s[1].t - s[0].t >= 140 && s[2].t - s[1].t >= 240, "defaults: about 150 ms hold and 250 ms dwell", `${s[1].t - s[0].t} / ${s[2].t - s[1].t}`);
  check(Number(val(s[1].argv, "--steps")) >= 3, "default steps derived from the duration (>= 3)", JSON.stringify(s[1].argv));

  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4, hold_start_ms: 0, hold_end_ms: 0, button: "right" });
  s = seq();
  check(!r.isError && s.length === 3 && cmdOf(s[0].argv)[1] === "right" && cmdOf(s[2].argv)[1] === "right", "zero holds are allowed; button is passed to press and release");

  // the move fails: the button must still be released
  t.setScenario({ replies: { mouse: { ok: false, error: "virtual hover not rendered before timeout" } } });
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4, hold_start_ms: 0, hold_end_ms: 0 });
  s = seq();
  check(r.isError && s.map((c) => cmdOf(c.argv)[0]).join() === "mousedown,mouse,mouseup" && /hover/.test(r.text), "a failed move still releases the button and reports the failure", r.text.slice(0, 250));
  // the press fails: nothing else is sent
  t.setScenario({ replies: { mousedown: { ok: false, error: "no window" } } });
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4 });
  check(r.isError && seq().length === 1 && /no window/.test(r.text), "a failed press stops there (no move, no release needed)", r.text.slice(0, 250));
} finally { await t.close(); }
console.log(state.failures ? `drag: FAIL (${state.failures})` : `drag: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
