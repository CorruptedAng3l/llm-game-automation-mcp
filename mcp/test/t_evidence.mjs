// (L) polled click / release evidence and edge_contact passed through, owned placement drag with its fallback,
// config edge_margin / fullscreen, and the capabilities in the compact status.
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const val = (argv, flag) => { const i = argv.indexOf(flag); return i < 0 ? undefined : argv[i + 1]; };
const t = await startFake({ replies: {
  click: { ok: true, di_poll_mode: "data", di_poll_confirmed: true, di_release_confirmed: true, edge_contact: false, unrelated: 1 },
  mouse: { ok: true, edge_contact: true, edge_sides: 1, edge_guard_applied: true },
  "placement-drag": { ok: true, job_id: "p1", di_poll_confirmed: true, di_release_confirmed: true },
  status: fullStatus({ capture_output_version: 1, input_precision: { edge_guard: true, release_poll: true, placement_drag: true },
    edge_margin: 8, edge_contact: true, edge_sides: 2, fullscreen: { policy: "borderless", requests: 0 } }),
} });
try {
  let r = await t.call("click", { pid, x: 50, y: 60, di_poll_timeout_ms: 300, di_release_timeout_ms: 200 });
  const c = t.callsOf("click").at(-1);
  check(val(c, "--di-poll-timeout-ms") === "300" && val(c, "--di-release-timeout-ms") === "200", "click passes the poll / release caps", JSON.stringify(c));
  check(r.json?.evidence?.di_poll_confirmed === true && r.json?.evidence?.di_release_confirmed === true && r.json?.evidence?.unrelated === undefined,
    "click reply carries evidence{di_poll_confirmed, di_release_confirmed}", JSON.stringify(r.json?.evidence));

  r = await t.call("move", { pid, x: 0, y: 5 });
  check(r.json?.evidence?.edge_contact === true && r.json?.evidence?.edge_guard_applied === true, "move reply carries edge_contact / edge_guard_applied", JSON.stringify(r.json?.evidence));

  // ---- placement drag
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 10, y1: 20, x2: 300, y2: 200, placement: true, hold_start_ms: 100, hold_end_ms: 700, ms: 300, steps: 10, poll_timeout_ms: 500 });
  const words = t.calls().map((a) => cmdOf(a)[0]).filter((w) => ["placement-drag", "mousedown", "mouse", "mouseup"].includes(w));
  const p = t.callsOf("placement-drag").at(-1) ?? [];
  check(!r.isError && words.join() === "placement-drag", "placement:true runs ONE owned placement-drag (no press / move / release)", words.join());
  check(cmdOf(p).slice(1, 5).join(" ") === "10 20 300 200" && val(p, "--steps") === "10" && val(p, "--ms") === "300" && val(p, "--hold-ms") === "100" &&
    val(p, "--dwell-ms") === "700" && val(p, "--button") === "left" && val(p, "--poll-timeout-ms") === "500", "placement-drag argv", JSON.stringify(p));
  check(r.json?.placement === true && r.json?.evidence?.di_poll_confirmed === true, "placement reply: placement true + evidence", r.text.slice(0, 300));

  t.setScenario({ replies: { "placement-drag": { ok:false,input_injected:false,placement_preflight_refused:true,error: "target lacks owned polled placement support; restart the owned target with the matching updated DLL; no input was sent" } } });
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4, placement: true, hold_start_ms: 0, hold_end_ms: 0 });
  const w2 = t.calls().map((a) => cmdOf(a)[0]).filter((w) => ["placement-drag", "mousedown", "mouse", "mouseup"].includes(w));
  check(!r.isError && w2.join() === "placement-drag,mousedown,mouse,mouseup" && /preflight.*before/.test(String(r.json?.placement_fallback)),
    "typed pre-input refusal permits one plain drag and explains its scope", `${w2.join()} ${r.text.slice(0, 200)}`);
  t.setScenario({ replies: { "placement-drag": { ok: false, job_state: "failed", job_reason: "button_release_not_observed", error: "placement release not observed before poll timeout" } } });
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4, placement: true });
  check(r.isError && t.callsOf("mousedown").length === 0 && /not observed/.test(r.text), "a placement failure after the drag started is reported, not retried", r.text.slice(0, 200));
  // An absent DirectInput read does not prove that another reader ignored the press.
  t.setScenario({ replies: { "placement-drag": { ok: false, job_state: "failed", job_reason: "button_down_not_observed" } } });
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4, placement: true, hold_start_ms: 0, hold_end_ms: 0 });
  const w3 = t.calls().map((a) => cmdOf(a)[0]).filter((w) => ["placement-drag", "mousedown", "mouse", "mouseup"].includes(w));
  check(r.isError&&w3.join()==='placement-drag'&&r.json.placement_warning&&r.json.job_reason==='button_down_not_observed',
    'unobserved placement press retains its receipt without another drag',`${w3.join()} ${r.text.slice(0,240)}`);
  t.setScenario({ replies: { "placement-drag": { ok: false, job_state: "failed", job_reason: "start_position_not_observed" } } });
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4, placement: true, hold_start_ms: 0, hold_end_ms: 0 });
  check(r.isError&&t.callsOf('mousedown').length===0&&r.json.job_reason==='start_position_not_observed','incomplete start-position failure cannot authorize another drag',r.text.slice(0,200));
  for(const bad of [{ok:false,error:'target lacks something',input_injected:false},{ok:false,error:'timeout',diagnostic:{job_reason:'button_down_not_observed'}},{ok:false,job_complete:false,button_released:false,job_reason:'button_down_not_observed'}]){
    t.setScenario({replies:{'placement-drag':bad}});t.clearCalls();r=await t.call('drag',{pid,x1:1,y1:2,x2:3,y2:4,placement:true,hold_start_ms:0,hold_end_ms:0});
    check(r.isError&&t.callsOf('mousedown').length===0&&t.callsOf('placement-drag').length===1,'reason strings and partial receipts cannot authorize a plain-drag replay');
  }
  t.clearCalls();
  await t.call("drag", { pid, x1: 1, y1: 2, x2: 3, y2: 4, hold_start_ms: 0, hold_end_ms: 0 });
  check(t.callsOf("placement-drag").length === 0 && t.callsOf("mousedown").length === 1, "default drag (no placement) is unchanged");

  // ---- config edge_margin / fullscreen
  t.setScenario({ replies: { config: fullStatus({ edge_margin: 12, fullscreen: { policy: "refuse" } }) } });
  r = await t.call("config", { pid, edge_margin: 12, fullscreen: "refuse" });
  const cfg = cmdOf(t.callsOf("config").at(-1));
  check(cfg.includes("edge_margin=12") && cfg.includes("fullscreen=refuse"), "config passes edge_margin and fullscreen", JSON.stringify(cfg));
  check(!r.isError && r.json?.config?.edge_margin === 12 && r.json?.config?.fullscreen === "refuse" && r.json?.warnings === undefined, "config reports them as in effect", r.text.slice(0, 300));
  t.setScenario({ replies: { config: fullStatus({ edge_margin: 8, fullscreen: { policy: "borderless" } }) } });
  r = await t.call("config", { pid, edge_margin: 12 });
  check(Array.isArray(r.json?.warnings) && /edge_margin=12/.test(r.json.warnings[0]), "a value that did not take effect is warned about", r.text.slice(0, 300));

  // ---- compact status
  t.setScenario({ replies: { status: fullStatus({ capture_output_version: 1, input_precision: { edge_guard: true, release_poll: true, placement_drag: true },
    edge_margin: 8, edge_contact: true, edge_sides: 2, fullscreen: { policy: "borderless", requests: 0 } }) } });
  r = await t.call("status", { pid });
  check(r.json?.capabilities?.capture_output_version === 1 && r.json?.capabilities?.placement_drag === true, "status shows capabilities", JSON.stringify(r.json?.capabilities));
  check(r.json?.edge_contact === true && r.json.warnings.some((w) => /edge_contact/.test(w)), "status shows edge_contact with a warning", JSON.stringify(r.json?.warnings));
  check(r.json?.fullscreen?.policy === "borderless", "status shows the fullscreen policy", JSON.stringify(r.json?.fullscreen));
  t.setScenario({ replies: { status: fullStatus() } });
  r = await t.call("status", { pid });
  check(r.json?.capabilities === undefined && r.json?.edge_contact === undefined, "an older status: no capabilities / edge fields invented");
} finally { await t.close(); }
console.log(state.failures ? `evidence: FAIL (${state.failures})` : `evidence: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
