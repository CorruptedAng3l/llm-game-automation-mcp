// MCP wrapper over the existing read-only CLI, entirely fake replies.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const native = { ok: true, command: "doctor", pid, status_available: true, observed_channels: ["xinput"],
  claim: { active: true, owner: "someone-else" }, key_trace: { vk: 39 }, suggestions: ["Compare snapshots; counters are observations."] };
const t = await startFake({ replies: { doctor: native } });
const call = async (args) => { try { return await t.call("doctor", { pid, ...args }); } catch (e) { return { isError: true, text: String(e) }; } };
try {
  const tool = (await t.client.listTools()).tools.find((x) => x.name === "doctor");
  check(tool?.annotations?.readOnlyHint === true && /read.only/i.test(tool.description ?? ""), "doctor advertised as read-only diagnostics");
  let r = await call({});
  check(!r.isError && r.json?.command === "doctor" && r.json?.claim?.owner === "someone-else", "existing native diagnostic/foreign-owner receipt preserved");
  check(JSON.stringify(r.json?.suggestions) === JSON.stringify(native.suggestions), "native suggestions retained without claiming game acceptance");
  check(t.calls().length === 1 && JSON.stringify(cmdOf(t.calls()[0])) === JSON.stringify(["doctor"]), "default performs one doctor CLI command without input/injection/claim");
  t.clearCalls(); r = await call({ key: "right" });
  check(!r.isError && JSON.stringify(cmdOf(t.callsOf("doctor")[0] ?? [])) === JSON.stringify(["doctor", "--key", "right"]), "optional key trace forwarded as structured argv");
  for (const key of ["", "x".repeat(65), 39]) { t.clearCalls(); r = await call({ key }); check(r.isError && t.calls().length === 0, `invalid key refused before CLI: ${JSON.stringify(key)}`); }
  t.setScenario({ replies: { doctor: { ok: false, error: "status unavailable", status_available: false, _exit: 1 } } });
  t.clearCalls(); r = await call({});
  check(r.isError && r.json?.status_available === false && r.json?.error === "status unavailable", "native unavailable status remains a failure");
  check(t.callsOf("doctor").length === 1 && t.calls().length === 1, "failed/unsupported native doctor never falls back to injection or input");
} finally { await t.close(); }
console.log(`doctor: ${state.passes} PASS / ${state.failures} FAIL`);
process.exit(state.failures ? 1 : 0);
