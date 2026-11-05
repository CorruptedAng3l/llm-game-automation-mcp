// (K) capture: vinput's own scaling / JPEG (capture_output_version 1) with the TS scaling kept as the fallback, and
// capture_burst. image{scale, offset_x, offset_y} keeps the same meaning whoever scaled: image_px = (client_px - offset) * scale.
import { makeChecker, startFake } from "./lib.mjs";
const { decodePng } = await import("../build/png.js");
const { state, check } = makeChecker();
const pid = process.pid;
const val = (argv, flag) => { const i = argv.indexOf(flag); return i < 0 ? undefined : argv[i + 1]; };
const positional = (argv, cmd) => { const rest = argv.slice(argv.indexOf(cmd) + 1); const i = rest.findIndex((x) => x.startsWith("--")); return (i < 0 ? rest : rest.slice(0, i)).filter((x) => !/^(left|right|middle)$/.test(x)); };

const t = await startFake({ png: { w: 640, h: 360, core_capture: true } });
try {
  const props = (await t.client.listTools()).tools.find((x) => x.name === "capture")?.inputSchema?.properties ?? {};
  check(!!props.format && !!props.quality, "capture schema has format and quality");

  let r = await t.call("capture", { pid, scale: 0.5 });
  let cap = t.callsOf("capture").at(-1);
  check(val(cap, "--scale") === "0.5", "scale <= 1 is passed to vinput (--scale)", JSON.stringify(cap));
  let im = decodePng(Buffer.from(r.image.data, "base64"));
  check(im.width === 320 && r.json?.image?.scaled_by === "vinput" && r.json?.image?.scale === 0.5, "vinput scaled it: 320 wide, scaled_by vinput, scale 0.5", JSON.stringify(r.json?.image));

  r = await t.call("capture", { pid, max_width: 160, region: { x: 100, y: 40, w: 320, h: 180 } });
  cap = t.callsOf("capture").at(-1);
  check(val(cap, "--max-width") === "160" && val(cap, "--region") === "100,40,320,180", "max_width + region passed to vinput", JSON.stringify(cap));
  check(r.json?.image?.scale === 0.5 && r.json?.image?.offset_x === 100 && r.json?.image?.offset_y === 40 && r.json?.image?.scaled_by === "vinput",
    "mapping from vinput's reply: scale = w / region_client_w, offset = origin", JSON.stringify(r.json?.image));
  t.clearCalls();
  await t.call("click", { pid, x: 10, y: 20, coord_space: "capture" });
  check(JSON.stringify(positional(t.callsOf("click").at(-1), "click")) === JSON.stringify(["120", "80"]), "coord_space capture uses that mapping: (10,20) -> (120,80)", JSON.stringify(t.callsOf("click").at(-1)));

  r = await t.call("capture", { pid, region: { x: 0, y: 0, w: 100, h: 100 }, zoom: 2 });
  cap = t.callsOf("capture").at(-1);
  check(val(cap, "--scale") === undefined && val(cap, "--max-width") === undefined && r.json?.image?.scaled_by === "mcp" && r.json?.image?.scale === 2,
    "zoom (enlarging) stays in the MCP server", JSON.stringify([cap, r.json?.image]));

  r = await t.call("capture", { pid });
  cap = t.callsOf("capture").at(-1);
  check(val(cap, "--scale") === undefined && val(cap, "--format") === undefined && r.json?.image?.scaled_by === undefined && r.image?.mimeType === "image/png",
    "plain capture: no output options, PNG, no scaled_by", JSON.stringify(cap));

  r=await t.call('capture',{pid,region:{x:100,y:40,w:80,h:60},scale:3});
  im=decodePng(Buffer.from(r.image.data,'base64'));
  check(!r.isError&&im.width===240&&im.height===180&&r.json.image.scale===3&&r.json.image.offset_x===100,'scale3 enlarges a cropped inline PNG and updates its mapping');
  check(/preview/.test(r.json.preview_note??''),'enlargement explains that path names the clean native capture');
  for(const options of [{scale:3},{zoom:3},{grid:50}]){
    t.clearCalls();const noImage=await t.call('capture',{pid,include_image:false,...options});
    check(noImage.isError&&/preview/.test(noImage.text)&&t.callsOf('capture').length===0,'disabled preview cannot silently ignore an enlargement/grid request');
  }

  r = await t.call("capture", { pid, format: "jpeg", quality: 70 });
  cap = t.callsOf("capture").at(-1);
  check(!r.isError && val(cap, "--format") === "jpeg" && val(cap, "--quality") === "70" && /\.jpg$/.test(cap[cap.indexOf("capture") + 1]), "jpeg: --format jpeg --quality 70, .jpg path", JSON.stringify(cap));
  check(r.image?.mimeType === "image/jpeg" && r.json?.image?.format === "jpeg", "jpeg is returned as image/jpeg", JSON.stringify(r.json?.image));
  t.clearCalls();
  r = await t.call("capture", { pid, quality: 70 });
  check(r.isError && t.callsOf("capture").length === 0, "quality without format jpeg is refused", r.text.slice(0, 200));
  r = await t.call("capture", { pid, format: "jpeg", grid: 50 });
  check(r.isError && t.callsOf("capture").length === 0, "grid with jpeg is refused (the grid is drawn on PNG)", r.text.slice(0, 200));

  // ---- capture_burst
  r = await t.call("capture_burst", { pid, frames: 6, interval_ms: 100, columns: 3, max_width: 200 });
  const b = t.callsOf("capture-burst").at(-1);
  check(!r.isError && val(b, "--frames") === "6" && val(b, "--interval-ms") === "100" && val(b, "--columns") === "3" && val(b, "--max-width") === "200", "capture_burst argv", JSON.stringify(b));
  check(r.image?.mimeType === "image/png", "the sheet is returned as an image");
  t.clearCalls();
  r = await t.call("capture_burst", { pid, max_width: 100, scale: 0.5 });
  check(r.isError && t.callsOf("capture-burst").length === 0, "capture_burst: max_width + scale refused", r.text.slice(0, 200));
  r = await t.call("capture_burst", { pid, frames: 2, columns: 4 });
  check(r.isError && t.callsOf("capture-burst").length === 0, "capture_burst: columns > frames refused", r.text.slice(0, 200));
} finally { await t.close(); }

