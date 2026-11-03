// Fix 5: status is compact by default (essentials + non-zero counters + warnings); verbose:true is the full reply.
import { fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const busy = fullStatus({}, { wm_key: 12, gaks: 340, di_kbd: 0, act_synth: 1, present: 900 });
const t = await startFake({ replies: { status: busy } });
try {
  const full = JSON.stringify(busy).length;
  const r = await t.call("status", { pid });
  const j = r.json ?? {};
  check(!r.isError && j.ok === true, "status ok", r.text.slice(0, 200));
  check(r.text.length < full / 2 && r.text.length < 1300, `default reply is compact (${r.text.length} bytes vs ${full} full)`);
  check(j.pid === 4242 && j.hwnd === "0x1A2B", "pid and hwnd");
  check(j.client?.w === 1280 && j.client?.h === 720, "client size", JSON.stringify(j.client));
  check(j.really_foreground === false && j.app_active === 1, "really_foreground and app_active");
  check(j.present === 900, "present count", String(j.present));
  check(j.cursor === "auto", "cursor mode by name", String(j.cursor));
  check(JSON.stringify(j.counters) === JSON.stringify({ wm_key: 12, gaks: 340, act_synth: 1, present: 900, present_ours: 500 }) || (j.counters?.wm_key === 12 && j.counters?.gaks === 340 && j.counters?.act_synth === 1 && !("di_kbd" in j.counters) && !("rid" in j.counters)),
    "only non-zero counters", JSON.stringify(j.counters));
  check(Array.isArray(j.warnings) && j.warnings.length === 0, "warnings is an (empty) array when healthy", JSON.stringify(j.warnings));
  check(!("frames_by_api" in j) && !("vk" in j) && !("d3d12" in j), "the bulky blocks are gone");

  const v = await t.call("status", { pid, verbose: true });
  check(v.json?.frames_by_api && v.json?.counters?.rid === 0 && v.json?.vk, "verbose:true returns the full reply", v.text.slice(0, 120));

  // warnings
  t.setScenario({ replies: { status: fullStatus({ app_active: 0, hwnd: "0x0" }, { present: 0, present_ours: 0, fs_refused: 40 }) } });
  const w = await t.call("status", { pid });
  const ws = (w.json?.warnings ?? []).join(" | ");
  check(/fullscreen/i.test(ws) && /windowed|borderless/i.test(ws), "warning: fullscreen refusals + nothing presented", ws);
  check(/app_active/i.test(ws) && /autoactivate|activate/i.test(ws), "warning: app_active is 0 while in the background", ws);
  check(/window/i.test(ws) && /hwnd|no window|adopted/i.test(ws), "warning: no window adopted (hwnd 0x0)", ws);

  t.setScenario({ replies: { status: fullStatus({ minimized: true, hooks_failed: "Present" }) } });
  const m = await t.call("status", { pid });
  const ms = (m.json?.warnings ?? []).join(" | ");
  check(/minimi[sz]ed/i.test(ms), "warning: minimised", ms);
  check(/hook/i.test(ms) && /Present/.test(ms), "warning: failed hooks listed", ms);

  t.setScenario({ replies: { status: { ok: false, error: "boom" } } });
  const e = await t.call("status", { pid });
  check(e.isError && e.json?.ok === false, "an error reply is passed through");
} finally { await t.close(); }
console.log(state.failures ? `status: FAIL (${state.failures})` : `status: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
