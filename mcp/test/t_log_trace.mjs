// Fix 2: `log` (tail of vinput's per-pid log, optional substring filter) and `trace` (trace key <key>).
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const ring = [];
for (let i = 0; i < 60; i++) ring.push(`[${(i / 10).toFixed(3).padStart(7)}] hook ${i}`);
for (let i = 0; i < 500; i++) ring.push(`[${(10 + i / 100).toFixed(3).padStart(7)}] refused DXGI exclusive fullscreen`);
ring.push("[ 20.000] last line");
const t = await startFake({ replies: { log: { ok: true, log: ring } } });
try {
  const names = (await t.client.listTools()).tools.map((x) => x.name);
  check(names.includes("log") && names.includes("trace"), "log and trace are listed");

  const r = await t.call("log", { pid });
  check(!r.isError && cmdOf(t.calls().at(-1)).join(" ") === "log", "log runs `log`", r.text.slice(0, 200));
  check(Array.isArray(r.json?.lines) && r.json.lines.length <= 40 && r.json.lines.at(-1).includes("last line"), "default tail is at most 40 lines, newest last", r.text.slice(0, 300));
  check(r.json?.lines.some((l) => /refused DXGI/.test(l) && /500/.test(l)), "500 identical consecutive lines collapse into one with a repeat count", r.json?.lines.join("|").slice(0, 300));
  check(r.json?.total === ring.length, "total counts the lines before collapsing", String(r.json?.total));

  const f = await t.call("log", { pid, lines: 5, contains: "hook" });
  check(f.json?.lines.length === 5 && f.json.lines.every((l) => l.includes("hook")), "contains filters, lines caps (last 5 matches)", f.text.slice(0, 300));
  check(f.json?.lines.at(-1).includes("hook 59"), "filtered tail ends at the newest match");

  const none = await t.call("log", { pid, contains: "zzz-no-such" });
  check(!none.isError && none.json?.lines.length === 0 && none.json?.matched === 0, "no match = empty lines, not an error", none.text.slice(0, 200));

  // file source: older lines than the 200-line ring
  const file = join(t.dir, "vinput_x.log");
  writeFileSync(file, Array.from({ length: 300 }, (_, i) => `[${i}.000] file line ${i}`).join("\r\n") + "\r\n");
  t.setScenario({ replies: { status: fullStatus({ log_path: file }), log: { ok: true, log: ring } } });
  const fr = await t.call("log", { pid, source: "file", lines: 3 });
  check(fr.json?.source === "file" && fr.json.lines.length === 3 && fr.json.lines.at(-1).includes("file line 299"), "source:file tails the file named by status.log_path", fr.text.slice(0, 300));
  t.setScenario({ replies: { status: fullStatus({ log_path: undefined }) } });
  const nf = await t.call("log", { pid, source: "file" });
  check(nf.isError === true && /log_path/.test(nf.text), "source:file without a log_path is an explicit error", nf.text.slice(0, 200));

  t.setScenario({ replies: { trace: { ok: true, vk: 13, name: "enter", message_down: 2 } } });
  t.clearCalls();
  const tr = await t.call("trace", { pid, key: "enter" });
  const w = cmdOf(t.calls().at(-1));
  check(!tr.isError && w.join(" ") === "trace key enter", "trace runs `trace key enter`", JSON.stringify(w));
  check(tr.json?.message_down === 2, "trace returns vinput's reply");
  t.setScenario({ replies: { trace: { ok: false, error: "trace needs a valid key" } } });
  const bt = await t.call("trace", { pid, key: "nonsense" });
  check(bt.isError === true, "trace error is surfaced as isError");
} finally { await t.close(); }
console.log(state.failures ? `log/trace: FAIL (${state.failures})` : `log/trace: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
