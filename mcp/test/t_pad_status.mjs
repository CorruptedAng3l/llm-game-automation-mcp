// Existing native pad-return telemetry through actual MCP status handlers.
// Only fake CLI replies; no native input, devices or game processes.
import { fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const pad = (over = {}) => ({ active: true, publication: "12", virtual_returns: "9",
  standard_returns: "7", extended_returns: "2", native_calls_bypassed: "8",
  last_return_publication: "11", last_return_age_ms: 20, last_return_neutral: false,
  capabilities_virtualized: false, device_arrival_announced: false, application_ready: null, ...over });
const status = (p, version = 1) => fullStatus({ pad_readiness_version: version, virtual_pad: p });
const t = await startFake();
const read = async (s, verbose = false) => {
  t.setScenario({ replies: { status: s } }); t.clearCalls();
  return await t.call("status", { pid, verbose });
};
try {
  const raw = status(pad()), r = await read(raw);
  check(!r.isError && JSON.stringify(r.json?.virtual_pad) === JSON.stringify(raw.virtual_pad), "compact status preserves typed native completed-return telemetry");
  check(r.json?.virtual_pad?.standard_returns === "7" && r.json?.virtual_pad?.extended_returns === "2", "standard and extended returns stay distinct");
  check(r.json?.virtual_pad?.last_return_publication === "11" && r.json?.virtual_pad?.publication === "12", "last return may belong to older publication, without false current-poll claim");
  check(r.json?.virtual_pad?.capabilities_virtualized === false && r.json?.virtual_pad?.application_ready === null, "discovery and application readiness limits retained");
  check(t.calls().length === 1 && t.callsOf("status").length === 1, "diagnostic performs one read and sends no arm/input/claim");
  const zero = pad({ active: false, publication: "0", virtual_returns: "0", standard_returns: "0", extended_returns: "0", native_calls_bypassed: "0", last_return_publication: "0", last_return_age_ms: null, last_return_neutral: null });
  check(JSON.stringify((await read(status(zero))).json?.virtual_pad) === JSON.stringify(zero), "zero counts and unobserved nulls remain present");
  const huge = pad({ virtual_returns: "18446744073709551615", standard_returns: "18446744073709551613" });
  check((await read(status(huge))).json?.virtual_pad?.virtual_returns === huge.virtual_returns, "uint64 counts remain exact decimal strings");
  check((await read(status(pad({ arbitrary: { unsafe: "extra" } })))).json?.virtual_pad?.arbitrary === undefined, "unrecognized native fields omitted from compact projection");
  const legacy = fullStatus();
  check(!("virtual_pad" in (await read(legacy)).json), "legacy absence is unknown, not manufactured zero");
  for (const version of [undefined, 0, 2, "1"]) {
    const s = status(pad(), version); if (version === undefined) delete s.pad_readiness_version;
    check(!("virtual_pad" in (await read(s)).json), `unsupported or absent version ${version} is not relabeled`);
  }
  for (const [label, bad] of [
    ["null", null], ["array", []], ["active", pad({ active: 1 })],
    ["negative count", pad({ virtual_returns: "-1" })], ["numeric count", pad({ virtual_returns: 9 })],
    ["noncanonical count", pad({ virtual_returns: "09" })], ["overflow", pad({ virtual_returns: "18446744073709551616" })],
    ["missing field", (() => { const p = pad(); delete p.extended_returns; return p; })()],
    ["age", pad({ last_return_age_ms: -1 })], ["neutral", pad({ last_return_neutral: "false" })],
    ["readiness", pad({ application_ready: true })], ["discovery", pad({ capabilities_virtualized: "false" })],
    ["unobserved nonnull", pad({ virtual_returns: "0", last_return_age_ms: 1, last_return_neutral: false })],
    ["observed null", pad({ last_return_age_ms: null, last_return_neutral: null })],
  ]) check(!("virtual_pad" in (await read(status(bad))).json), `malformed ${label} telemetry stays unknown`);
  const verbose = await read(status(pad({ arbitrary: "raw" })), true);
  check(verbose.json?.virtual_pad?.arbitrary === "raw", "verbose status retains raw native diagnostics");
} finally { await t.close(); }
console.log(`pad status: ${state.passes} PASS / ${state.failures} FAIL`);
process.exit(state.failures ? 1 : 0);
