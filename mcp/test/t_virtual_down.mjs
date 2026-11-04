// Fake protocol/status-view model only: not a native key release or game-consumption proof.
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ replies: { status: fullStatus({ virtual_down: [] }) } });
const own = (o, k) => Object.prototype.hasOwnProperty.call(o ?? {}, k);
try {
  const empty = await t.call("status", { pid });
  check(!empty.isError && own(empty.json, "virtual_down") && Array.isArray(empty.json.virtual_down) && empty.json.virtual_down.length === 0,
    "typed empty native snapshot remains an explicit empty array", empty.text);

  t.setScenario({ replies: { status: fullStatus({ virtual_down: [1, 68, 165] }) } });
  const held = await t.call("status", { pid });
  check(!held.isError && JSON.stringify(held.json?.virtual_down) === "[1,68,165]", "held mouse/keyboard VK snapshot survives compact view", held.text);

  // The fake's finite key completion and later empty status are separate observations.
  // This is the SAME before/after MCP contract; it deliberately does not fabricate a real reader.
  t.setScenario({ replies: { key: { ok: true, job_id: "hold-d", job_state: "completed", accepted: false, input_injected: true },
    status: fullStatus({ virtual_down: [] }) } });
  const key = await t.call("key", { pid, key: "d", hold_ms: 150 });
  check(!key.isError && key.json?.job_state === "completed" && JSON.stringify(cmdOf(t.callsOf("key").at(-1) ?? [])) === '["key","d","150"]',
    "finite hold uses the existing key contract unchanged", key.text);
  const released = await t.call("status", { pid });
  check(!released.isError && own(released.json, "virtual_down") && JSON.stringify(released.json.virtual_down) === "[]",
    "post-completion empty native snapshot is retained, not dropped", released.text);

  for (const value of [null, {}, "[]", false, ["68"], [68, null], [0], [256], [-1], [68.5], [[68]]]) {
    t.setScenario({ replies: { status: fullStatus({ virtual_down: value }) } });
    const r = await t.call("status", { pid });
    check(!r.isError && !own(r.json, "virtual_down"), `malformed virtual snapshot omitted: ${JSON.stringify(value)}`, r.text);
  }
  const legacy = fullStatus();
  delete legacy.virtual_down; // fullStatus otherwise supplies [] by default.
  t.setScenario({ replies: { status: legacy } });
  const rawLegacy = await t.call("status", { pid, verbose: true });
  check(!rawLegacy.isError && !own(rawLegacy.json, "virtual_down"), "legacy fixture actually omits the native snapshot", rawLegacy.text);
  const absent = await t.call("status", { pid });
  check(!absent.isError && !own(absent.json, "virtual_down"), "old native reply without snapshot stays unknown", absent.text);
  t.setScenario({ replies: { status: fullStatus({ virtual_down: ["malformed"] }) } });
  const verbose = await t.call("status", { pid, verbose: true });
  check(!verbose.isError && JSON.stringify(verbose.json?.virtual_down) === '["malformed"]', "verbose native reply remains unchanged");
  const tools = (await t.client.listTools()).tools;
  check(/virtual_down/.test(tools.find((x) => x.name === "status")?.description ?? ""), "status advice describes the published snapshot");
} finally { await t.close(); }
console.log(state.failures ? `virtual-down: FAIL (${state.failures})` : `virtual-down: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
