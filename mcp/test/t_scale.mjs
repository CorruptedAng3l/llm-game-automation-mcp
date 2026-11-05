// (C) capture max_width / scale / zoom (done in TS, mapping returned) and coord_space:"capture" on click / move / drag.
import { makeChecker, startFake } from "./lib.mjs";
const { decodePng } = await import("../build/png.js");
const { state, check } = makeChecker();
const pid = process.pid;
const scope = { hwnd:'0x123',thread:1,adoption_generation:'1',geometry_epoch:'0',lifetime_cookie:'99',client_w:640,client_h:360,geometry_witness:'client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch' };
const t = await startFake({ png: { w: 640, h: 360 }, replies:{json:{ok:true,pid,capture_space_updated:false,capture_scope:scope}} });
const returned = (r) => decodePng(Buffer.from(r.image.data, "base64"));
const px = (im, x, y) => { const o = (y * im.width + x) * 4; return [im.data[o], im.data[o + 1], im.data[o + 2]]; };
const near = (a, b, d = 1) => a.every((v, i) => Math.abs(v - b[i]) <= d);
const val = (argv, flag) => { const i = argv.indexOf(flag); return i < 0 ? undefined : argv[i + 1]; };
const positional = (argv, cmd) => { const rest = argv.slice(argv.indexOf(cmd) + 1); const i = rest.findIndex((x) => x.startsWith("--")); return (i < 0 ? rest : rest.slice(0, i)).filter((x) => !/^(left|right|middle)$/.test(x)); };
try {
  const props = (await t.client.listTools()).tools.find((x) => x.name === "capture")?.inputSchema?.properties ?? {};
  check(!!props.max_width && !!props.scale && !!props.zoom, "capture schema has max_width, scale and zoom");

  const half = await t.call("capture", { pid, scale: 0.5 });
  const h = returned(half);
  check(h.width === 320 && h.height === 180, "scale 0.5 halves the image", `${h.width}x${h.height}`);
  check(half.json?.image?.scale === 0.5 && half.json?.image?.w === 320 && half.json?.image?.offset_x === 0, "...and reports scale 0.5, w, h, offsets", JSON.stringify(half.json?.image));
  check(near(px(h, 10, 10), [20.5, 20.5, 77], 1), "downscale averages the covered pixels (client 20..21 -> 20.5)", JSON.stringify(px(h, 10, 10)));
  check(half.json?.w === 640, "the vinput reply still reports the real frame size (w 640)", String(half.json?.w));

  const mw = await t.call("capture", { pid, max_width: 320 });
  check(returned(mw).width === 320 && returned(mw).height === 180 && mw.json?.image?.scale === 0.5, "max_width 320 on a 640 frame = scale 0.5");
  const big = await t.call("capture", { pid, max_width: 2000 });
  check(returned(big).width === 640 && big.json?.image?.scale === 1, "max_width above the frame width never upscales");

  const z = await t.call("capture", { pid, region: { x: 100, y: 50, w: 200, h: 100 }, zoom: 3 });
  const zi = returned(z);
  check(zi.width === 600 && zi.height === 300, "region + zoom 3 = 3x crop", `${zi.width}x${zi.height}`);
  check(z.json?.image?.scale === 3 && z.json?.image?.offset_x === 100 && z.json?.image?.offset_y === 50, "zoom reports scale 3 with the region offset", JSON.stringify(z.json?.image));
  check(near(px(zi, 31, 16), [110, 55, 77], 0) && near(px(zi, 0, 0), [100, 50, 77], 0), "zoom is crisp (nearest-like): image (31,16) = client (110,55)", JSON.stringify(px(zi, 31, 16)));

  const both = await t.call("capture", { pid, scale: 0.5, zoom: 2 });
  check(both.isError === true && /one of/.test(both.text), "scale + zoom together is an explicit error", both.text.slice(0, 200));

  // zoom + max_width: the zoom is reduced so the returned image is never wider than max_width
  const zm = await t.call("capture", { pid, region: { x: 100, y: 50, w: 200, h: 100 }, zoom: 4, max_width: 600 });
  const zmi = returned(zm);
  check(!zm.isError && zmi.width === 600 && zmi.height === 300 && zm.json?.image?.scale === 3 && zm.json?.image?.offset_x === 100,
    "zoom 4 + max_width 600 on a 200 px region = 600 px wide (effective scale 3)", `${zm.text.slice(0, 160)} ${zmi?.width}x${zmi?.height}`);
  const zk = await t.call("capture", { pid, region: { x: 100, y: 50, w: 200, h: 100 }, zoom: 2, max_width: 1000 });
  check(!zk.isError && returned(zk).width === 400 && zk.json?.image?.scale === 2, "zoom 2 + max_width 1000: the cap does not bind, zoom stays 2");
  const sm = await t.call("capture", { pid, scale: 0.5, max_width: 300 });
  check(sm.isError === true && /one of/.test(sm.text), "scale + max_width together stays an explicit error", sm.text.slice(0, 200));
  const jz = await t.call("capture", { pid, format: "jpeg", zoom: 2 });
  check(jz.isError === true && /max_width/.test(jz.text) && /png/i.test(jz.text), "jpeg + zoom: the error names the fix (png + max_width)", jz.text.slice(0, 260));
  const jg = await t.call("capture", { pid, format: "jpeg", grid: 100 });
  check(jg.isError === true && /max_width|scale/.test(jg.text) && /png/i.test(jg.text), "jpeg + grid: the error names the fix (png + max_width / scale)", jg.text.slice(0, 260));
  t.clearCalls();
  const js = await t.call("capture", { pid, format: "jpeg", scale: 2 });
  check(js.isError && /scale > 1/.test(js.text) && /png/i.test(js.text) && t.callsOf("capture").length === 0,
    "jpeg enlargement is refused before capture instead of reporting an unapplied scale", js.text.slice(0, 260));

  const zg = await t.call("capture", { pid, region: { x: 100, y: 50, w: 200, h: 100 }, zoom: 2, grid: 50 });
  const zgi = returned(zg);
  check(zgi.width === 400 && !near(px(zgi, 100, 20), [150, 70, 77], 0) && near(px(zgi, 60, 20), [130, 60, 77], 0), "grid + zoom: line for client x=150 at image x=(150-100)*2=100", JSON.stringify([px(zgi, 100, 20), px(zgi, 60, 20)]));

  // ---- coord_space:"capture"
  t.clearCalls();
  const c = await t.call("click", { pid, x: 60, y: 40, coord_space: "capture" });
  const ca = t.callsOf("click").at(-1) ?? [];
  // last capture was the region+zoom2+grid one: client = offset + img/scale = (100 + 30, 50 + 20)
  check(!c.isError && JSON.stringify(positional(ca, "click")) === JSON.stringify(["130", "70"]), "click coord_space capture: image (60,40) -> client (130,70) via the last capture's mapping", JSON.stringify(ca));
  check(c.json?.mapped?.x === 130 && c.json?.mapped?.y === 70, "reply shows the mapped client coordinates", c.text.slice(0, 250));
  await t.call("move", { pid, x: 20, y: 10, coord_space: "capture" });
  check(JSON.stringify(positional(t.callsOf("mouse").at(-1), "mouse")) === JSON.stringify(["110", "55"]), "move maps too", JSON.stringify(t.callsOf("mouse").at(-1)));
  await t.call("drag", { pid, x1: 0, y1: 0, x2: 400, y2: 200, coord_space: "capture" });
  check(JSON.stringify(positional(t.callsOf('mousedown').at(-1),'mousedown'))===JSON.stringify(['100','50'])&&JSON.stringify(positional(t.callsOf('mouse').at(-1),'mouse'))===JSON.stringify(['300','150'])&&positional(t.callsOf('mouseup').at(-1),'mouseup').length===0,'drag maps press/move endpoints and releases without another coordinate move',JSON.stringify([t.callsOf('mousedown').at(-1),t.callsOf('mouse').at(-1),t.callsOf('mouseup').at(-1)]));

  t.clearCalls();
  await t.call("click", { pid, x: 60, y: 40 });
  check(JSON.stringify(positional(t.callsOf("click").at(-1), "click")) === JSON.stringify(["60", "40"]), "default (no coord_space) = client coordinates, unchanged");
  await t.call("click", { pid, x: 100, y: 50, coord_space: "capture", mapping: { scale: 0.5, offset_x: 0, offset_y: 0 } });
  const explicitArgs = t.callsOf("click").at(-1);
  check(JSON.stringify(positional(explicitArgs, "click")) === JSON.stringify(["200", "100"]) && JSON.parse(val(explicitArgs,"--scope-json")).lifetime_cookie === scope.lifetime_cookie,
    "an explicit mapping overrides the transform and carries a fresh native scope fence", JSON.stringify(explicitArgs));
  t.clearCalls();
  const mix = await t.call("click", { pid, x: 1, y: 1, coord_space: "capture", norm: true });
  check(mix.isError && t.callsOf("click").length === 0, "coord_space capture with norm is refused before anything is sent", mix.text.slice(0, 200));
} finally { await t.close(); }

// no capture yet -> explicit error, nothing sent
const t2 = await startFake({ png: { w: 640, h: 360 } });
try {
  const r = await t2.call("click", { pid, x: 5, y: 5, coord_space: "capture" });
  check(r.isError && /capture/.test(r.text) && t2.callsOf("click").length === 0, "coord_space capture before any capture: explicit error, nothing sent", r.text.slice(0, 250));
} finally { await t2.close(); }
console.log(state.failures ? `scale: FAIL (${state.failures})` : `scale: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
