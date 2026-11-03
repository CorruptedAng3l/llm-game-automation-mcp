// (F) edge-scroll guard: clicks / moves / drags / scrolls that start or end within edge_margin px of the client edge get a
// warning (games with edge scrolling pan the view while the cursor rests there); park_cursor moves the cursor to the centre.
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const t = await startFake({ replies: { status: fullStatus({ client_w: 1280, client_h: 720 }) } });
const warns = (r) => (r.json?.warnings ?? []).join(" | ");
const statusCalls = () => t.callsOf("status").length;
try {
  const props = (await t.client.listTools()).tools.reduce((m, x) => ({ ...m, [x.name]: x.inputSchema?.properties ?? {} }), {});
  check(["click", "move", "drag", "scroll"].every((n) => !!props[n].edge_margin), "click, move, drag, scroll have edge_margin");
  check(!!props.click.park_cursor && !!props.drag.park_cursor, "click and drag have park_cursor");

  let r = await t.call("click", { pid, x: 640, y: 360 });
  check(!r.isError && !("warnings" in (r.json ?? {})), "a click in the middle: no warning", r.text.slice(0, 200));
  r = await t.call("click", { pid, x: 3, y: 300 });
  check(!r.isError && /left edge/.test(warns(r)) && /scroll/i.test(warns(r)) && /park_cursor/.test(warns(r)), "x=3: warns about the left edge, edge scrolling and park_cursor", warns(r));
  r = await t.call("click", { pid, x: 1279, y: 719 });
  check(/right edge/.test(warns(r)) && /bottom edge/.test(warns(r)), "bottom-right corner: warns about both edges", warns(r));
  r = await t.call("click", { pid, x: 600, y: 2 });
  check(/top edge/.test(warns(r)), "y=2: top edge", warns(r));
  r = await t.call("click", { pid, x: 10, y: 300 });
  check(!("warnings" in (r.json ?? {})), "x=10 is outside the default 8 px margin: no warning");
  r = await t.call("click", { pid, x: 30, y: 300, edge_margin: 40 });
  check(/left edge/.test(warns(r)), "edge_margin 40 widens the zone", warns(r));
  r = await t.call("click", { pid, x: 1, y: 1, edge_margin: 0 });
  check(!("warnings" in (r.json ?? {})), "edge_margin 0 turns the guard off");
  check(r.json?.ok === true, "the click itself is still sent when it warns (the target may be in the margin on purpose)");

  r = await t.call("move", { pid, x: 640, y: 1 });
  check(/top edge/.test(warns(r)), "move is guarded", warns(r));
  r = await t.call("drag", { pid, x1: 640, y1: 360, x2: 1278, y2: 360, hold_start_ms: 0, hold_end_ms: 0 });
  check(/right edge/.test(warns(r)) && /end|destination|x2/i.test(warns(r)), "drag: the destination is checked and named", warns(r));
  r = await t.call("drag", { pid, x1: 2, y1: 360, x2: 600, y2: 360, hold_start_ms: 0, hold_end_ms: 0 });
  check(/left edge/.test(warns(r)) && /start|x1/i.test(warns(r)), "drag: the start is checked and named", warns(r));
  r = await t.call("drag", { pid, x1: 200, y1: 360, x2: 1000, y2: 400, hold_start_ms: 0, hold_end_ms: 0 });
  check(!("warnings" in (r.json ?? {})), "drag between two safe points: a straight path stays inside, no warning");
  r = await t.call("scroll", { pid, notches: 1, x: 5, y: 300 });
  check(/left edge/.test(warns(r)), "scroll at a position is guarded", warns(r));
  r = await t.call("click", { pid, x: 0.001, y: 0.5, norm: true });
  check(/left edge/.test(warns(r)), "norm coordinates are checked in pixels", warns(r));

  // parking
  t.clearCalls();
  r = await t.call("click", { pid, x: 100, y: 100, park_cursor: true });
  const mice = t.callsOf("mouse");
  check(mice.length === 1 && cmdOf(mice[0]).slice(0, 3).join(" ") === "mouse 640 360" && r.json?.parked?.x === 640 && r.json?.parked?.y === 360, "park_cursor: the cursor goes to the client centre afterwards", JSON.stringify(mice));
  t.clearCalls();
  await t.call("click", { pid, x: 100, y: 100 });
  check(t.callsOf("mouse").length === 0, "no parking unless asked");
  t.clearCalls();
  r = await t.call("drag", { pid, x1: 100, y1: 100, x2: 200, y2: 200, hold_start_ms: 0, hold_end_ms: 0, park_cursor: true });
  const order = t.calls().map((a) => cmdOf(a)[0]).filter((c) => c !== "status");
  check(order.join() === "mousedown,mouse,mouseup,mouse" && r.json?.parked?.x === 640, "drag + park_cursor: parks after the release", order.join());

  // size is cached, and an unreadable status never blocks the action
  const t2 = await startFake({ replies: { status: fullStatus() } });
  await t2.call("click", { pid, x: 1, y: 1 }); await t2.call("click", { pid, x: 1, y: 1 });
  check(t2.callsOf("status").length === 1, "the client size is fetched once and cached for a few seconds", String(t2.callsOf("status").length));
  await t2.close();
  const t3 = await startFake({ replies: { status: { ok: false, error: "boom" } } });
  const u = await t3.call("click", { pid, x: 1, y: 1 });
  check(!u.isError && !("warnings" in (u.json ?? {})), "status unreadable: the click goes ahead without a warning");
  await t3.close();
} finally { await t.close(); }
console.log(state.failures ? `edge: FAIL (${state.failures})` : `edge: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
