// SDK metadata/validation over the existing fake CLI only; no native/game input.
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid;
const t = await startFake({ replies: { pad: { ok: true } } });
const schemaOf = (tools, name) => tools.find((x) => x.name === name)?.inputSchema ?? {};
// Reused Zod constants may be represented as local JSON-Schema references.
const resolved = (root, schema) => {
  let value = schema;
  for (let n = 0; n < 8 && typeof value?.$ref === "string" && value.$ref.startsWith("#/"); n++) {
    const target = value.$ref.slice(2).split("/").reduce((at, key) => at?.[key.replace(/~1/g, "/").replace(/~0/g, "~")], root);
    if (!target) return value;
    const { $ref, ...own } = value; value = { ...target, ...own };
  }
  return value;
};
const triggerWords = (p) => typeof p?.description === "string" && /0\.\.255/.test(p.description) && /released/i.test(p.description) && /fully pressed/i.test(p.description);
const stickWords = (p) => typeof p?.description === "string" && /-32768\.\.32767/.test(p.description) && /neutral/i.test(p.description);
const trigger = (p) => p?.minimum === 0 && p?.maximum === 255 && triggerWords(p);
const stick = (p) => p?.minimum === -32768 && p?.maximum === 32767 && stickWords(p);
try {
  const tools = (await t.client.listTools()).tools;
  const pad = tools.find((x) => x.name === "pad"), padSchema = schemaOf(tools, "pad"), direct = padSchema.properties ?? {};
  check(/sticks -32768\.\.32767, triggers 0\.\.255/i.test(pad?.description ?? ""), "existing pad tool overview retains distinct numeric ranges");
  for (const axis of ["lt", "rt"]) check(trigger(resolved(padSchema, direct[axis])), `pad ${axis} metadata says byte trigger, released0/full255`);
  for (const axis of ["lx", "ly", "rx", "ry"]) check(stick(resolved(padSchema, direct[axis])), `pad ${axis} metadata says signed stick and neutral0`);
  const rampSchema = schemaOf(tools, "pad_ramp"), ramp = rampSchema.properties ?? {}, timelineSchema = schemaOf(tools, "pad_timeline");
  const states = resolved(timelineSchema, timelineSchema.properties?.states), timeline = resolved(timelineSchema, states?.items)?.properties ?? {};
  for (const [name, root, props] of [["pad_ramp constants", rampSchema, ramp], ["pad_timeline states", timelineSchema, timeline]]) {
    for (const axis of ["lt", "rt"]) check(trigger(resolved(root, props[axis])), `${name} ${axis} exposes 0..255 with trigger endpoints`);
    for (const axis of ["lx", "ly", "rx", "ry"]) check(stick(resolved(root, props[axis])), `${name} ${axis} exposes signed stick range`);
  }
  for (const endpoint of ["from", "to"]) check(/lt\/rt triggers use 0\.\.255/.test(ramp[endpoint]?.description ?? "") && /sticks use -32768\.\.32767/.test(ramp[endpoint]?.description ?? ""), `ramp ${endpoint} explains conditional trigger/stick ranges`);
  check(t.calls().length === 0, "listTools metadata inspection sends no CLI/input command");
  for (const axis of ["lt", "rt"]) {
    t.clearCalls();
    const bad = await t.call("pad", { pid, [axis]: 32000, capture_after: false });
    check(bad.isError && t.calls().length === 0, `${axis}32000 still refused before CLI, without implicit rescaling`);
  }
  t.clearCalls();
  const good = await t.call("pad", { pid, ms: 1, lx: -32768, ly: 32767, rx: 0, ry: 0, lt: 255, rt: 0, capture_after: false });
  const argv = cmdOf(t.callsOf("pad").at(-1) ?? []), at = (flag) => argv[argv.indexOf(flag) + 1];
  check(!good.isError && good.json?.ok === true && t.callsOf("pad").length === 1 && at("--lt") === "255" && at("--rt") === "0" && at("--lx") === "-32768" && at("--ly") === "32767", "existing byte-trigger and signed-stick endpoints forwarded unchanged");
} finally { await t.close(); }
console.log(`pad axis metadata: ${state.passes} PASS / ${state.failures} FAIL`);
process.exit(state.failures ? 1 : 0);
