// MCP protocol/argv model only: no real launch, Steam, native CLI or input.
import { makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
// Restricted oracle for this generated launch's single args option, copied from
// native SplitArgs/GetOpt behavior. Not a general CLI/global-option parser:
// inline splits at the first '='; otherwise a next token starting '--' with
// length > 2 is NOT a value, and args retains the default string "true".
// args is not among SplitArgs' boolean exemptions. Legal separate forms work.
const nativeArgsValue = (argv) => {
  const i = argv.findIndex((x) => x === "--args" || x.startsWith("--args="));
  if (i < 0) return undefined;
  const eq = argv[i].indexOf("=");
  if (eq >= 0) return argv[i].slice(eq + 1);
  const next = argv[i + 1];
  return next !== undefined && !(next.length > 2 && next.startsWith("--")) ? next : "true";
};
const t = await startFake({ replies: { launch: { ok: true, pid: process.pid, injected: true }, status: { ok: true, client_w: 1280, client_h: 720 } } });
try {
  const cases = [
    ["exe dash-prefixed Chromium switches", { exe: "C:/Games/Test/test.exe" }, "--disable-backgrounding-occluded-windows --disable-renderer-backgrounding --disable-background-timer-throttling --disable-features=CalculateNativeWinOcclusion"],
    ["Steam dash-prefixed switches", { steam_appid: "1964200", wait_exe: "OwnedTest.exe" }, "--disable-background-timer-throttling --disable-renderer-backgrounding"],
    ["embedded quotes, spaces, equals and backslashes", { exe: "C:/Games/Test/test.exe" }, String.raw`--profile="C:\Folder With Spaces\a=b.json" --label="two words"`],
    ["target switches resembling global CLI flags", { exe: "C:/Games/Test/test.exe" }, "--pid 42 --owner target-owner --json"],
    ["single-dash compatibility", { exe: "C:/Games/Test/test.exe" }, "-windowed -width 1280 -height 720"],
    ["explicit empty tail", { exe: "C:/Games/Test/test.exe" }, ""],
    ["leading-space compatibility", { exe: "C:/Games/Test/test.exe" }, " --disable-renderer-backgrounding"],
  ];
  for (const [label, launch, args] of cases) {
    t.clearCalls();
    const r = await t.call("launch", { ...launch, args });
    const argv = t.callsOf("launch").at(-1) ?? [];
    check(!r.isError && nativeArgsValue(argv) === args,
      `${label}: native args consumer receives the original tail unchanged`, JSON.stringify({ argv, decoded: nativeArgsValue(argv), expected: args }));
    if (launch.steam_appid) check(argv.includes("--steam") && argv[argv.indexOf("--steam") + 1] === launch.steam_appid && argv.includes("--wait-exe"),
      "Steam launch selection and wait executable remain separate CLI options", JSON.stringify(argv));
  }
  t.clearCalls();
  await t.call("launch", { exe: "C:/Games/Test/test.exe" });
  check(nativeArgsValue(t.callsOf("launch").at(-1) ?? []) === undefined, "omitted args stay omitted");
  const instructions = t.client.getInstructions() ?? "";
  check(!instructions.includes("returns the next frame") && instructions.includes("PrintWindow") && instructions.includes("does not prove a new rendered frame") && instructions.includes("capture_after:0"),
    "shared advice distinguishes a PrintWindow observation from a rendered-frame guarantee", instructions);
  const { tools } = await t.client.listTools();
  const burst = tools.find((x) => x.name === "capture_burst")?.description ?? "";
  check(burst.includes("frames[]") && !burst.includes("tiles[]"), "capture_burst names the actual response array", burst);
} finally { await t.close(); }
console.log(state.failures ? `pr1 args/advice: FAIL (${state.failures})` : `pr1 args/advice: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
