// The fake vinput works and the server runs it through VINPUT_EXE=<script>: a plain tool call reaches it with
// --owner and --pid, and a dead pid gives the CLI's own "process gone" reply.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const t = await startFake({ replies: { ping: { ok: true, pong: 1 } } });
try {
  const r = await t.call("release", { pid: process.pid });
  check(!r.isError && r.json?.ok === true, "release reaches the fake vinput", r.text);
  const c = t.callsOf("release")[0] ?? [];
  check(c.includes("--owner") && c.includes("mcp-test") && c.includes(String(process.pid)) && cmdOf(c)[0] === "release", "argv carries --owner/--pid/release", JSON.stringify(c));
} finally { await t.close(); }
console.log(state.failures ? `harness: FAIL (${state.failures})` : `harness: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
