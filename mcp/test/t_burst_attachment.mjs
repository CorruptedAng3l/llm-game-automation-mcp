// Actual MCP handler, bounded synthetic PNG fixtures; never native capture/input.
import { writeFileSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { createHash } from "node:crypto";
import { encodePng, decodePng } from "../build/png.js";
import { makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker(), pid = process.pid, budget = 256;
const t = await startFake({}, { VINPUT_MCP_MAX_IMAGE_BYTES: String(budget) });
const image = (noise) => {
  const data = Buffer.alloc(24 * 24 * 4); let seed = 0x15a47;
  for (let i = 0; i < data.length; i++) { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; data[i] = i % 4 === 3 ? 255 : noise ? seed & 255 : 40; }
  return encodePng({ width: 24, height: 24, data });
};
const digest = (b) => createHash("sha256").update(b).digest("hex");
const frames = [{ i: 0, at_ms: 3, x: 0, y: 0, w: 12, h: 24, hash: "one" }, { i: 1, at_ms: 503, x: 12, y: 0, w: 12, h: 24, hash: "two" }];
const run = async (bytes, options = {}) => {
  const path = join(t.dir, "original-sheet.bin"); writeFileSync(path, bytes);
  t.setScenario({ replies: { "capture-burst": { ok: true, path, width: 24, height: 24, frames, capture_space_updated: false } } });
  t.clearCalls(); const r = await t.call("capture_burst", { pid, frames: 2, interval_ms: 500, ...options });
  return { r, path, unchanged: digest(readFileSync(path)) === digest(bytes) };
};
try {
  const flat = image(false), noisy = image(true);
  check(flat.length <= budget && noisy.length > budget, "equal-size low/high entropy controls straddle actual encoded cap");
  for (const limits of [{ maxPixels: 575, maxDimension: 8192 }, { maxPixels: 1024, maxDimension: 23 }]) {
    let refused = false; try { decodePng(noisy, limits); } catch { refused = true; }
    check(refused, `PNG allocation bounds are enforced by actual decoder: ${JSON.stringify(limits)}`);
  }
  let { r, unchanged } = await run(noisy);
  const preview = r.image ? Buffer.from(r.image.data, "base64") : null, dims = preview ? decodePng(preview) : null;
  check(!r.isError && preview && preview.length <= budget && dims.width < 24 && dims.height < 24, "oversized PNG produces actually bounded smaller preview");
  check(r.json?.attachment?.resized === true && r.json.attachment.original_bytes === noisy.length && r.json.attachment.preview_bytes === preview?.length && r.json.attachment.original_width === 24 && r.json.attachment.preview_width === dims?.width && r.json.attachment.scale_x === dims?.width / 24, "preview metadata separates saved original and attached dimensions/scale");
  check(unchanged && JSON.stringify(r.json?.frames) === JSON.stringify(frames) && r.json?.width === 24 && r.json?.height === 24, "original saved bytes and native frame geometry/hashes/times remain exact");
  check(t.callsOf("capture-burst").length === 1 && t.calls().length === 1, "budget reduction never reruns burst or sends extra input/status");
  check(r.json?.attachment?.capture_space_updated === false && r.json?.capture_space_updated === false, "temporal preview never publishes a single-image coordinate map");
  ({ r, unchanged } = await run(flat));
  check(!r.isError && r.image && Buffer.from(r.image.data, "base64").equals(flat) && unchanged && r.json?.attachment?.resized === false, "compressible same-size PNG remains byte-identical without resize");
  ({ r, unchanged } = await run(noisy, { include_image: false }));
  check(!r.isError && !r.image && unchanged && t.callsOf("capture-burst").length === 1, "include_image:false preserves saved result without deriving preview");
  ({ r, unchanged } = await run(Buffer.alloc(400, 0x41)));
  check(!r.isError && !r.image && unchanged && r.json?.attachment?.attached === false && /PNG/i.test(r.json?.attachment?.reason ?? ""), "undecodable oversized PNG is omitted honestly, capture receipt retained");
  ({ r, unchanged } = await run(Buffer.concat([Buffer.from([0xff, 0xd8]), Buffer.alloc(400)]), { format: "jpeg" }));
  check(!r.isError && !r.image && unchanged && r.json?.attachment?.attached === false && /JPEG/i.test(r.json?.attachment?.reason ?? ""), "oversized JPEG stays explicit unsupported shrink, no mislabeled PNG conversion");
  const tiny = await startFake({}, { VINPUT_MCP_MAX_IMAGE_BYTES: "1" });
  try {
    const path = join(tiny.dir, "tiny.png"); writeFileSync(path, noisy);
    tiny.setScenario({ replies: { "capture-burst": { ok: true, path, capture_space_updated: false } } });
    const r = await tiny.call("capture_burst", { pid, frames: 1 });
    check(!r.isError && !r.image && r.json?.attachment?.attached === false && r.json?.attachment?.attempts <= 4 && /budget/i.test(r.json?.attachment?.reason ?? ""), "irreducible byte cap gives bounded omission rather than false attachment");
    check(tiny.callsOf("capture-burst").length === 1, "irreducible cap still captures once");
  } finally { await tiny.close(); }
} finally { await t.close(); }
console.log(`burst attachment: ${state.passes} PASS / ${state.failures} FAIL`);
process.exit(state.failures ? 1 : 0);
