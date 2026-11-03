// (B) capture `grid`: a labelled coordinate grid drawn on the RETURNED image only, in client coordinates.
import { readFileSync } from "node:fs";
import { makeChecker, startFake } from "./lib.mjs";
const { decodePng } = await import("../build/png.js");
const { state, check } = makeChecker();
const pid = process.pid;
const t = await startFake({ png: { w: 640, h: 360 } });
const px = (im, x, y) => { const o = (y * im.width + x) * 4; return [im.data[o], im.data[o + 1], im.data[o + 2]]; };
const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
const returned = (r) => decodePng(Buffer.from(r.image.data, "base64"));
try {
  const props = (await t.client.listTools()).tools.find((x) => x.name === "capture")?.inputSchema?.properties ?? {};
  check(!!props.grid, "capture schema has grid");

  const plain = await t.call("capture", { pid });
  const p = returned(plain);
  check(p.width === 640 && p.height === 360 && same(px(p, 100, 50), [100, 50, 77]), "no grid: the image is the frame untouched", JSON.stringify(px(p, 100, 50)));
  check(plain.json?.image?.scale === 1 && plain.json?.image?.offset_x === 0 && plain.json?.image?.offset_y === 0, "reply reports the image mapping (scale 1, offset 0)", JSON.stringify(plain.json?.image));

  const g = await t.call("capture", { pid, grid: 100 });
  const im = returned(g);
  check(!g.isError && im.width === 640 && im.height === 360, "grid keeps the image size", `${im.width}x${im.height}`);
  check(!same(px(im, 100, 50), [100, 50, 77]) && !same(px(im, 300, 200), [300 & 255, 200, 77]), "pixels on the grid lines (client x=100, client x=300) are drawn over", JSON.stringify([px(im, 100, 50), px(im, 300, 200)]));
  check(same(px(im, 150, 150), [150, 150, 77]) && same(px(im, 250, 330), [250, 330 & 255, 77]), "pixels between lines are untouched", JSON.stringify(px(im, 150, 150)));
  let white = 0; for (let y = 0; y < 14; y++) for (let x = 103; x < 130; x++) if (same(px(im, x, y), [255, 255, 255])) white++;
  check(white > 10, "the line at client x=100 is labelled (white digits at its top)", String(white));
  check(g.json?.image?.grid === 100, "reply says grid 100");
  const saved = decodePng(readFileSync(g.json.path));
  check(same(px(saved, 100, 50), [100, 50, 77]), "the PNG saved at path has NO grid (find_image / click coordinates stay exact)");

  const reg = await t.call("capture", { pid, region: { x: 100, y: 50, w: 200, h: 100 }, grid: 50 });
  const ri = returned(reg);
  check(ri.width === 200 && ri.height === 100, "region + grid: crop size", `${ri.width}x${ri.height}`);
  check(!same(px(ri, 50, 10), [150, 60, 77]) && !same(px(ri, 0, 40), [100, 90, 77]), "grid lines sit at CLIENT multiples (client x=150 -> image x=50, client x=100 -> image x=0)", JSON.stringify([px(ri, 50, 10), px(ri, 0, 40)]));
  check(!same(px(ri, 120, 50), [220, 100, 77]), "client y=100 -> image y=50 line", JSON.stringify(px(ri, 120, 50)));
  check(same(px(ri, 25, 25), [125, 75, 77]), "off-line pixel untouched in the crop", JSON.stringify(px(ri, 25, 25)));
  check(reg.json?.image?.offset_x === 100 && reg.json?.image?.offset_y === 50, "region capture reports offset_x/offset_y", JSON.stringify(reg.json?.image));

  const small = await t.call("capture", { pid, grid: 10 });
  check(!small.isError && !!small.image, "a dense grid (10 px) still works (labels are thinned out)");
  const bad = await t.call("capture", { pid, grid: 3 });
  check(bad.isError === true, "grid is range-checked (min 10)");

  t.setScenario({ png: { w: 640, h: 360, garbage: true } });
  const gb = await t.call("capture", { pid, grid: 100 });
  check(!gb.isError && !!gb.image && /PNG|png/.test(String(gb.json?.image_note ?? "")), "a PNG the codec cannot read: original returned untouched plus image_note", gb.text.slice(0, 300));
} finally { await t.close(); }
console.log(state.failures ? `grid: FAIL (${state.failures})` : `grid: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
