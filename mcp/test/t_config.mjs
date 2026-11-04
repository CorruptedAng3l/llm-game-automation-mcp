// Fix 1: the `config` tool (typed enums / booleans, --owner, effective config in the reply).
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const t = await startFake({ replies: { config: fullStatus({ fakefocus: false, cursor_mode: 1, rawmode: 1, msgmode: 2 }) } });
try {
  const { tools } = await t.client.listTools();
  const cfg = tools.find((x) => x.name === "config");
  check(!!cfg, "config tool is listed");
  const props = cfg?.inputSchema?.properties ?? {};
  check(props.cursor?.enum?.join() === "auto,virtual,real", "cursor is an enum auto|virtual|real", JSON.stringify(props.cursor));
  check(props.raw?.enum?.join() === "auto,post,buffer,off", "raw is an enum auto|post|buffer|off", JSON.stringify(props.raw));
  check(props.msgs?.enum?.join() === "auto,on,off", "msgs is an enum auto|on|off", JSON.stringify(props.msgs));
  for (const b of ["fakefocus", "autoactivate", "isolate", "blockfocus", "nofullscreen", "keepvisible"])
    check(props[b]?.type === "boolean", `${b} is a boolean`, JSON.stringify(props[b]));

  const r = await t.call("config", { pid, fakefocus: false, cursor: "virtual", raw: "post", msgs: "off" });
  const argv = t.calls().at(-1) ?? [];
  const words = cmdOf(argv);
  check(!r.isError && words[0] === "config", "config runs `config`", r.text);
  check(argv.includes("--owner") && argv.includes("mcp-test"), "config passes --owner like the other tools", JSON.stringify(argv));
  check(words.includes("fakefocus=false") && words.includes("cursor=virtual") && words.includes("raw=post") && words.includes("msgs=off"), "settings become k=v words", JSON.stringify(words));
  check(r.json?.config?.fakefocus === false && r.json?.config?.cursor === "virtual" && r.json?.config?.raw === "post" && r.json?.config?.msgs === "off",
    "reply carries the effective config with names (not numbers)", r.text.slice(0, 400));
  check(r.text.length < 800, "reply is compact (not the 3 KB status)", String(r.text.length));
  check(!("warnings" in (r.json ?? {})), "no mismatch warning when the effective config matches");

  // requested value not in effect -> say so
  t.setScenario({ replies: { config: fullStatus({ cursor_mode: 0 }) } });
  const m = await t.call("config", { pid, cursor: "real" });
  check(Array.isArray(m.json?.warnings) && /cursor/.test(m.json.warnings.join(" ")), "a requested value that did not take effect is reported", m.text.slice(0, 400));

  // no settings = read-only query, still works
  t.clearCalls();
  const q = await t.call("config", { pid });
  check(!q.isError && cmdOf(t.calls().at(-1)).join(" ") === "config", "no settings = just read the config", q.text.slice(0, 200));

  // bounded physical-input opt-out: for_ms becomes --for, only with isolate:false
  t.setScenario({ replies: { config: fullStatus({ isolate: false }) } });
  t.clearCalls();
  const lease = await t.call("config", { pid, isolate: false, for_ms: 1000 });
  const leaseWords = cmdOf(t.calls().at(-1));
  check(!lease.isError && leaseWords.join(" ") === "config isolate=false --for 1000", "isolate:false + for_ms -> `config isolate=false --for 1000`", leaseWords.join(" "));
  check(props.for_ms?.type === "integer" && props.for_ms?.maximum === 120000, "for_ms is an integer 1..120000", JSON.stringify(props.for_ms));
  t.clearCalls();
  const noLease = await t.call("config", { pid, for_ms: 1000 });
  check(noLease.isError && t.calls().length === 0, "for_ms without isolate:false is refused before anything is sent", noLease.text.slice(0, 200));
  const trueLease = await t.call("config", { pid, isolate: true, for_ms: 1000 });
  check(trueLease.isError && t.calls().length === 0, "for_ms with isolate:true is refused", trueLease.text.slice(0, 200));

  // bad enum value rejected by the schema
  const bad = await t.call("config", { pid, cursor: "sideways" });
  check(bad.isError === true, "an invalid enum value is an error", bad.text.slice(0, 200));
} finally { await t.close(); }
console.log(state.failures ? `config: FAIL (${state.failures})` : `config: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
