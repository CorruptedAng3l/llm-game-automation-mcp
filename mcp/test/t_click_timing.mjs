// Fix 7 + (D): click hovers (hover_ms, default 80) and holds the press (hold_ms, default 140) so polling games / old menus see it.
import { makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const t = await startFake({});
const val = (argv, flag) => { const i = argv.indexOf(flag); return i < 0 ? undefined : argv[i + 1]; };
try {
  const props = (await t.client.listTools()).tools.find((x) => x.name === "click")?.inputSchema?.properties ?? {};
  check(!!props.hover_ms && !!props.hold_ms, "click schema has hover_ms and hold_ms");
  check(/200 ms/.test(props.hold_ms?.description ?? "") && /300/.test(props.hold_ms?.description ?? ""), "hold_ms description carries the measured numbers (<200 ms ignored, ~300 old menus)", props.hold_ms?.description);

  await t.call("click", { pid, x: 10, y: 20 });
  let a = t.callsOf("click").at(-1);
  check(val(a, "--settle-ms") === "80" && val(a, "--ms") === "140", "defaults: hover 80 ms before the press, press held 140 ms", JSON.stringify(a));
  await t.call("click", { pid, x: 10, y: 20, hover_ms: 250, hold_ms: 300 });
  a = t.callsOf("click").at(-1);
  check(val(a, "--settle-ms") === "250" && val(a, "--ms") === "300", "hover_ms / hold_ms override the defaults", JSON.stringify(a));
  await t.call("click", { pid, x: 1, y: 1, hover_ms: 0, hold_ms: 900 });
  a = t.callsOf("click").at(-1);
  check(val(a, "--settle-ms") === "0" && val(a, "--ms") === "900", "hover_ms 0 turns the extra hover off; hold_ms 900 for placement", JSON.stringify(a));
  await t.call("click", { pid });
  a = t.callsOf("click").at(-1);
  check(!a.includes("--settle-ms") && val(a, "--ms") === "140", "click at the current cursor: no hover to wait for, hold still applies", JSON.stringify(a));
  const bad = await t.call("click", { pid, x: 1, y: 1, hover_ms: 99999 });
  check(bad.isError === true, "hover_ms is range-checked");
} finally { await t.close(); }
console.log(state.failures ? `click timing: FAIL (${state.failures})` : `click timing: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
