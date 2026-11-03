// (A) The pure-TS PNG codec: round trips, all five row filters, RGB / RGBA / gray, and clear errors.
import { deflateSync } from "node:zlib";
import { makeChecker } from "./lib.mjs";
const { decodePng, encodePng, PngError } = await import("../build/png.js");
const { state, check } = makeChecker();

const sig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
const T = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc = (b) => { let c = 0xffffffff; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => { const o = Buffer.alloc(12 + data.length); o.writeUInt32BE(data.length, 0); o.write(type, 4, "latin1"); data.copy(o, 8); o.writeUInt32BE(crc(o.subarray(4, 8 + data.length)), 8 + data.length); return o; };
const ihdr = (w, h, depth, ct, il = 0) => { const b = Buffer.alloc(13); b.writeUInt32BE(w, 0); b.writeUInt32BE(h, 4); b[8] = depth; b[9] = ct; b[12] = il; return b; };
/** hand-built PNG with a chosen filter per row (independent of the codec's own encoder) */
function build(w, h, ct, ch, pixel, filterOf) {
  const stride = w * ch; const rows = []; let prev = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const cur = Buffer.alloc(stride); for (let i = 0; i < stride; i++) cur[i] = pixel(y, i);
    const ft = filterOf(y), f = Buffer.alloc(stride + 1); f[0] = ft;
    for (let i = 0; i < stride; i++) {
      const a = i >= ch ? cur[i - ch] : 0, b = prev[i], c = i >= ch ? prev[i - ch] : 0;
      const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const pred = ft === 0 ? 0 : ft === 1 ? a : ft === 2 ? b : ft === 3 ? (a + b) >> 1 : (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
      f[i + 1] = (cur[i] - pred) & 255;
    }
    rows.push(f); prev = cur;
  }
  return Buffer.concat([sig, chunk("IHDR", ihdr(w, h, 8, ct)), chunk("IDAT", deflateSync(Buffer.concat(rows))), chunk("IEND", Buffer.alloc(0))]);
}
const px = (y, i) => (y * 37 + i * 91 + (i % 5) * 13) & 255;

for (let ft = 0; ft <= 4; ft++) {
  const im = decodePng(build(7, 6, 6, 4, px, () => ft));
  let ok = im.width === 7 && im.height === 6;
  for (let y = 0; y < 6 && ok; y++) for (let i = 0; i < 28; i++) if (im.data[y * 28 + i] !== px(y, i)) { ok = false; break; }
  check(ok, `RGBA decode, every row filter type ${ft}`);
}
const mixed = decodePng(build(9, 5, 6, 4, px, (y) => y % 5));
check(mixed.data[0] === px(0, 0) && mixed.data[4 * 36 + 35] === px(4, 35), "RGBA decode, filters mixed per row");
const rgb = decodePng(build(5, 4, 2, 3, px, (y) => (y * 2) % 5));
check(rgb.width === 5 && rgb.data[3] === 255 && rgb.data[0] === px(0, 0) && rgb.data[4] === px(0, 3), "RGB decodes to RGBA with alpha 255");
const gray = decodePng(build(4, 3, 0, 1, px, () => 1));
check(gray.data[0] === px(0, 0) && gray.data[1] === px(0, 0) && gray.data[3] === 255, "grayscale decodes to RGBA");

const w = 33, h = 17, data = Buffer.alloc(w * h * 4);
for (let i = 0; i < data.length; i++) data[i] = (i * 7 + (i >> 5)) & 255;
for (let i = 3; i < data.length; i += 4) data[i] = 255;
const back = decodePng(encodePng({ width: w, height: h, data }));
check(back.width === w && back.height === h && back.data.equals(data), "encode -> decode round trip, opaque (written as RGB)");
check(encodePng({ width: w, height: h, data })[25] === 2, "opaque image is written as colour type 2 (RGB)");
for (let i = 3; i < data.length; i += 4) data[i] = (i * 3) & 255;
check(decodePng(encodePng({ width: w, height: h, data })).data.equals(data), "encode -> decode round trip with real alpha (RGBA)");

const one = decodePng(Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64"));
check(one.width === 1 && one.height === 1, "a real 1x1 RGBA file decodes");

const err = (buf) => { try { decodePng(buf); return null; } catch (e) { return e; } };
const tail = [chunk("IDAT", deflateSync(Buffer.alloc(100))), chunk("IEND", Buffer.alloc(0))];
let e = err(Buffer.from("not a png at all, just text........................"));
check(e instanceof PngError && /signature/.test(e.message), "garbage: clear 'bad signature' error", e?.message);
e = err(Buffer.concat([sig, chunk("IHDR", ihdr(2, 2, 16, 6)), ...tail]));
check(e instanceof PngError && /bit depth 16/.test(e.message), "16-bit: clear 'unsupported' error", e?.message);
e = err(Buffer.concat([sig, chunk("IHDR", ihdr(2, 2, 8, 6, 1)), ...tail]));
check(e instanceof PngError && /interlaced/.test(e.message), "interlaced: clear 'unsupported' error", e?.message);
e = err(Buffer.concat([sig, chunk("IHDR", ihdr(2, 2, 8, 3)), ...tail]));
check(e instanceof PngError && /color type 3/.test(e.message), "palette: clear 'unsupported' error", e?.message);
const good = build(4, 4, 6, 4, px, () => 0);
e = err(good.subarray(0, good.length - 20));
check(e instanceof PngError && /truncated/.test(e.message), "truncated file: clear error", e?.message);
const bad = Buffer.from(good); bad[bad.length - 20] ^= 0xff;
e = err(bad);
check(e instanceof PngError && /CRC|corrupt|truncated/.test(e.message), "flipped byte: clear error", e?.message);
console.log(state.failures ? `png: FAIL (${state.failures})` : `png: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