// an older DLL in the game: the CLI refuses the output options ("lacks"), the MCP retries plain and scales the PNG itself
const t2 = await startFake({ png: { w: 640, h: 360 }, replies: {} });
try {
  // the fake ignores output options without core_capture: it returns the unscaled frame without region_client_w
  let r = await t2.call("capture", { pid, scale: 0.5 });
  let im = decodePng(Buffer.from(r.image.data, "base64"));
  check(im.width === 320 && r.json?.image?.scaled_by === "mcp", "a reply without the core fields is scaled by the MCP server", JSON.stringify(r.json?.image));
  r = await t2.call("capture", { pid, format: "jpeg" });
  check(r.isError && /jpeg/i.test(r.text) && !r.image, "jpeg requested but no JPEG reported: explicit error, no mislabelled image", r.text.slice(0, 250));
} finally { await t2.close(); }

const t3 = await startFake({ png: { w: 640, h: 360 } });
try {
  // the real CLI refuses output options for an old DLL with a 'lacks' error before anything is captured
  t3.setScenario({ png: { w: 640, h: 360 }, replies: { capture: { ok: false, error: "target lacks capture output/map support; restart the owned target with the matching updated DLL" } } });
  t3.clearCalls();
  let r = await t3.call("capture", { pid, format: "jpeg" });
  check(r.isError && /DLL with capture output support/.test(r.text) && t3.callsOf("capture").length === 1, "jpeg on an old DLL: one attempt, explicit error", r.text.slice(0, 250));
  t3.clearCalls();
  r = await t3.call("capture", { pid, scale: 0.5 });
  check(t3.callsOf("capture").length === 2 && val(t3.callsOf("capture")[1], "--scale") === undefined, "scale on an old DLL: retried once without output options", JSON.stringify(t3.callsOf("capture")));
} finally { await t3.close(); }
console.log(state.failures ? `capture_core: FAIL (${state.failures})` : `capture_core: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
