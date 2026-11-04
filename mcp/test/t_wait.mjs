// Fake-SDK transport model only. The new wait tool itself must never launch the fake CLI.
import { fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ replies: { status: fullStatus() } });
const callWait = async (args) => { try { return await t.call("wait", args); } catch (e) { return { isError: true, json: null, text: String(e) }; } };
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
try {
  const tools = (await t.client.listTools()).tools, w = tools.find((x) => x.name === "wait");
  const schema = w?.inputSchema ?? {}, ms = schema.properties?.ms;
  check(!!w && ms?.minimum === 0 && ms?.maximum === 30000 && ms?.type === "integer" && schema.required?.includes("ms") && !schema.properties?.pid,
    "wait advertises required integer duration and no target within a 30-second bound", JSON.stringify(schema));
  check(w?.annotations?.readOnlyHint === true, "host timer is advertised as read-only");
  const zero = await callWait({ ms: 0 });
  check(!zero.isError && zero.json?.ok === true && zero.json.requested_ms === 0 && Number.isInteger(zero.json.elapsed_ms) && zero.json.elapsed_ms >= 0,
    "zero-duration wait reports explicit requested and actual elapsed time", zero.text);
  const short = await callWait({ ms: 25 });
  check(!short.isError && short.json?.requested_ms === 25 && Number.isInteger(short.json.elapsed_ms) && short.json.elapsed_ms >= 24 && /host timer only/.test(short.json.scope ?? ""),
    "positive wait measures monotonic elapsed time without claiming game progress", short.text);
  check(t.calls().length === 0, "standalone wait launches no CLI command");

  // A synchronous busy loop or target-side wait would prevent this status from completing first.
  let done = false;
  const pending = callWait({ ms: 3000 }).finally(() => { done = true; });
  await sleep(30);
  const status = await t.call("status", { pid });
  check(!status.isError && status.json?.ok === true && !done, "status completes concurrently while the wait timer is still pending", status.text);
  const long = await pending;
  check(!long.isError && long.json?.requested_ms === 3000 && long.json.elapsed_ms >= 2999, "pending timer completes normally after concurrent status", long.text);
  check(t.callsOf("status").length === 1 && t.calls().length === 1 && t.callsOf("wait").length === 0,
    "only the explicitly requested status uses CLI I/O; wait has none", JSON.stringify(t.calls()));

  for (const args of [{}, { ms: -1 }, { ms: 30001 }, { ms: 0.5 }, { ms: "1" }, { ms: null }, { ms: true }]) {
    const before = t.calls().length, r = await callWait(args);
    check(r.isError && t.calls().length === before, `invalid wait refused before CLI I/O: ${JSON.stringify(args)}`, r.text);
  }
} finally { await t.close(); }
console.log(state.failures ? `wait: FAIL (${state.failures})` : `wait: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
