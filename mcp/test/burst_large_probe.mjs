// Field-sized attachment control: actual MCP, fake CLI and synthetic pixels only.
import { writeFileSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { createHash } from "node:crypto";
import { encodePng, decodePng } from "../build/png.js";
import { startFake } from "./lib.mjs";
const w = 2048, h = 1536, cap = 8 * 1024 * 1024, data = Buffer.alloc(w * h * 4);
let seed = 0x27ba10;
for (let i = 0; i < data.length; i++) { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; data[i] = i % 4 === 3 ? 255 : seed & 255; }
const png = encodePng({ width: w, height: h, data }), hash = (b) => createHash("sha256").update(b).digest("hex");
const t = await startFake({}, { VINPUT_MCP_MAX_IMAGE_BYTES: String(cap) });
try {
  const path = join(t.dir, "original.png"); writeFileSync(path, png);
  const frames = [{ at_ms: 0, x: 0, y: 0, w: 1024, h: 1536, hash: "before" }, { at_ms: 500, x: 1024, y: 0, w: 1024, h: 1536, hash: "after" }];
  t.setScenario({ replies: { "capture-burst": { ok: true, path, width: w, height: h, frames, capture_space_updated: false } } });
  const r = await t.call("capture_burst", { pid: process.pid, frames: 2, interval_ms: 500 });
  const preview = r.image ? Buffer.from(r.image.data, "base64") : null, size = preview ? decodePng(preview) : null;
  const result = { original_bytes: png.length, cap, original_width: w, original_height: h,
    attached_bytes: preview?.length ?? null, attached_width: size?.width ?? null, attached_height: size?.height ?? null,
    original_unchanged: hash(png) === hash(readFileSync(path)), frames_unchanged: JSON.stringify(r.json?.frames) === JSON.stringify(frames),
    burst_calls: t.callsOf("capture-burst").length, input_calls: t.calls().length - t.callsOf("capture-burst").length,
    attachment: r.json?.attachment ?? null, is_error: r.isError };
  const passed = result.original_bytes > cap && preview && preview.length <= cap && result.original_unchanged && result.frames_unchanged &&
    result.burst_calls === 1 && result.input_calls === 0 && !result.is_error && result.attachment?.resized === true;
  console.log(JSON.stringify({ ...result, passed: !!passed }, null, 2)); process.exitCode = passed ? 0 : 1;
} finally { await t.close(); }
