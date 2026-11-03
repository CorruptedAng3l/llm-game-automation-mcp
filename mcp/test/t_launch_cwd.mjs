// Fix 3: launch has a `cwd` option (default stays the exe's own folder, which is the CLI's behaviour).
import { resolve } from "node:path";
import { makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const t = await startFake({ replies: { launch: { ok: true, pid: process.pid, injected: true } } });
try {
  const { tools } = await t.client.listTools();
  const props = tools.find((x) => x.name === "launch")?.inputSchema?.properties ?? {};
  check(!!props.cwd, "launch schema has cwd", Object.keys(props).join());

  const r = await t.call("launch", { exe: "C:/Games/X/x.exe", cwd: t.dir });
  const argv = t.callsOf("launch").at(-1) ?? [];
  const i = argv.indexOf("--cwd");
  check(!r.isError && i > 0 && argv[i + 1] === t.dir, "cwd becomes --cwd <folder>", JSON.stringify(argv));

  t.clearCalls();
  await t.call("launch", { exe: "C:/Games/X/x.exe" });
  check(!(t.callsOf("launch").at(-1) ?? []).includes("--cwd"), "no cwd given = no --cwd (the exe's folder stays the default)");

  t.clearCalls();
  const rel = await t.call("launch", { exe: "C:/Games/X/x.exe", cwd: "." });
  const a2 = t.callsOf("launch").at(-1) ?? [];
  check(a2[a2.indexOf("--cwd") + 1] === resolve("."), "a relative cwd is made absolute", JSON.stringify(a2));

  t.clearCalls();
  const miss = await t.call("launch", { exe: "C:/Games/X/x.exe", cwd: "C:/no/such/folder/lgam" });
  check(miss.isError === true && /cwd/.test(miss.text) && /exist/.test(miss.text) && t.callsOf("launch").length === 0, "a cwd that is not a folder is refused before vinput runs", miss.text);

  t.clearCalls();
  const aum = await t.call("launch", { aumid: "P.G_x!App", cwd: t.dir });
  check(aum.isError === true && /cwd/.test(aum.text) && t.callsOf("launch").length === 0, "cwd with aumid / steam_appid is refused (only exe launches take it)", aum.text);
} finally { await t.close(); }
console.log(state.failures ? `launch cwd: FAIL (${state.failures})` : `launch cwd: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
